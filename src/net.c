#include "peek.h"
#include "platform.h"
#include <time.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#define UBSZ 2048
#define NS_TMO 10000

int url_is(const char *s) {
    return !pk_strncasecmp(s, "http://", 7) || !pk_strncasecmp(s, "https://", 8);
}

static int uparse(const char *u, char *host, char *hh, int *port, char *path, int *tls) {
    int t = 0, pl = 7;
    if (!pk_strncasecmp(u, "https://", 8)) t = 1, pl = 8;
    else if (pk_strncasecmp(u, "http://", 7)) return 0;
    const char *a = u + pl, *e = a;
    while (*e && *e != '/') e++;
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
        for (const char *q = cs + 1; q < e; q++) p = p * 10 + (*q & 15);
    }
    size_t hl = (size_t)(he - hs);
    if (!hl || hl >= 256) return 0;
    if (*hs == '[' && he[-1] == ']') {
        memcpy(host, hs + 1, hl - 2);
        host[hl - 2] = 0;
    } else {
        memcpy(host, hs, hl);
        host[hl] = 0;
    }
    memcpy(hh, hs, hl);
    hh[hl] = 0;
    if (p != (t ? 443 : 80)) snprintf(hh + hl, 8, ":%d", p);
    *port = p;
    *tls = t;
    if (!*e) strcpy(path, "/");
    else snprintf(path, UBSZ, "%s", e);
    return 1;
}

char *url_join(const char *b, const char *h) {
    size_t bl = strlen(b), hl = strlen(h);
    if (!hl) return sdup(b, bl);
    if (url_is(h)) return sdup(h, hl);
    char *r;
    if (h[0] == '/' && h[1] == '/') {
        const char *c = strchr(b, ':');
        size_t sl = c ? (size_t)(c - b) + 1 : 5;
        r = malloc(sl + hl + 1);
        if (!r) oom();
        memcpy(r, b, sl);
        memcpy(r + sl, h, hl + 1);
        return r;
    }
    const char *ps = 0;
    if (url_is(b)) {
        const char *a = strstr(b, "//");
        if (a) ps = strchr(a + 2, '/');
    }
    if (h[0] == '/') {
        size_t pl = ps ? (size_t)(ps - b) : bl;
        r = malloc(pl + hl + 1);
        if (!r) oom();
        memcpy(r, b, pl);
        memcpy(r + pl, h, hl + 1);
        return r;
    }
    const char *ct = ps ? strrchr(ps, '/') : 0;
    if (!ct) {
        r = malloc(bl + hl + 2);
        if (!r) oom();
        memcpy(r, b, bl);
        r[bl] = '/';
        memcpy(r + bl + 1, h, hl + 1);
        return r;
    }
    size_t kl = (size_t)(ct - b) + 1;
    r = malloc(kl + hl + 1);
    if (!r) oom();
    memcpy(r, b, kl);
    memcpy(r + kl, h, hl + 1);
    return r;
}

static long clock_ms(void) {
    return (long)pk_now_ms();
}

static pk_socket_t conn_tcp(const char *host, int port, int nb, int tms) {
    if (pk_socket_init()) return PK_INVALID_SOCKET;
    char ps[8];
    snprintf(ps, sizeof ps, "%d", port);
    pk_addrinfo *list;
    if (pk_resolve(host, ps, &list)) return PK_INVALID_SOCKET;
    pk_socket_t fd = PK_INVALID_SOCKET;
    for (pk_addrinfo *p = pk_addrinfo_first(list); p; p = pk_addrinfo_next(p)) {
        fd = pk_socket_create(pk_addrinfo_family(p), pk_addrinfo_type(p), pk_addrinfo_protocol(p));
        if (fd == PK_INVALID_SOCKET) continue;
        pk_socket_nonblock(fd, 1);
        int c = pk_socket_connect(fd, pk_addrinfo_address(p), pk_addrinfo_length(p));
        if (c < 0) { pk_socket_close(fd); fd = PK_INVALID_SOCKET; continue; }
        if (c > 0) {
            if (pk_socket_wait(fd, PK_POLL_OUT, tms) <= 0 ||
                pk_socket_error(fd)) {
                pk_socket_close(fd);
                fd = PK_INVALID_SOCKET;
                continue;
            }
        }
        break;
    }
    pk_resolve_free(list);
    if (fd == PK_INVALID_SOCKET) return fd;
    if (nb) return fd;
    pk_socket_nonblock(fd, 0);
    pk_socket_set_timeout(fd, 10);
    return fd;
}

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config cfg;
    mbedtls_x509_crt ca;
    pk_socket_t fd;
    int tls, nb, ini;
} Tls;

static int mb_send(void *ctx, const unsigned char *b, size_t n) {
    Tls *t = ctx;
    int w = pk_socket_send(t->fd, b, n);
    if (w >= 0) return w;
    if (t->nb && pk_socket_would_block()) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int mb_recv(void *ctx, unsigned char *b, size_t n) {
    Tls *t = ctx;
    int r = pk_socket_recv(t->fd, b, n);
    if (r > 0) return r;
    if (r < 0 && pk_socket_would_block()) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tls_up(Tls *t, const char *host) {
    long dl = clock_ms() + NS_TMO;
    if (psa_crypto_init() != PSA_SUCCESS) {
        fprintf(stderr, "peek: tls: psa init failed\n");
        return 0;
    }
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->cfg);
    mbedtls_x509_crt_init(&t->ca);
    t->ini = 1;
    char ca[1024];
    if (!pk_tls_ca_path(ca, sizeof ca) ||
        mbedtls_x509_crt_parse_file(&t->ca, ca) < 0) {
        fprintf(stderr, "peek: tls: no ca bundle\n");
        return 0;
    }
    if (mbedtls_ssl_config_defaults(&t->cfg, MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) return 0;
    mbedtls_ssl_conf_session_tickets(&t->cfg, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    mbedtls_ssl_conf_authmode(&t->cfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->cfg, &t->ca, 0);
    if (mbedtls_ssl_setup(&t->ssl, &t->cfg)) return 0;
    if (mbedtls_ssl_set_hostname(&t->ssl, host)) return 0;
    mbedtls_ssl_set_bio(&t->ssl, t, mb_send, mb_recv, 0);
    t->tls = 1;
    int r;
    while ((r = mbedtls_ssl_handshake(&t->ssl))) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            fprintf(stderr, "peek: tls: -0x%x\n", -r);
            return 0;
        }
        if (!t->nb) continue;
        int left = (int)(dl - clock_ms());
        if (left <= 0) { fprintf(stderr, "peek: tls: handshake timeout\n"); return 0; }
        pk_socket_wait(t->fd, r == MBEDTLS_ERR_SSL_WANT_WRITE ? PK_POLL_OUT : PK_POLL_IN, left);
    }
    return 1;
}

static void tls_done(Tls *t) {
    if (!t->ini) return;
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->cfg);
    mbedtls_x509_crt_free(&t->ca);
    t->ini = 0;
    t->tls = 0;
}

static long io_send(Tls *t, const char *b, size_t n) {
    if (t->tls) {
        int r = mbedtls_ssl_write(&t->ssl, (const unsigned char *)b, n);
        if (r > 0) return r;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
        return -1;
    }
    int w = pk_socket_send(t->fd, b, n);
    if (w > 0) return w;
    if (w < 0 && pk_socket_would_block()) return 0;
    return -1;
}

static long io_read(Tls *t, char *b, size_t n) {
    if (t->tls) {
        int r = mbedtls_ssl_read(&t->ssl, (unsigned char *)b, n);
        if (r > 0) return r;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
            r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return 0;
        return -1;
    }
    int r = pk_socket_recv(t->fd, b, n);
    if (r > 0) return r;
    if (r < 0 && pk_socket_would_block()) return 0;
    return -1;
}

struct NetStream {
    Tls t;
    char *pb;
    size_t pn, poff;
};

NetStream *ns_connect(const char *host, int port, int tls) {
    pk_socket_t fd = conn_tcp(host, port, 1, NS_TMO);
    if (fd == PK_INVALID_SOCKET) return 0;
    NetStream *s = calloc(1, sizeof *s);
    if (!s) oom();
    s->t.fd = fd;
    s->t.nb = 1;
    if (tls && !tls_up(&s->t, host)) { ns_close(s); return 0; }
    return s;
}

pk_socket_t ns_fd(NetStream *s) {
    return s ? s->t.fd : PK_INVALID_SOCKET;
}

long ns_read(NetStream *s, char *buf, long n) {
    if (!s || n <= 0) return -1;
    if (s->poff < s->pn) {
        size_t av = s->pn - s->poff, take = (size_t)n < av ? (size_t)n : av;
        memcpy(buf, s->pb + s->poff, take);
        s->poff += take;
        return (long)take;
    }
    return io_read(&s->t, buf, (size_t)n);
}

long ns_write(NetStream *s, const char *buf, long n) {
    if (!s || n < 0) return -1;
    if (!n) return 0;
    return io_send(&s->t, buf, (size_t)n);
}

void ns_close(NetStream *s) {
    if (!s) return;
    if (s->t.tls && s->t.nb) mbedtls_ssl_close_notify(&s->t.ssl);
    tls_done(&s->t);
    if (s->t.fd != PK_INVALID_SOCKET) pk_socket_close(s->t.fd);
    free(s->pb);
    free(s);
}

static size_t dechunk(char *s, size_t n) {
    char *r = s, *w = s, *e = s + n;
    for (;;) {
        char *nl = memchr(r, '\n', (size_t)(e - r));
        if (!nl) break;
        size_t sz = 0;
        for (char *q = r; q < nl && *q != '\r' && *q != ';'; q++)
            sz = sz << 4 | (size_t)((*q & 15) + (*q >> 6) * 9 & 15);
        r = nl + 1;
        if (!sz) break;
        if (r + sz > e) sz = (size_t)(e - r);
        memmove(w, r, sz);
        w += sz;
        r += sz;
        nl = memchr(r, '\n', (size_t)(e - r));
        if (!nl) break;
        r = nl + 1;
    }
    return (size_t)(w - s);
}

static int hdr_has(const char *hdrs, const char *name) {
    if (!hdrs || !*hdrs) return 0;
    int nl = (int)strlen(name);
    for (const char *p = hdrs; *p; p++) {
        if (p != hdrs && p[-1] != '\n') continue;
        if (!pk_strncasecmp(p, name, (size_t)nl)) {
            const char *q = p + nl;
            while (*q == ' ') q++;
            if (*q == ':') return 1;
        }
    }
    return 0;
}

static char *req_build(const char *method, const char *path, const char *hh,
                       const char *body, size_t blen, const char *hdrs, int *rlen) {
    size_t hl = hdrs ? strlen(hdrs) : 0;
    int own = hdr_has(hdrs, "content-type"), ownl = hdr_has(hdrs, "content-length");
    size_t rsz = strlen(method) + strlen(path) + strlen(hh) + hl + 512;
    if (body) rsz += (own ? 0 : 48) + (ownl ? 0 : 40) + blen + 2;
    char *req = malloc(rsz + 1);
    if (!req) oom();
    int n = snprintf(req, rsz,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: peek/1.0\r\n"
        "Accept: text/html,*/*\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "%s%s",
        method, path, hh, hdrs ? hdrs : "",
        hdrs && *hdrs && hdrs[strlen(hdrs) - 1] != '\n' ? "\r\n" : "");
    if (body) {
        if (!own) n += snprintf(req + n, rsz - (size_t)n, "Content-Type: application/json\r\n");
        if (!ownl) n += snprintf(req + n, rsz - (size_t)n, "Content-Length: %zu\r\n", blen);
        n += snprintf(req + n, rsz - (size_t)n, "\r\n");
        if (n > (int)rsz - 1) n = (int)rsz - 1;
        memcpy(req + n, body, blen);
        n += (int)blen;
    } else
        n += snprintf(req + n, rsz - (size_t)n, "\r\n");
    *rlen = n;
    return req;
}

static char *fetch1(const char *host, const char *hh, int port, const char *path, int tls,
                    const char *method, const char *body, size_t blen, const char *hdrs,
                    char *nurl, size_t nsz, int *code, size_t *len, char **head) {
    *code = 0;
    *nurl = 0;
    *len = 0;
    if (head) *head = 0;
    pk_socket_t fd = conn_tcp(host, port, 0, NS_TMO);
    if (fd == PK_INVALID_SOCKET) { fprintf(stderr, "peek: connect %s:%d\n", host, port); return 0; }
    Tls T = {0};
    T.fd = fd;
    char *buf = 0, *res = 0, *req = 0;
    if (tls && !tls_up(&T, host)) goto out;
    int rl = 0;
    req = req_build(method, path, hh, body, blen, hdrs, &rl);
    time_t dl = time(0) + 30;
    size_t off = 0;
    while (off < (size_t)rl) {
        long w = io_send(&T, req + off, (size_t)rl - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (!w && time(0) < dl) continue;
        fprintf(stderr, "peek: send %s\n", host);
        goto out;
    }
    size_t cap = 1 << 16, n = 0;
    buf = malloc(cap);
    if (!buf) oom();
    for (;;) {
        if (n + 4096 > cap) {
            cap <<= 1;
            char *nb = realloc(buf, cap);
            if (!nb) oom();
            buf = nb;
        }
        long r = io_read(&T, buf + n, 4096);
        if (r > 0) { n += (size_t)r; continue; }
        if (r < 0) break;
        if (time(0) >= dl) break;
    }
    if (!n) { fprintf(stderr, "peek: empty response %s\n", host); goto out; }
    buf[n] = 0;
    char *hd = strstr(buf, "\r\n\r\n");
    if (!hd || memcmp(buf, "HTTP/", 5)) { fprintf(stderr, "peek: bad response\n"); goto out; }
    *code = atoi(buf + 9);
    if (head) {
        char *nl = memchr(buf, '\n', (size_t)(hd - buf));
        char *hs = nl && nl < hd ? nl + 1 : hd;
        *head = sdup(hs, (size_t)(hd - hs));
    }
    long clen = -1;
    int chunked = 0, enc = 0;
    char *loc = 0;
    char *l = strstr(buf, "\r\n");
    if (l) l += 2;
    while (l && l < hd) {
        char *e = memchr(l, '\n', (size_t)(hd - l));
        if (!e) break;
        char *le = e[-1] == '\r' ? e - 1 : e;
        char *c = memchr(l, ':', (size_t)(le - l));
        if (c) {
            *c = 0;
            for (char *q = l; q < c; q++) *q = (char)tolower((unsigned char)*q);
            char *v = c + 1;
            while (v < le && *v == ' ') v++;
            if (!strcmp(l, "content-length")) clen = atol(v);
            else if (!strcmp(l, "transfer-encoding") &&
                     (size_t)(le - v) >= 7 && !pk_strncasecmp(v, "chunked", 7)) chunked = 1;
            else if (!strcmp(l, "location") && v < le) { *le = 0; loc = v; }
            else if (!strcmp(l, "content-encoding") &&
                     (size_t)(le - v) >= 8 && pk_strncasecmp(v, "identity", 8)) enc = 1;
        }
        l = e + 1;
    }
    if (loc && *code >= 300 && *code < 400) {
        snprintf(nurl, nsz, "%s", loc);
        goto out;
    }
    if (*code < 200 || *code >= 300) { fprintf(stderr, "peek: HTTP %d\n", *code); goto out; }
    if (enc) { fprintf(stderr, "peek: compressed body\n"); goto out; }
    char *bs = hd + 4;
    size_t bn = n - (size_t)(bs - buf);
    if (chunked) bn = dechunk(bs, bn);
    else if (clen >= 0 && (size_t)clen < bn) bn = (size_t)clen;
    memmove(buf, bs, bn);
    buf[bn] = 0;
    *len = bn;
    res = buf;
    buf = 0;
out:
    tls_done(&T);
    pk_socket_close(fd);
    free(req);
    free(buf);
    return res;
}

static char *hdr_end(char *b, size_t n) {
    for (size_t i = 0; i + 3 < n; i++)
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n')
            return b + i;
    return 0;
}

NetStream *http_open(const char *method, const char *url, const char *hdrs,
                     char **head_out, int *code_out) {
    if (head_out) *head_out = 0;
    if (code_out) *code_out = 0;
    char ub[UBSZ];
    snprintf(ub, sizeof ub, "%s", url);
    char host[300], hh[308], path[UBSZ];
    int port, tls;
    if (!uparse(ub, host, hh, &port, path, &tls)) return 0;
    NetStream *s = calloc(1, sizeof *s);
    if (!s) oom();
    s->t.fd = conn_tcp(host, port, 1, NS_TMO);
    if (s->t.fd == PK_INVALID_SOCKET) { free(s); return 0; }
    s->t.nb = 1;
    if (tls && !tls_up(&s->t, host)) { ns_close(s); return 0; }
    int rl = 0;
    char *req = req_build(method, path, hh, 0, 0, hdrs, &rl);
    long dl = clock_ms() + 15000;
    size_t off = 0;
    while (off < (size_t)rl) {
        long w = io_send(&s->t, req + off, (size_t)rl - off);
        if (w > 0) { off += (size_t)w; continue; }
        long left = dl - clock_ms();
        if (w < 0 || left <= 0) { free(req); ns_close(s); return 0; }
        pk_socket_wait(s->t.fd, PK_POLL_IN | PK_POLL_OUT, left > 200 ? 200 : (int)left);
    }
    free(req);
    size_t cap = 8192, n = 0;
    char *buf = malloc(cap);
    if (!buf) oom();
    char *hd = 0;
    for (;;) {
        if (n + 4097 > cap) {
            cap <<= 1;
            char *nb = realloc(buf, cap);
            if (!nb) oom();
            buf = nb;
        }
        long r = io_read(&s->t, buf + n, 4096);
        if (r > 0) {
            n += (size_t)r;
            buf[n] = 0;
            if ((hd = hdr_end(buf, n))) break;
            continue;
        }
        long left = dl - clock_ms();
        if (r < 0 || left <= 0) break;
        pk_socket_wait(s->t.fd, PK_POLL_IN, left > 200 ? 200 : (int)left);
    }
    if (!hd || memcmp(buf, "HTTP/", 5)) { free(buf); ns_close(s); return 0; }
    int code = atoi(buf + 9);
    if (code_out) *code_out = code;
    if (head_out) {
        char *nl = memchr(buf, '\n', (size_t)(hd - buf));
        char *hs = nl && nl < hd ? nl + 1 : hd;
        *head_out = sdup(hs, (size_t)(hd - hs));
    }
    size_t bn = n - (size_t)(hd + 4 - buf);
    s->pb = malloc(bn + 1);
    if (!s->pb) oom();
    memcpy(s->pb, hd + 4, bn);
    s->pb[bn] = 0;
    s->pn = bn;
    s->poff = 0;
    free(buf);
    return s;
}

char *http_do(HttpReq *r) {
    size_t blen = r->body ? (r->blen ? r->blen : strlen(r->body)) : 0;
    char ub[UBSZ];
    snprintf(ub, sizeof ub, "%s", r->url);
    for (int hop = 0; hop < 6; hop++) {
        char host[300], hh[308], path[UBSZ], nu[UBSZ];
        int port, tls, code = 0;
        size_t len = 0;
        char *head = 0;
        if (!uparse(ub, host, hh, &port, path, &tls)) {
            fprintf(stderr, "peek: bad url %s\n", ub);
            return 0;
        }
        char *b = fetch1(host, hh, port, path, tls, r->method, r->body, blen, r->hdrs,
                         nu, sizeof nu, &code, &len, &head);
        if (r->code) *r->code = code;
        if (b) {
            if (r->len) *r->len = len;
            if (r->head) *r->head = head;
            else free(head);
            snprintf((char *)r->url, UBSZ, "%s", ub);
            return b;
        }
        free(head);
        if (code >= 300 && code < 400 && *nu) {
            char *j = url_join(ub, nu);
            snprintf(ub, sizeof ub, "%s", j);
            free(j);
            continue;
        }
        return 0;
    }
    fprintf(stderr, "peek: too many redirects\n");
    return 0;
}

char *http_get(char *url, size_t *len) {
    int code;
    HttpReq r = {.method = "GET", .url = url, .len = len, .code = &code};
    return http_do(&r);
}

char *http_req(const char *method, const char *url, const char *body, size_t *len, int *code_out) {
    HttpReq r = {.method = method, .url = (char *)url, .body = body, .len = len,
                 .code = code_out};
    return http_do(&r);
}
