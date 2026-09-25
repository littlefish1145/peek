#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif
#include "peek.h"
#include "box.h"
#include "platform.h"
#ifdef __cplusplus
}
#endif
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RECT r;
    Node *n;
} HitLink;

typedef struct {
    ID2D1Factory *factory;
    ID2D1DCRenderTarget *target;
    IDWriteFactory *write;
    IDWriteTextFormat *normal;
    IDWriteTextFormat *bold;
    ID2D1SolidColorBrush *brush;
    HWND hwnd;
    HWND address;
    HWND back;
    HitLink *links;
    int nlink;
    int caplink;
    int scroll;
    int page_height;
    int ready;
} Gui;

static char *current_url;
static char *history[32];
static int history_count;
static int log_seen;
static int alert_seen;

static int style_value(Node *n, const Prop *p) {
    if (!n) return 0;
    for (int i = 0; i < n->nst; i++)
        if (n->nst && n->st[i].p == p) return i + 1;
    return 0;
}

static const char *style_text(Node *n, const Prop *p) {
    int i = style_value(n, p);
    return i ? n->st[i - 1].v : 0;
}

static int parse_size(const char *s, int base) {
    if (!s) return 0;
    char *end;
    double value = strtod(s, &end);
    while (*end == ' ') end++;
    if (!strcmp(end, "%")) return (int)(base * value / 100.0);
    if (!strcmp(end, "em") || !strcmp(end, "rem")) return (int)(base * value);
    if (!strcmp(end, "pt")) return (int)(value * 96.0 / 72.0);
    if (!strcmp(end, "pc")) return (int)(value * 96.0 / 6.0);
    if (!strcmp(end, "in")) return (int)(value * 96.0);
    if (!strcmp(end, "cm")) return (int)(value * 96.0 / 2.54);
    if (!strcmp(end, "mm")) return (int)(value * 96.0 / 25.4);
    return (int)value;
}

static void parse_spacing(const char *s, int base, int out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!s) return;
    const char *p = s;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return;
    out[0] = (int)v;
    int n = 1;
    while (*end && n < 4) {
        while (*end && (*end == ' ' || *end == '\t')) end++;
        if (!*end) break;
        v = strtol(end, &end, 10);
        out[n++] = (int)v;
    }
    if (n == 2) out[2] = out[0], out[3] = out[1];
    else if (n == 3) out[3] = out[1];
    for (int i = 0; i < 4; i++) out[i] = (int)((double)out[i] * base / 16.0);
}

static unsigned hex_digit(int c) {
    if (c >= '0' && c <= '9') return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (unsigned)(c - 'A' + 10);
    return 0;
}

static int parse_color(const char *s, D2D1_COLOR_F *out) {
    out->r = out->g = out->b = 0;
    out->a = 1;
    if (!s) return 0;
    while (*s == ' ') s++;
    if (*s == '#') {
        unsigned v = 0;
        int n = 0;
        for (s++; *s && *s != ' '; s++, n++) {
            unsigned d = hex_digit((unsigned char)*s);
            if (d > 15) return 0;
            v = v * 16 + d;
        }
        if (n == 3) {
            out->r = (v >> 8 & 15) / 15.0f;
            out->g = (v >> 4 & 15) / 15.0f;
            out->b = (v & 15) / 15.0f;
            return 1;
        }
        if (n == 6) {
            out->r = (v >> 16 & 255) / 255.0f;
            out->g = (v >> 8 & 255) / 255.0f;
            out->b = (v & 255) / 255.0f;
            return 1;
        }
        return 0;
    }
    if (!strncmp(s, "rgb", 3)) {
        const char *p = strchr(s, '(');
        if (!p) return 0;
        int r = 0, g = 0, b = 0;
        if (sscanf(p + 1, "%d , %d , %d", &r, &g, &b) != 3 &&
            sscanf(p + 1, "%d %d %d", &r, &g, &b) != 3) return 0;
        out->r = (float)(r < 0 ? 0 : r > 255 ? 255 : r) / 255.0f;
        out->g = (float)(g < 0 ? 0 : g > 255 ? 255 : g) / 255.0f;
        out->b = (float)(b < 0 ? 0 : b > 255 ? 255 : b) / 255.0f;
        return 1;
    }
    if (!strcmp(s, "black")) { out->r = out->g = out->b = 0; return 1; }
    if (!strcmp(s, "white")) { out->r = out->g = out->b = 1; return 1; }
    if (!strcmp(s, "red")) { out->r = 1; return 1; }
    if (!strcmp(s, "green")) { out->g = 1; return 1; }
    if (!strcmp(s, "blue")) { out->b = 1; return 1; }
    if (!strcmp(s, "gray") || !strcmp(s, "grey")) { out->r = out->g = out->b = .5f; return 1; }
    return 0;
}

static int utf8_to_wide(const char *s, int n, WCHAR **out) {
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, 0, 0);
    if (len <= 0) len = MultiByteToWideChar(CP_UTF8, 0, s, n, 0, 0);
    if (len <= 0) return 0;
    WCHAR *w = (WCHAR *)malloc((size_t)(len + 1) * sizeof *w);
    if (!w) oom();
    if (!MultiByteToWideChar(CP_UTF8, 0, s, n, w, len)) {
        free(w);
        return 0;
    }
    w[len] = 0;
    *out = w;
    return len;
}

static int wide_to_utf8(const WCHAR *w, char *out, int cap) {
    if (!w || cap <= 0) return 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, 0, 0);
    return n > 0 ? n - 1 : 0;
}

static void gui_events(Gui *g) {
    while (log_seen < NLOG) fprintf(stderr, "js: %s\n", LOGS[log_seen++]);
    while (alert_seen < NAL) {
        const char *text = ALERTS[alert_seen++];
        WCHAR *message = 0;
        if (utf8_to_wide(text, (int)strlen(text), &message))
            MessageBoxW(g->hwnd, message, L"peek", MB_OK | MB_ICONINFORMATION);
        free(message);
    }
}

static float node_font_size(Node *n) {
    int size = parse_size(style_text(n, &P_FSIZE), 16);
    return size > 0 ? (float)size : 16.0f;
}

static int node_bold(Node *n) {
    const char *v = style_text(n, &P_WEIGHT);
    return v && (!strcmp(v, "bold") || atoi(v) >= 600);
}

static int hidden(Node *n) {
    const char *display = style_text(n, &P_DISPLAY);
    return (n->def->f & T_HIDDEN) || (display && !strcmp(display, "none"));
}

static void add_link(Gui *g, Node *n, float x, float y, float w, float h) {
    if (!n || !attr_get(n, "href")) return;
    if (g->nlink >= g->caplink) {
        g->caplink = g->caplink ? g->caplink * 2 : 32;
        HitLink *p = (HitLink *)realloc(g->links, (size_t)g->caplink * sizeof *p);
        if (!p) oom();
        g->links = p;
    }
    g->links[g->nlink].r.left = (LONG)x;
    g->links[g->nlink].r.top = (LONG)y;
    g->links[g->nlink].r.right = (LONG)(x + w);
    g->links[g->nlink].r.bottom = (LONG)(y + h);
    g->links[g->nlink].n = n;
    g->nlink++;
}

static void draw_rect(Gui *g, float x, float y, float w, float h, D2D1_COLOR_F c) {
    D2D1_RECT_F r = {x, y, x + w, y + h};
    g->brush->SetColor(c);
    g->target->FillRectangle(r, g->brush);
}

static void draw_text(Gui *g, Node *n, Node *link, const char *text, int len, float x, float y, float width) {
    if (!text || len <= 0 || width <= 0) return;
    int visible = 0;
    for (int i = 0; i < len; i++) if (!ISWS((unsigned char)text[i])) { visible = 1; break; }
    if (!visible) return;
    WCHAR *wide = 0;
    int wlen = utf8_to_wide(text, len, &wide);
    if (wlen <= 0) return;
    IDWriteTextFormat *format = node_bold(n) ? g->bold : g->normal;
    D2D1_COLOR_F color = {0, 0, 0, 1};
    parse_color(style_text(n, &P_COLOR), &color);
    float line_height = node_font_size(n) * 1.35f;
    int max_lines = len > 80 ? 4 : 2;
    D2D1_RECT_F bounds = {x, y, x + width, y + line_height * (float)max_lines};
    g->brush->SetColor(color);
    g->target->DrawText(wide, (UINT32)wlen, format, &bounds, g->brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    free(wide);
    add_link(g, link ? link : n, x, y, width, line_height * (float)max_lines);
    const char *deco = style_text(n, &P_DECO);
    if (deco && (strstr(deco, "underline") || strstr(deco, "line-through"))) {
        g->brush->SetColor(color);
        g->target->DrawLine(D2D1_POINT_2F{x, y + line_height},
            D2D1_POINT_2F{x + width, y + line_height}, g->brush, 1.0f);
    }
}

typedef struct {
    Gui *g;
    float x;
    float y;
    float width;
    Node *link;
} Flow;

static void draw_flow(Flow *f, Node *n, int block_context);

static float measure_node(Node *n) {
    if (!n || hidden(n)) return 0;
    if (n->def->f & T_TEXTN) return node_font_size(n) + 8;
    if (n->taglen == 2 && n->tagpk == K2('b', 'r')) return 20;
    int pad[4];
    parse_spacing(style_text(n, &P_PAD), 16, pad);
    float h = 0;
    for (int i = 0; i < n->nchild; i++) h += measure_node(n->child[i]);
    if (n->def->f & T_BLOCK) h += pad[0] + pad[1];
    return h;
}

static float draw_children(Flow *f, Node *n) {
    float start = f->y;
    for (int i = 0; i < n->nchild; i++) draw_flow(f, n->child[i], 0);
    return f->y - start;
}

static void draw_flow(Flow *f, Node *n, int block_context) {
    if (!n || hidden(n)) return;
    if (n->def->f & T_TEXTN) {
        int fs = (int)node_font_size(n);
        int h = fs + 8;
        if (n->text && n->tlen > 0) {
            draw_text(f->g, n, f->link, n->text, n->tlen, f->x, f->y, f->width);
            f->y += h * (n->tlen > 80 ? 2 : 1);
        }
        return;
    }
    if (n->taglen == 2 && n->tagpk == K2('b', 'r')) {
        f->y += 20;
        return;
    }
    int block = block_context || (n->def->f & T_BLOCK);
    Node *old_link = f->link;
    if (attr_get(n, "href")) f->link = n;
    if (!block) {
        draw_children(f, n);
        f->link = old_link;
        return;
    }
    int pad[4], margin[4];
    parse_spacing(style_text(n, &P_PAD), 16, pad);
    parse_spacing(style_text(n, &P_MARGIN), 16, margin);
    int border = 0;
    const char *bs = style_text(n, &P_BORDER);
    if (bs) border = parse_size(bs, 16);
    if (border < 0) border = 0;
    float x = f->x + margin[3];
    float y = f->y + margin[0];
    float w = f->width - margin[1] - margin[3];
    if (w < 20) w = 20;
    D2D1_COLOR_F color;
    float h = measure_node(n) + border * 2;
    if (parse_color(style_text(n, &P_BG), &color)) {
        draw_rect(f->g, x, y, w, h, color);
        if (border > 0) {
            D2D1_COLOR_F bc = {0, 0, 0, 1};
            parse_color(bs, &bc);
            f->g->brush->SetColor(bc);
            f->g->target->DrawRectangle(D2D1_RECT_F{x, y, x + w, y + h}, f->g->brush, (float)border);
        }
        Flow child = {f->g, x + pad[3] + border, y + pad[0] + border,
            w - pad[1] - pad[3] - border * 2, f->link};
        draw_children(&child, n);
        f->y = y + h + margin[2];
    } else {
        Flow child = {f->g, x + pad[3] + border, y + pad[0] + border,
            w - pad[1] - pad[3] - border * 2, f->link};
        float h = draw_children(&child, n) + pad[0] + pad[1] + border * 2;
        if (border > 0) {
            D2D1_COLOR_F bc = {0, 0, 0, 1};
            parse_color(bs, &bc);
            f->g->brush->SetColor(bc);
            f->g->target->DrawRectangle(D2D1_RECT_F{x, y, x + w, y + h}, f->g->brush, (float)border);
        }
        f->y = y + h + margin[2];
    }
    f->link = old_link;
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f && (path[0] == '/' || path[0] == '\\')) f = fopen(path + 1, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return 0; }
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) oom();
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    if (len) *len = got;
    return b;
}

static void set_base(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) { js_set_base("."); return; }
    if (slash == path) { js_set_base("/"); return; }
    char *base = sdup(path, (size_t)(slash - path));
    js_set_base(base);
    free(base);
}

static int load_page(const char *url) {
    char normalized[2048];
    snprintf(normalized, sizeof normalized, "%s", url);
    int remote = url_is(normalized);
    size_t len = 0;
    char *body = remote ? http_get(normalized, &len) : read_file(normalized, &len);
    if (!body) return 0;
    for (int i = 0; i < NLOG; i++) free(LOGS[i]);
    for (int i = 0; i < NAL; i++) free(ALERTS[i]);
    NLOG = NAL = 0;
    log_seen = alert_seen = 0;
    static int js_started;
    if (remote) js_set_base(normalized);
    else set_base(normalized);
    if (js_started) { js_done(); js_init(); }
    else { js_init(); js_started = 1; }
    css_reset();
    DOM = parse_html(body);
    run_scripts(DOM);
    js_pump();
    parse_css(css_collect(DOM, normalized));
    apply_styles(DOM);
    js_win_event("domcontentloaded");
    js_win_event("load");
    free(current_url);
    current_url = sdup(normalized, strlen(normalized));
    return 1;
}

static void navigate(const char *href) {
    if (!href || !*href || *href == '#') return;
    char *url;
    if (url_is(href)) url = sdup(href, strlen(href));
    else if (url_is(current_url)) url = url_join(current_url, href);
    else {
        const char *slash = strrchr(current_url, '/');
        size_t n = slash && slash > current_url ? (size_t)(slash - current_url) : 1;
        url = (char *)malloc(n + strlen(href) + 2);
        if (!url) oom();
        if (slash && slash > current_url) memcpy(url, current_url, n);
        else *url = '.';
        url[n] = '/';
        memcpy(url + n + 1, href, strlen(href) + 1);
    }
    if (history_count == 32) {
        free(history[0]);
        memmove(history, history + 1, 31 * sizeof *history);
        history_count = 31;
    }
    history[history_count++] = sdup(current_url, strlen(current_url));
    if (!load_page(url)) free(history[--history_count]);
    free(url);
}

typedef struct {
    Gui *g;
    int offset_y;
} D2DContext;

static D2D1_COLOR_F ansi_rgb(int color) {
    static const BYTE basic[16][3] = {
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},
        {0,128,128},{192,192,192},{128,128,128},{255,0,0},{0,255,0},{255,255,0},
        {0,0,255},{255,0,255},{0,255,255},{255,255,255}
    };
    D2D1_COLOR_F c = {0, 0, 0, 1};
    if (color < 0) return c;
    if (color < 16) {
        c.r = basic[color][0] / 255.0f;
        c.g = basic[color][1] / 255.0f;
        c.b = basic[color][2] / 255.0f;
        return c;
    }
    if (color < 232) {
        int n = color - 16;
        int r = n / 36, g = n / 6 % 6, b = n % 6;
        int rv = r ? 55 + r * 40 : 0;
        int gv = g ? 55 + g * 40 : 0;
        int bv = b ? 55 + b * 40 : 0;
        c.r = rv / 255.0f;
        c.g = gv / 255.0f;
        c.b = bv / 255.0f;
        return c;
    }
    int v = 8 + (color - 232) * 10;
    c.r = c.g = c.b = (float)v / 255.0f;
    return c;
}

static void d2d_rect(void *u, int x, int y, int w, int h, int color) {
    D2DContext *c = (D2DContext *)u;
    if (w <= 0 || h <= 0) return;
    c->g->brush->SetColor(ansi_rgb(color));
    D2D1_RECT_F r = {(float)x, (float)(y + c->offset_y), (float)(x + w), (float)(y + h + c->offset_y)};
    c->g->target->FillRectangle(r, c->g->brush);
}

static void d2d_text(void *u, int x, int y, const char *s, int len, int color, int fl) {
    D2DContext *c = (D2DContext *)u;
    if (!s || len <= 0) return;
    WCHAR *wide = 0;
    int n = utf8_to_wide(s, len, &wide);
    if (n <= 0) return;
    IDWriteTextFormat *format = fl & FL_BOLD ? c->g->bold : c->g->normal;
    c->g->brush->SetColor(ansi_rgb(color));
    D2D1_RECT_F r = {(float)x, (float)(y + c->offset_y), (float)(x + 10000), (float)(y + 24 + c->offset_y)};
    c->g->target->DrawText(wide, (UINT32)n, format, &r, c->g->brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    free(wide);
}

static void d2d_hline(void *u, int x, int y, int w, int color, int style) {
    D2DContext *c = (D2DContext *)u;
    if (w <= 0) return;
    c->g->brush->SetColor(ansi_rgb(color));
    D2D1_POINT_2F a = {(float)x, (float)(y + c->offset_y)};
    D2D1_POINT_2F b = {(float)(x + w), (float)(y + c->offset_y)};
    c->g->target->DrawLine(a, b, c->g->brush, style == BS_DOTTED ? 1.0f : 1.5f);
}

static void d2d_vline(void *u, int x, int y, int h, int color, int style) {
    D2DContext *c = (D2DContext *)u;
    if (h <= 0) return;
    c->g->brush->SetColor(ansi_rgb(color));
    D2D1_POINT_2F a = {(float)x, (float)(y + c->offset_y)};
    D2D1_POINT_2F b = {(float)x, (float)(y + h + c->offset_y)};
    c->g->target->DrawLine(a, b, c->g->brush, style == BS_DOTTED ? 1.0f : 1.5f);
}

static const PaintBackend D2D_BACKEND = {d2d_rect, d2d_text, d2d_hline, d2d_vline};

static void refresh(Gui *g) {
    if (!DOM) {
        g->target->BeginDraw();
        g->target->Clear(D2D1_COLOR_F{1, 1, 1, 1});
        g->target->EndDraw();
        return;
    }
    RECT rc;
    GetClientRect(g->hwnd, &rc);
    char cols[32], rows[32];
    snprintf(cols, sizeof cols, "%d", rc.right / 8 > 20 ? rc.right / 8 : 20);
    snprintf(rows, sizeof rows, "400");
    SetEnvironmentVariableA("PEEK_COLS", cols);
    SetEnvironmentVariableA("PEEK_ROWS", rows);
    vx_sync();
    js_viewport(vx_width(), vx_height());
    apply_styles(DOM);
    peek_layout();
    D2DContext context = {g, 48 - g->scroll};
    g->target->BeginDraw();
    g->target->Clear(D2D1_COLOR_F{1, 1, 1, 1});
    if (peek_root()) paint_tree(peek_root(), &D2D_BACKEND, &context);
    g->page_height = peek_root() ? peek_root()->h + 72 : 0;
    g->target->EndDraw();
}

static int init_d2d(Gui *g) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g->factory);
    if (FAILED(hr)) return 0;
    D2D1_RENDER_TARGET_PROPERTIES p = {};
    p.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    p.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    p.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    p.dpiX = 96.0f;
    p.dpiY = 96.0f;
    hr = g->factory->CreateDCRenderTarget(&p, &g->target);
    if (FAILED(hr)) return 0;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&g->write);
    if (FAILED(hr)) return 0;
    g->write->CreateTextFormat(L"Segoe UI", 0, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-cn", &g->normal);
    g->write->CreateTextFormat(L"Segoe UI", 0, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-cn", &g->bold);
    g->normal->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    g->bold->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    D2D1_COLOR_F black = {0, 0, 0, 1};
    g->target->CreateSolidColorBrush(black, &g->brush);
    return 1;
}

static void release_d2d(Gui *g) {
    if (g->brush) g->brush->Release();
    if (g->bold) g->bold->Release();
    if (g->normal) g->normal->Release();
    if (g->write) g->write->Release();
    if (g->target) g->target->Release();
    if (g->factory) g->factory->Release();
    free(g->links);
}

static void resize_targets(Gui *g) {
    RECT rc;
    GetClientRect(g->hwnd, &rc);
    int top = 42;
    if (g->back) MoveWindow(g->back, 8, 8, 52, 26, TRUE);
    if (g->address) MoveWindow(g->address, 68, 8, rc.right > 320 ? rc.right - 80 : 220, 26, TRUE);
    (void)top;
}

static void do_back(Gui *g) {
    if (!history_count) return;
    char *url = history[--history_count];
    load_page(url);
    free(url);
    InvalidateRect(g->hwnd, 0, FALSE);
}

static void address_enter(Gui *g) {
    WCHAR *wide = (WCHAR *)malloc(4096 * sizeof *wide);
    if (!wide) oom();
    GetWindowTextW(g->address, wide, 4096);
    char utf8[4096];
    int n = wide_to_utf8(wide, utf8, sizeof utf8);
    free(wide);
    if (n <= 0) return;
    char *url = utf8;
    if (!strstr(url, "://") && GetFileAttributesA(url) == INVALID_FILE_ATTRIBUTES) {
        static char full[4200];
        snprintf(full, sizeof full, "http://%s", url);
        url = full;
    }
    navigate(url);
    InvalidateRect(g->hwnd, 0, FALSE);
}

static void paint(HWND hwnd, Gui *g) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (g->target) {
        RECT bounds = {0, 0, rc.right, rc.bottom};
        g->target->BindDC(dc, &bounds);
        refresh(g);
    }
    EndPaint(hwnd, &ps);
    (void)dc;
}

static Node *hit_box(Box *b, int x, int y) {
    if (!b || b->hide) return 0;
    if (b->n && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
        if (b->n->taglen == 1 && b->n->tagpk == K1('a')) return b->n;
        if (b->n->taglen == 6 && b->n->tagpk == K6('b', 'u', 't', 't', 'o', 'n')) return b->n;
        if (b->n->taglen == 5 && b->n->tagpk == K5('i', 'n', 'p', 'u', 't')) return b->n;
    }
    for (Box *c = b->first; c; c = c->next) {
        Node *n = hit_box(c, x, y);
        if (n) return n;
    }
    return 0;
}

static void hit_test(Gui *g, int x, int y) {
    int doc_y = y - 48 + g->scroll;
    Node *n = hit_box(peek_root(), x, doc_y);
    if (!n) return;
    if (n->taglen == 1 && n->tagpk == K1('a')) {
        const char *href = attr_get(n, "href");
        if (href) navigate(href);
    } else {
        int index = oc_find(n);
        if (index >= 0) js_click(index);
    }
    InvalidateRect(g->hwnd, 0, FALSE);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Gui *g = (Gui *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        g = (Gui *)calloc(1, sizeof *g);
        if (!g) return -1;
        g->hwnd = hwnd;
        if (!init_d2d(g)) { release_d2d(g); free(g); return -1; }
        g->back = CreateWindowExW(0, L"BUTTON", L"Back", WS_CHILD | WS_VISIBLE, 8, 8, 52, 26, hwnd, 0, 0, 0);
        g->address = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 68, 8, 600, 26, hwnd, 0, 0, 0);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)g);
        resize_targets(g);
        SetTimer(hwnd, 1, 16, 0);
        return 0;
    }
    case WM_SIZE:
        resize_targets(g);
        InvalidateRect(hwnd, 0, FALSE);
        return 0;
    case WM_PAINT:
        paint(hwnd, g);
        return 0;
    case WM_TIMER:
        if (g) {
            js_pump();
            gui_events(g);
            InvalidateRect(hwnd, 0, FALSE);
        }
        return 0;
    case WM_COMMAND:
        if (g && LOWORD(wp) == 1) { do_back(g); InvalidateRect(hwnd, 0, FALSE); return 0; }
        return 0;
    case WM_KEYDOWN:
        if (g && wp == VK_RETURN && GetFocus() == g->address) { address_enter(g); return 0; }
        break;
    case WM_LBUTTONUP:
        if (g) { int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp); if (y > 42) hit_test(g, x, y); }
        return 0;
    case WM_MOUSEWHEEL:
        if (g) {
            g->scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 48;
            if (g->scroll < 0) g->scroll = 0;
            InvalidateRect(hwnd, 0, FALSE);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g) {
            KillTimer(hwnd, 1);
            if (DOM) js_done();
            release_d2d(g);
            free(g);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        free(current_url);
        for (int i = 0; i < history_count; i++) free(history[i]);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.html|url>\n", argv[0]);
        return 1;
    }
    WNDCLASSEXW wc = {sizeof wc};
    wc.hInstance = GetModuleHandleW(0);
    wc.lpfnWndProc = window_proc;
    wc.lpszClassName = L"PeekGui";
    wc.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    if (!RegisterClassExW(&wc)) return 1;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"peek", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800, 0, 0, wc.hInstance, 0);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    Gui *g = (Gui *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    WCHAR *initial = NULL;
    int wlen = utf8_to_wide(argv[1], (int)strlen(argv[1]), &initial);
    if (wlen > 0) SetWindowTextW(g->address, initial);
    free(initial);
    SetFocus(g->address);
    if (!load_page(argv[1])) {
        MessageBoxW(hwnd, L"页面加载失败", L"peek", MB_OK | MB_ICONERROR);
    } else {
        gui_events(g);
        InvalidateRect(hwnd, 0, FALSE);
    }
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
