#include "peek.h"
#include <fcntl.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/base64.h>
#include "ws.h"

#define WSHDR 8192
#define WSMAX 8388608u
#define WSOMAX 16777216u
#define WSRSZ 8192
#define WSLICE 256
#define WSREAD (1u << 20)
#define WSCLO 5000
#define WSUB 2048

static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct Ws {
    Ws *next;
    char *url, *proto, *rbuf, *msg, *out;
    void *ud;
    WsHooks hk;
    NetStream *ns;
    size_t rn, rcap, pos, hend, need, mn, macap, on, ocap, ooff, cn;
    long dl;
    int st, op, fin, msgop, rep, dead, ondead, ccode;
    unsigned char ctrl[128];
    char bkey[28], acc[32], creason[128];
};

static Ws *LIST, *DEADL, *GRAV;
static Ws **VEC;
static size_t CVEC;
static int INP;
static uint64_t RX;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint32_t rol(uint32_t v, unsigned k) {
    return v << k | v >> (32 - k);
}

static void sha1(const void *in, size_t n, unsigned char out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t bl = (n + 9 + 63) / 64 * 64;
    unsigned char *b = calloc(1, bl);
    if (!b) oom();
    memcpy(b, in, n);
    b[n] = 0x80;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) b[bl - 1 - i] = (unsigned char)(bits >> (i * 8));
    uint32_t w[80];
    for (size_t off = 0; off < bl; off += 64) {
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)b[off + i * 4] << 24 | (uint32_t)b[off + i * 4 + 1] << 16 |
                   (uint32_t)b[off + i * 4 + 2] << 8 | b[off + i * 4 + 3];
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], c = h[1], d = h[2], e = h[3], f = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t g, k;
            if (i < 20) g = (c & d) | (~c & e), k = 0x5A827999u;
            else if (i < 40) g = c ^ d ^ e, k = 0x6ED9EBA1u;
            else if (i < 60) g = (c & d) | (c & e) | (d & e), k = 0x8F1BBCDCu;
            else g = c ^ d ^ e, k = 0xCA62C1D6u;
            uint32_t t = rol(a, 5) + g + f + k + w[i];
            f = e;
            e = d;
            d = rol(c, 30);
            c = a;
            a = t;
        }
        h[0] += a;
        h[1] += c;
        h[2] += d;
        h[3] += e;
        h[4] += f;
    }
    free(b);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (unsigned char)(h[i] >> (24 - j * 8));
}

static int b64(const unsigned char *in, size_t n, char *out, size_t osz) {
    size_t ol = 0;
    if (mbedtls_base64_encode((unsigned char *)out, osz, &ol, in, n) || ol >= osz) return 0;
    out[ol] = 0;
    return 1;
}

static void rnd(unsigned char *b, size_t n) {
    int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (f >= 0) {
        size_t g = 0;
        while (g < n) {
            ssize_t r = read(f, b + g, n - g);
            if (r <= 0) break;
            g += (size_t)r;
        }
        close(f);
        if (g == n) return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (!RX)
        RX = ((uint64_t)ts.tv_sec << 31) ^ ((uint64_t)ts.tv_nsec << 7) ^
             (uint64_t)(uintptr_t)b ^ ((uint64_t)getpid() << 17) ^ 0x9E3779B97F4A7C15ull;
    for (size_t i = 0; i < n; i++) {
        RX ^= RX << 13;
        RX ^= RX >> 7;
        RX ^= RX << 17;
        b[i] = (unsigned char)(RX >> 33);
    }
}

static char *mfind(const char *h, size_t hn, const char *n, size_t nn) {
    if (hn < nn) return 0;
    for (size_t i = 0; i + nn <= hn; i++)
        if (!memcmp(h + i, n, nn)) return (char *)(h + i);
    return 0;
}

static void *gbuf(void *p, size_t *cap, size_t want) {
    if (*cap >= want && *cap) return p;
    size_t c = *cap ? *cap : 8192;
    while (c < want) c <<= 1;
    void *q = realloc(p, c);
    if (!q) oom();
    *cap = c;
    return q;
}

static int hv(const char *blk, size_t bl, const char *name, char *v, size_t vs) {
    size_t nlen = strlen(name);
    *v = 0;
    for (const char *p = blk, *e = blk + bl; p < e; p++) {
        const char *eol = memchr(p, '\n', (size_t)(e - p));
        if (!eol) break;
        const char *le = eol > p && eol[-1] == '\r' ? eol - 1 : eol;
        const char *ks = p;
        while (ks < le && (*ks == ' ' || *ks == '\t')) ks++;
        const char *c = memchr(ks, ':', (size_t)(le - ks));
        const char *nx = eol + 1;
        if (c && (size_t)(c - ks) == nlen && !strncasecmp(ks, name, nlen)) {
            const char *s = c + 1;
            while (s < le && (*s == ' ' || *s == '\t')) s++;
            size_t l = (size_t)(le - s);
            if (l >= vs) l = vs - 1;
            memcpy(v, s, l);
            v[l] = 0;
            return 1;
        }
        p = nx - 1;
    }
    return 0;
}

static void unl(Ws **h, Ws *w) {
    while (*h) {
        if (*h == w) {
            *h = w->next;
            w->next = 0;
            return;
        }
        h = &(*h)->next;
    }
}

static void retire(Ws *w) {
    unl(&LIST, w);
    if (w->ondead) return;
    w->ondead = 1;
    w->next = DEADL;
    DEADL = w;
}

static void freebufs(Ws *w) {
    free(w->rbuf);
    w->rbuf = 0;
    free(w->msg);
    w->msg = 0;
    free(w->out);
    w->out = 0;
    w->rcap = w->rn = w->pos = w->hend = w->need = 0;
    w->macap = w->mn = w->ocap = w->on = w->ooff = w->cn = 0;
}

static void stop(Ws *w) {
    ns_close(w->ns);
    w->ns = 0;
    freebufs(w);
}

static void drop(Ws *w) {
    stop(w);
    free(w->url);
    free(w->proto);
    free(w);
}

static int failf(Ws *w, const char *what) {
    stop(w);
    w->st = WS_CLOSED;
    retire(w);
    if (!w->rep) {
        w->rep = 1;
        if (w->hk.on_err) w->hk.on_err(w, what);
    }
    return -1;
}

static void closed(Ws *w, int code, const char *reason) {
    stop(w);
    w->st = WS_CLOSED;
    retire(w);
    if (w->rep) return;
    w->rep = 1;
    if (w->hk.on_close) w->hk.on_close(w, code, reason ? reason : "");
}

static int qspace(Ws *w, size_t need) {
    size_t live = w->on - w->ooff;
    if (live + need > WSOMAX) return -1;
    if (w->ooff && (w->ooff > WSRSZ || w->ocap < live + need)) {
        if (live) memmove(w->out, w->out + w->ooff, live);
        w->on = live;
        w->ooff = 0;
    }
    w->out = gbuf(w->out, &w->ocap, w->on + need);
    return 0;
}

static void qput(Ws *w, const void *b, size_t n) {
    if (n) memcpy(w->out + w->on, b, n);
    w->on += n;
}

static int qframe(Ws *w, int op, const void *pay, size_t n) {
    if (!w->ns) return -1;
    unsigned char h[10], mk[4];
    size_t hl = 2;
    h[0] = (unsigned char)(0x80 | op);
    if (n < 126) {
        h[1] = (unsigned char)(0x80 | n);
    } else if (n <= 0xffff) {
        h[1] = 0xFE;
        h[2] = (unsigned char)(n >> 8);
        h[3] = (unsigned char)n;
        hl = 4;
    } else {
        h[1] = 0xFF;
        uint64_t v = n;
        for (int i = 0; i < 8; i++) h[2 + i] = (unsigned char)(v >> (56 - i * 8));
        hl = 10;
    }
    rnd(mk, 4);
    if (qspace(w, hl + 4 + n)) return -1;
    size_t at = w->on + hl + 4;
    qput(w, h, hl);
    qput(w, mk, 4);
    qput(w, pay, n);
    unsigned char *p = (unsigned char *)w->out + at;
    for (size_t i = 0; i < n; i++) p[i] ^= mk[i & 3];
    return 0;
}

static void flush(Ws *w) {
    while (w->ns && w->on > w->ooff) {
        long k = ns_write(w->ns, w->out + w->ooff, (long)(w->on - w->ooff));
        if (k > 0) {
            w->ooff += (size_t)k;
            if (w->ooff == w->on) w->ooff = w->on = 0;
            continue;
        }
        if (!k) return;
        failf(w, "write failed");
        return;
    }
}

static int deliver(Ws *w, int bin) {
    size_t n = w->mn;
    w->mn = 0;
    if (w->hk.on_msg) w->hk.on_msg(w, w->msg ? w->msg : "", n, bin);
    return 1;
}

static int ctrl(Ws *w, int op) {
    size_t n = w->cn;
    w->cn = 0;
    if (op == 9) {
        qframe(w, 10, w->ctrl, n);
        return 1;
    }
    if (op == 10) return 1;
    if (n == 1) return failf(w, "short close frame");
    int code = 1005;
    const char *rs = "";
    if (n >= 2) {
        code = (w->ctrl[0] << 8 | w->ctrl[1]) & 0xffff;
        if (code == 1006 || code == 1015 || (code > 1013 && code < 3000) || code >= 5000)
            return failf(w, "bad close code");
        if (n > 2) {
            size_t l = n - 2;
            if (l > sizeof w->creason - 1) l = sizeof w->creason - 1;
            memcpy(w->creason, w->ctrl + 2, l);
            w->creason[l] = 0;
            rs = w->creason;
        }
    }
    if (w->st == WS_OPEN && code != 1005) {
        unsigned char p[2];
        p[0] = (unsigned char)(code >> 8);
        p[1] = (unsigned char)code;
        if (!qframe(w, 8, p, 2)) flush(w);
        if (w->dead || !w->ns) return -1;
    }
    closed(w, code, rs);
    return -1;
}

static int done(Ws *w) {
    int op = w->op;
    if (op >= 8) return ctrl(w, op);
    if (!w->fin) {
        if (op) w->msgop = op;
        return 1;
    }
    int bin = (op ? op : w->msgop) == 2;
    w->msgop = -1;
    return deliver(w, bin);
}

static int fr(Ws *w) {
    size_t av = w->rn - w->pos;
    if (!w->need) {
        if (av < 2) return 0;
        unsigned char *p = (unsigned char *)w->rbuf + w->pos;
        int op = p[0] & 15, fin = p[0] >> 7;
        if (p[0] & 0x70) return failf(w, "reserved bits set");
        if (p[1] & 0x80) return failf(w, "masked frame from server");
        if (op > 2 && op < 8) return failf(w, "reserved opcode");
        if (op > 10) return failf(w, "bad opcode");
        uint64_t l = p[1] & 127;
        size_t hl = 2;
        if (op >= 8 && (l > 125 || !fin)) return failf(w, "bad control frame");
        if (l == 126) {
            if (av < 4) return 0;
            l = (uint64_t)p[2] << 8 | p[3];
            if (l < 126) return failf(w, "non-minimal length");
            hl = 4;
        } else if (l == 127) {
            if (av < 10) return 0;
            l = 0;
            for (int i = 0; i < 8; i++) l = l << 8 | p[2 + i];
            if (l < 65536 || l >> 62) return failf(w, "bad length");
            hl = 10;
        }
        if (op < 8) {
            if (!op && w->msgop < 0) return failf(w, "unexpected continuation");
            if (op && w->msgop >= 0) return failf(w, "interleaved data frame");
            if (l > WSMAX || w->mn + (size_t)l > WSMAX) return failf(w, "message too large");
        }
        w->pos += hl;
        w->op = op;
        w->fin = fin;
        w->need = (size_t)l;
        av -= hl;
        if (!w->need) return done(w);
    }
    size_t k = av < w->need ? av : w->need;
    if (!k) return 0;
    if (w->op < 8) {
        w->msg = gbuf(w->msg, &w->macap, w->mn + k);
        memmove(w->msg + w->mn, w->rbuf + w->pos, k);
        w->mn += k;
    } else {
        size_t room = sizeof w->ctrl - w->cn;
        if (k > room) k = room;
        memmove(w->ctrl + w->cn, w->rbuf + w->pos, k);
        w->cn += k;
    }
    w->pos += k;
    w->need -= k;
    if (w->need) return 1;
    return done(w);
}

static int hs(Ws *w) {
    if (!w->hend) {
        size_t lim = w->rn > WSHDR ? WSHDR : w->rn;
        char *e = mfind(w->rbuf, lim, "\r\n\r\n", 4);
        if (!e) return w->rn >= WSHDR ? failf(w, "handshake too large") : 0;
        w->hend = (size_t)(e + 4 - w->rbuf);
    }
    if (w->hend < 12 || memcmp(w->rbuf, "HTTP/1.", 7)) return failf(w, "bad handshake");
    if (atoi(w->rbuf + 9) != 101) return failf(w, "bad handshake status");
    char v[160];
    if (!hv(w->rbuf, w->hend, "upgrade", v, sizeof v)) return failf(w, "no upgrade header");
    for (char *q = v; *q; q++) *q = (char)tolower((unsigned char)*q);
    if (!strstr(v, "websocket")) return failf(w, "bad upgrade header");
    if (!hv(w->rbuf, w->hend, "connection", v, sizeof v)) return failf(w, "no connection header");
    for (char *q = v; *q; q++) *q = (char)tolower((unsigned char)*q);
    if (!strstr(v, "upgrade")) return failf(w, "bad connection header");
    if (!hv(w->rbuf, w->hend, "sec-websocket-accept", v, sizeof v) || strcmp(v, w->acc))
        return failf(w, "bad sec-websocket-accept");
    if (hv(w->rbuf, w->hend, "sec-websocket-extensions", v, sizeof v) && *v)
        return failf(w, "extension not supported");
    if (hv(w->rbuf, w->hend, "sec-websocket-protocol", v, sizeof v) && *v)
        w->proto = sdup(v, strlen(v));
    w->pos = w->hend;
    w->st = WS_OPEN;
    w->msgop = -1;
    if (w->hk.on_open) w->hk.on_open(w);
    return 1;
}

static int rd(Ws *w) {
    if (w->rn + WSRSZ + 1 > w->rcap) {
        if (w->pos) {
            memmove(w->rbuf, w->rbuf + w->pos, w->rn - w->pos);
            w->rn -= w->pos;
            w->pos = 0;
        }
        w->rbuf = gbuf(w->rbuf, &w->rcap, w->rn + WSRSZ + 1);
    }
    long k = ns_read(w->ns, w->rbuf + w->rn, WSRSZ);
    if (k > 0) {
        w->rn += (size_t)k;
        w->rbuf[w->rn] = 0;
        return 1;
    }
    return (int)k;
}

static void eofin(Ws *w) {
    if (w->st == WS_CONNECTING) {
        failf(w, "closed during handshake");
        return;
    }
    if (w->st == WS_CLOSING) {
        closed(w, w->ccode, w->creason);
        return;
    }
    closed(w, 1006, "");
}

static int parse(Ws *w) {
    if (w->st == WS_CONNECTING) {
        int r = hs(w);
        if (r != 1) return r;
    }
    if (w->dead || !w->ns) return -1;
    return fr(w);
}

static void pump(Ws *w) {
    size_t got = 0;
    for (int i = 0; i < WSLICE; i++) {
        int r = parse(w);
        if (r < 0) return;
        if (w->dead || w->st == WS_CLOSED) return;
        if (r) continue;
        int k = rd(w);
        if (k < 0) {
            eofin(w);
            return;
        }
        if (!k) return;
        got += WSRSZ;
        if (got >= WSREAD) return;
    }
}

static void step(Ws *w) {
    flush(w);
    if (w->dead || !w->ns) return;
    pump(w);
    if (w->dead || !w->ns) return;
    flush(w);
    if (w->dead || !w->ns) return;
    if (w->st == WS_CLOSING && w->on == w->ooff && now_ms() >= w->dl)
        closed(w, w->ccode, w->creason);
}

void ws_poll(void) {
    if (INP || !LIST) return;
    int n = 0;
    for (Ws *x = LIST; x; x = x->next) n++;
    VEC = gbuf(VEC, &CVEC, ((size_t)n + 1) * sizeof *VEC);
    int i = 0;
    for (Ws *x = LIST; x; x = x->next) VEC[i++] = x;
    INP = 1;
    for (i = 0; i < n; i++)
        if (!VEC[i]->dead) step(VEC[i]);
    INP = 0;
    while (GRAV) {
        Ws *x = GRAV->next;
        drop(GRAV);
        GRAV = x;
    }
}

static int wparse(const char *u, char *host, char *hh, int *port, char *path, int *tls) {
    int t = 0, pl = 5;
    if (!strncasecmp(u, "wss://", 6)) t = 1, pl = 6;
    else if (strncasecmp(u, "ws://", 5)) return 0;
    const char *a = u + pl, *e = a;
    while (*e && *e != '/' && *e != '?' && *e != '#') e++;
    const char *hs = a, *he = e, *cs = 0;
    int p = t ? 443 : 80;
    if (*hs == '[') {
        const char *rb = memchr(hs, ']', (size_t)(e - hs));
        if (!rb) return 0;
        cs = rb + 1 < e && rb[1] == ':' ? rb + 1 : 0;
    } else {
        cs = memchr(a, ':', (size_t)(e - a));
    }
    if (cs) {
        he = cs;
        p = 0;
        for (const char *q = cs + 1; q < e; q++) {
            if (*q < '0' || *q > '9') return 0;
            p = p * 10 + (*q - '0');
        }
    }
    size_t hl = (size_t)(he - hs);
    if (!hl || hl >= 256 || !p || p > 65535) return 0;
    if (*hs == '[' && he[-1] == ']') {
        memcpy(host, hs + 1, hl - 2);
        host[hl - 2] = 0;
    } else {
        memcpy(host, hs, hl);
        host[hl] = 0;
    }
    memcpy(hh, hs, hl);
    hh[hl] = 0;
    if (p != (t ? 443 : 80)) snprintf(hh + hl, 16, ":%d", p);
    *port = p;
    *tls = t;
    const char *f = strchr(e, '#');
    size_t n = f ? (size_t)(f - e) : strlen(e);
    if (!n) {
        path[0] = '/';
        path[1] = 0;
        return 1;
    }
    if (n >= WSUB) n = WSUB - 1;
    memcpy(path, e, n);
    path[n] = 0;
    return 1;
}

Ws *ws_connect(const char *url, const WsHooks *hooks, void *ud) {
    char host[256], hh[288], path[WSUB], req[4096], cat[80];
    int port, tls;
    if (!url || !wparse(url, host, hh, &port, path, &tls)) return 0;
    NetStream *ns = ns_connect(host, port, tls);
    if (!ns) return 0;
    Ws *w = calloc(1, sizeof *w);
    if (!w) oom();
    w->ns = ns;
    w->ud = ud;
    w->st = WS_CONNECTING;
    w->msgop = -1;
    w->ccode = 1005;
    if (hooks) w->hk = *hooks;
    w->url = sdup(url, strlen(url));
    unsigned char rb[16], dg[20];
    rnd(rb, 16);
    if (!b64(rb, 16, w->bkey, sizeof w->bkey)) {
        drop(w);
        return 0;
    }
    snprintf(cat, sizeof cat, "%s%s", w->bkey, GUID);
    sha1(cat, strlen(cat), dg);
    if (!b64(dg, 20, w->acc, sizeof w->acc)) {
        drop(w);
        return 0;
    }
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: peek/1.0\r\n"
        "\r\n", path, hh, w->bkey);
    if (rl <= 0 || (size_t)rl >= sizeof req || qspace(w, (size_t)rl)) {
        drop(w);
        return 0;
    }
    qput(w, req, (size_t)rl);
    w->next = LIST;
    LIST = w;
    flush(w);
    return w;
}

int ws_send(Ws *w, const char *data, size_t n, int binary) {
    if (!w || w->dead || (w->st != WS_OPEN && w->st != WS_CONNECTING) || (n && !data)) return -1;
    if (qframe(w, binary ? 2 : 1, data, n)) return -1;
    flush(w);
    return 0;
}

void ws_close(Ws *w, int code, const char *reason) {
    if (!w || w->dead || w->st == WS_CLOSED || w->st == WS_CLOSING) return;
    if (!code) code = 1005;
    w->ccode = code;
    snprintf(w->creason, sizeof w->creason, "%s", reason ? reason : "");
    w->st = WS_CLOSING;
    w->dl = now_ms() + WSCLO;
    int wc = code == 1000 || code == 1001 || code == 1002 || code == 1003 ||
             (code >= 1007 && code <= 1011) || (code >= 3000 && code <= 4999) ? code : 1000;
    unsigned char p[128];
    p[0] = (unsigned char)(wc >> 8);
    p[1] = (unsigned char)wc;
    size_t n = 2;
    if (reason && *reason) {
        size_t rl = strlen(reason);
        if (rl > sizeof p - 2) rl = sizeof p - 2;
        memcpy(p + 2, reason, rl);
        n += rl;
    }
    qframe(w, 8, p, n);
}

void ws_free(Ws *w) {
    if (!w || w->dead) return;
    unl(&LIST, w);
    unl(&DEADL, w);
    w->ondead = 0;
    w->dead = 1;
    if (INP) {
        w->next = GRAV;
        GRAV = w;
        return;
    }
    drop(w);
}

int ws_active(void) {
    int n = 0;
    for (Ws *w = LIST; w; w = w->next) n++;
    return n;
}

int ws_state(Ws *w) {
    return w && !w->dead ? w->st : WS_CLOSED;
}

void *ws_ud(Ws *w) {
    return w ? w->ud : 0;
}

const char *ws_url(Ws *w) {
    return w && !w->dead ? w->url : "";
}

const char *ws_protocol(Ws *w) {
    if (!w || w->dead || !w->proto) return "";
    return w->proto;
}
