#include "peek.h"
#include "box.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>

#define CELL_W 8
#define CELL_H 16
#define FONT_DEFAULT 16

static int V_COLS = 80, V_ROWS = 24;

static int env_int(const char *k, int dflt) {
    const char *s = getenv(k);
    if (!s || !*s) return dflt;
    int v = atoi(s);
    return v > 0 ? v : dflt;
}

void vx_sync(void) {
    struct winsize ws;
    int cols = 0, rows = 0;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0) { cols = ws.ws_col; rows = ws.ws_row; }
    if (cols <= 0 && ioctl(0, TIOCGWINSZ, &ws) == 0) { cols = ws.ws_col; rows = ws.ws_row; }
    V_COLS = env_int("PEEK_COLS", cols > 0 ? cols : 80);
    V_ROWS = env_int("PEEK_ROWS", rows > 0 ? rows : 24);
}

void vx_init(void) { vx_sync(); }

int vx_cols(void) { return V_COLS; }
int vx_rows(void) { return V_ROWS; }
int vx_width(void) { return V_COLS * CELL_W; }
int vx_height(void) { return V_ROWS * CELL_H; }
int vx_cell_w(void) { return CELL_W; }
int vx_cell_h(void) { return CELL_H; }
int vx_col(int px) { return px / CELL_W; }
int vx_row(int px) { return px / CELL_H; }
int vx_font_default(void) { return FONT_DEFAULT; }

int vx_chlen(const char *s, int i, int n) {
    if (i >= n) return 0;
    unsigned char c = (unsigned char)s[i];
    int l = 1;
    if (c >= 0xF0) l = 4;
    else if (c >= 0xE0) l = 3;
    else if (c >= 0xC0) l = 2;
    if (i + l > n) return 1;
    for (int k = 1; k < l; k++)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 1;
    return l;
}

static uint32_t utf8_dec(const char *s, int l) {
    unsigned char c = (unsigned char)s[0];
    if (l == 1) return c;
    uint32_t cp = c & (uint32_t)(0x7F >> l);
    for (int k = 1; k < l; k++) cp = (cp << 6) | ((unsigned char)s[k] & 0x3F);
    return cp;
}

static int cp_cols(uint32_t c) {
    if (c == 0) return 0;
    if (c < 0x20 || (c >= 0x7F && c < 0xA0)) return 0;
    if (c < 0x1100) return 1;
    if (c >= 0x0300 && c <= 0x036F) return 0;
    if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0xFEFF) return 0;
    if (c >= 0x1100 && c <= 0x115F) return 2;
    if (c >= 0x2E80 && c <= 0x303E) return 2;
    if (c >= 0x3041 && c <= 0x33FF) return 2;
    if (c >= 0x3400 && c <= 0x4DBF) return 2;
    if (c >= 0x4E00 && c <= 0x9FFF) return 2;
    if (c >= 0xA000 && c <= 0xA4CF) return 2;
    if (c >= 0xAC00 && c <= 0xD7A3) return 2;
    if (c >= 0xF900 && c <= 0xFAFF) return 2;
    if (c >= 0xFE30 && c <= 0xFE6F) return 2;
    if (c >= 0xFF00 && c <= 0xFF60) return 2;
    if (c >= 0xFFE0 && c <= 0xFFE6) return 2;
    if (c >= 0x1F300 && c <= 0x1FAFF) return 2;
    if (c >= 0x20000 && c <= 0x3FFFD) return 2;
    return 1;
}

int vx_chwidth(const char *s, int i, int n) { return vx_chlen(s, i, n); }

int vx_chcols(const char *s, int i, int n) {
    int l = vx_chlen(s, i, n);
    if (!l) return 0;
    return cp_cols(utf8_dec(s + i, l));
}

int vx_text_px(const char *s, int n, int font) {
    (void)font;
    int w = 0;
    for (int i = 0; i < n; ) {
        int l = vx_chlen(s, i, n);
        if (!l) break;
        w += cp_cols(utf8_dec(s + i, l)) * CELL_W;
        i += l;
    }
    return w;
}

static int unit_of(const char *s, const char *e) {
    int n = (int)(e - s);
    if (n == 1) {
        if (*s == '%') return U_PCT;
    }
    if (n == 2) {
        if (s[0] == 'p' && s[1] == 'x') return U_PX;
        if (s[0] == 'p' && s[1] == 't') return U_PT;
        if (s[0] == 'p' && s[1] == 'c') return U_PC;
        if (s[0] == 'c' && s[1] == 'm') return U_CM;
        if (s[0] == 'm' && s[1] == 'm') return U_MM;
        if (s[0] == 'i' && s[1] == 'n') return U_IN;
        if (s[0] == 'e' && s[1] == 'm') return U_EM;
        if (s[0] == 'e' && s[1] == 'x') return U_EX;
        if (s[0] == 'c' && s[1] == 'h') return U_CH;
        if (s[0] == 'v' && s[1] == 'w') return U_VW;
        if (s[0] == 'v' && s[1] == 'h') return U_VH;
    }
    if (n == 3 && s[0] == 'r' && s[1] == 'e' && s[2] == 'm') return U_REM;
    return -1;
}

int len_parse(const char *s, const char *e, Len *out) {
    if (!s || s >= e) return 0;
    if (e - s == 4 && !strncasecmp(s, "auto", 4)) { *out = LEN_AUTO; return 1; }
    if (e - s == 4 && !strncasecmp(s, "none", 4)) { *out = LEN_AUTO; return 1; }
    if (e - s == 6 && !strncasecmp(s, "normal", 6)) { *out = LEN_AUTO; return 1; }

    int neg = 0;
    const char *p = s;
    while (p < e && ISWS(*p)) p++;
    if (p < e && (*p == '+' || *p == '-')) { neg = (*p == '-'); p++; }

    long ip = 0;
    int digits = 0;
    while (p < e && *p >= '0' && *p <= '9') { ip = ip * 10 + (*p++ - '0'); digits = 1; }
    long fp = 0, scale = 1;
    if (p < e && *p == '.') {
        p++;
        while (p < e && *p >= '0' && *p <= '9') {
            if (scale < 1000000) { fp = fp * 10 + (*p - '0'); scale *= 10; }
            p++;
            digits = 1;
        }
    }
    if (!digits) return 0;

    const char *ue = e;
    while (ue > p && ISWS(ue[-1])) ue--;
    int u = unit_of(p, ue);
    if (u < 0) {
        if (p != ue) return 0;
        u = U_PX;
    }
    long long q = (long long)ip * 64 + (long long)fp * 64 / scale;
    if (neg) q = -q;
    out->v = (int)q;
    out->u = (uint8_t)u;
    return 1;
}

Len len_read(const char *s) {
    Len l = LEN_AUTO;
    if (!s) return l;
    len_parse(s, s + strlen(s), &l);
    return l;
}

int len_is_auto(Len l) { return l.u == U_AUTO; }

int len_px(Len l, int base, int font) {
    long long v = l.v;
    switch (l.u) {
        case U_AUTO: return 0;
        case U_PX:   return (int)(v / 64);
        case U_PCT:  return (int)((long long)base * v / 6400);
        case U_EM:   return (int)((long long)font * v / 6400);
        case U_REM:  return (int)((long long)FONT_DEFAULT * v / 6400);
        case U_EX:   return (int)((long long)(font / 2) * v / 6400);
        case U_CH:   return (int)((long long)CELL_W * v / 6400);
        case U_PT:   return (int)(v * 96 / (72 * 64));
        case U_PC:   return (int)(v * 96 / (6 * 64) / 100);
        case U_CM:   return (int)(v * 96 / (int)(2.54 * 64));
        case U_MM:   return (int)(v * 96 / (int)(25.4 * 64));
        case U_IN:   return (int)(v * 96 / 64);
        case U_VW:   return (int)((long long)vx_width() * v / 6400);
        case U_VH:   return (int)((long long)vx_height() * v / 6400);
    }
    return 0;
}

Canvas *cv_new(int w, int h) {
    Canvas *c = calloc(1, sizeof *c);
    if (!c) oom();
    c->w = w; c->h = h;
    c->p = malloc((size_t)(w * h) * sizeof(Cell));
    if (!c->p) oom();
    for (int i = 0; i < w * h; i++) {
        c->p[i].ch = ' ';
        c->p[i].fg = (uint8_t)-1;
        c->p[i].bg = (uint8_t)-1;
    }
    return c;
}

void cv_free(Canvas *c) {
    if (!c) return;
    free(c->p);
    free(c);
}

void cv_clear(Canvas *c, int bg) {
    for (int i = 0; i < c->w * c->h; i++) {
        c->p[i].ch = ' ';
        c->p[i].fg = (uint8_t)-1;
        c->p[i].bg = (uint8_t)bg;
        c->p[i].fl = 0;
        c->p[i].cont = 0;
    }
}

static void cv_putc(Canvas *c, int x, int y, uint32_t cp, int fg, int bg, int fl) {
    if (y < 0 || y >= c->h) return;
    if (x < 0 || x >= c->w) return;
    Cell *cell = c->p + y * c->w + x;
    cell->ch = cp;
    cell->fg = (uint8_t)fg;
    cell->bg = (uint8_t)bg;
    cell->fl = (uint8_t)fl;
    cell->cont = 0;
}

static void cv_fill(Canvas *c, int x, int y, int w, int h, int bg) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= c->h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= c->w) continue;
            Cell *cell = c->p + yy * c->w + xx;

            cell->bg = (uint8_t)bg;
        }
    }
}

static void be_rect(void *u, int x, int y, int w, int h, int color) {
    Canvas *c = ((TermCtx *)u)->cv;
    int x0 = vx_col(x), y0 = vx_row(y);
    int x1 = vx_col(x + w - 1) + 1, y1 = vx_row(y + h - 1) + 1;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    cv_fill(c, x0, y0, x1 - x0, y1 - y0, color);
}

static void be_text(void *u, int x, int y, const char *s, int len, int color, int fl) {
    Canvas *c = ((TermCtx *)u)->cv;
    int cx = vx_col(x), cy = vx_row(y);
    int skip = -1;
    if (x < 0) {
        int cutp = 0;
        while (cutp < len && vx_text_px(s, cutp, 0) < -x) cutp += vx_chlen(s, cutp, len);
        s += cutp; len -= cutp;
    } else if (x % CELL_W) {
        cx += 1;
    }
    for (int i = 0; i < len; ) {
        int l = vx_chlen(s, i, len);
        if (!l) break;
        uint32_t cp = utf8_dec(s + i, l);
        int cw = cp_cols(cp);
        if (cw == 0) { i += l; continue; }
        if (cx >= 0 && skip != cx) {
            cv_putc(c, cx, cy, cp, color, -1, fl);
            if (cw == 2 && cx + 1 < c->w) {
                Cell *n = c->p + cy * c->w + cx + 1;
                n->ch = 0; n->cont = 1;
                n->fg = (uint8_t)color; n->bg = (uint8_t)-1; n->fl = (uint8_t)fl;
            }
        }
        cx += cw;
        i += l;
    }
}

static void be_hline(void *u, int x, int y, int w, int color, int style) {
    Canvas *c = ((TermCtx *)u)->cv;
    if (w <= 0) return;
    int cx = vx_col(x), cy = vx_row(y);
    int n = vx_col(x + w - 1) - cx + 1;
    for (int i = 0; i < n; i++) {
        uint32_t ch = ' ';
        if (style == BS_SOLID) ch = 0x2500;
        else if (style == BS_DASHED) ch = 0x2504;
        else if (style == BS_DOTTED) ch = 0x2508;
        cv_putc(c, cx + i, cy, ch, color, -1, 0);
    }
}

static void be_vline(void *u, int x, int y, int h, int color, int style) {
    Canvas *c = ((TermCtx *)u)->cv;
    if (h <= 0) return;
    int cx = vx_col(x), cy = vx_row(y);
    int n = vx_row(y + h - 1) - cy + 1;
    for (int i = 0; i < n; i++) {
        uint32_t ch = ' ';
        if (style == BS_SOLID) ch = 0x2502;
        else if (style == BS_DASHED) ch = 0x2506;
        else if (style == BS_DOTTED) ch = 0x250A;
        cv_putc(c, cx, cy + i, ch, color, -1, 0);
    }
}

static const PaintBackend TERM_BE = { be_rect, be_text, be_hline, be_vline };
const PaintBackend *term_backend(void) { return &TERM_BE; }

static int utf8_enc(char *w, uint32_t cp) {
    if (cp < 0x80) { w[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        w[0] = (char)(0xC0 | (cp >> 6));
        w[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        w[0] = (char)(0xE0 | (cp >> 12));
        w[1] = (char)(0x80 | ((cp >> 6) & 63));
        w[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    w[0] = (char)(0xF0 | (cp >> 18));
    w[1] = (char)(0x80 | ((cp >> 12) & 63));
    w[2] = (char)(0x80 | ((cp >> 6) & 63));
    w[3] = (char)(0x80 | (cp & 63));
    return 4;
}

static void sgr(char *out, size_t *w, int fg, int bg, int fl) {
    *w += (size_t)sprintf(out + *w, "\x1b[0m");
    if (fg >= 0) *w += (size_t)sprintf(out + *w, "\x1b[38;5;%dm", fg);
    if (bg >= 0) *w += (size_t)sprintf(out + *w, "\x1b[48;5;%dm", bg);
    if (fl & FL_BOLD) *w += (size_t)sprintf(out + *w, "\x1b[1m");
    if (fl & FL_ITAL) *w += (size_t)sprintf(out + *w, "\x1b[3m");
    if (fl & FL_UL) *w += (size_t)sprintf(out + *w, "\x1b[4m");
    if (fl & FL_STRIKE) *w += (size_t)sprintf(out + *w, "\x1b[9m");
    if (fl & FL_REV) *w += (size_t)sprintf(out + *w, "\x1b[7m");
}

static int cell_blank(const Cell *c) {
    return c->ch == ' ' && c->fg == (uint8_t)-1 && c->bg == (uint8_t)-1 && !c->fl;
}

void term_flush(Canvas *cv) {
    size_t cap = (size_t)cv->w * 48 + 256;
    char *line = malloc(cap);
    if (!line) oom();

    int first_row = -1, last_row = -1;
    for (int y = 0; y < cv->h; y++) {
        int any = 0;
        for (int x = 0; x < cv->w; x++)
            if (!cell_blank(cv->p + y * cv->w + x)) { any = 1; break; }
        if (any) {
            if (first_row < 0) first_row = y;
            last_row = y;
        }
    }
    if (last_row < 0) { free(line); return; }

    int cur_fg = -1, cur_bg = -1, cur_fl = 0;
    for (int y = first_row; y <= last_row; y++) {
        int end = cv->w;
        while (end > 0 && cell_blank(cv->p + y * cv->w + end - 1)) end--;
        if (end == 0) { putchar('\n'); continue; }

        size_t w = 0;
        for (int x = 0; x < end; x++) {
            Cell *c = cv->p + y * cv->w + x;
            if (c->cont) continue;
            if (c->fg != cur_fg || c->bg != cur_bg || c->fl != cur_fl) {
                sgr(line, &w, c->fg == (uint8_t)-1 ? -1 : c->fg,
                            c->bg == (uint8_t)-1 ? -1 : c->bg, c->fl);
                cur_fg = c->fg;
                cur_bg = c->bg;
                cur_fl = c->fl;
            }
            w += (size_t)utf8_enc(line + w, c->ch ? c->ch : ' ');
        }
        if (cur_fg != -1 || cur_bg != -1 || cur_fl != 0) {
            w += (size_t)sprintf(line + w, "\x1b[0m");
            cur_fg = -1;
            cur_bg = -1;
            cur_fl = 0;
        }
        line[w] = 0;
        fputs(line, stdout);
        putchar('\n');
    }
    free(line);
}

void term_frame(Canvas *cv) {
    fputs("\x1b[2J\x1b[H", stdout);
    term_flush(cv);
}

static void hrule(int cx, int w) {
    printf("%*s+", cx, "");
    for (int i = 0; i < w; i++) putchar('-');
    fputs("+\n", stdout);
}

void draw_dialog(const char *m) {
    int ml = (int)strlen(m);
    int total = ml + 2;
    int cx = 0;
    if (total < V_COLS) cx = (V_COLS - total - 2) / 2;
    int w = total > V_COLS - 4 ? V_COLS - 4 : total;
    int shown = ml < w ? ml : w;
    int left = (w - shown) / 2;
    int right = w - shown - left;
    hrule(cx, w);
    printf("%*s|%*s%.*s%*s|\n", cx, "", left, "", shown, m, right, "");
    hrule(cx, w);
}
