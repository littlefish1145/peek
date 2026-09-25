#include "peek.h"
#include "box.h"

static const char *sv(Node *n, const Prop *p) {
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p == p) return n->st[i].v;
    return 0;
}

static int kw(const char *v, const char *a) { return v && !strcmp(v, a); }
static int kwp(const char *v, const char *a) { return v && !pk_strncasecmp(v, a, strlen(a)); }

static int len_of(Node *n, const Prop *p, int base, int font, int dflt) {
    const char *v = sv(n, p);
    if (!v) return dflt;
    Len l;
    if (!len_parse(v, v + strlen(v), &l) || l.u == U_AUTO) return dflt;
    return len_px(l, base, font);
}

static int len_auto(Node *n, const Prop *p, int base, int font, int dflt, int *out_auto) {
    const char *v = sv(n, p);
    *out_auto = 0;
    if (!v) return dflt;
    Len l;
    if (!len_parse(v, v + strlen(v), &l)) return dflt;
    if (l.u == U_AUTO) { *out_auto = 1; return dflt; }
    return len_px(l, base, font);
}

static int split_vals(const char *v, Len out[4]) {
    int n = 0;
    const char *p = v;
    while (*p && n < 4) {
        while (ISWS(*p)) p++;
        if (!*p) break;
        const char *s = p;
        int depth = 0;
        while (*p) {
            if (*p == '(') depth++;
            else if (*p == ')') { if (depth) depth--; }
            else if (ISWS(*p) && !depth) break;
            p++;
        }
        Len l;
        if (len_parse(s, p, &l)) out[n++] = l;
        while (*p && ISWS(*p)) p++;
        if (p == s) break;
    }
    return n;
}

static void edges4(const Len *v, int n, int base, int font, int *t, int *r, int *b, int *l) {
    int px[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++)
        px[i] = (v[i].u == U_AUTO) ? M_AUTO : len_px(v[i], base, font);
    switch (n) {
        case 1: *t = *r = *b = *l = px[0]; break;
        case 2: *t = *b = px[0]; *r = *l = px[1]; break;
        case 3: *t = px[0]; *r = *l = px[1]; *b = px[2]; break;
        case 4: *t = px[0]; *r = px[1]; *b = px[2]; *l = px[3]; break;
        default: break;
    }
}

static int fg_of(const char *v) {
    if (!v) return -1;
    if (kwp(v, "inherit") || kwp(v, "currentcolor")) return -2;
    return ansi_color(v);
}

static int bg_of(const char *v) {
    if (!v || kwp(v, "transparent") || kwp(v, "none") || kwp(v, "inherit")) return -1;
    char c[32];
    if (bg_first_color(v, c)) return ansi_color(c);
    return ansi_color(v);
}

static void grid_place(const char *v, int *start, int *span) {
    *start = 0;
    *span = 1;
    const char *sl = strchr(v, '/');
    if (!pk_strncasecmp(v, "span", 4)) {
        *span = atoi(v + 4);
        if (*span < 1) *span = 1;
        return;
    }
    *start = atoi(v);
    if (!sl) return;
    const char *r = sl + 1;
    while (*r == ' ') r++;
    if (!pk_strncasecmp(r, "span", 4)) {
        *span = atoi(r + 4);
        if (*span < 1) *span = 1;
    } else {
        int e = atoi(r);
        if (e > *start) *span = e - *start;
    }
}

enum { LS_NONE, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL };

typedef struct {
    const char *tag;
    uint8_t role;
    uint8_t bold, ital, mono;
    uint8_t fs_n, fs_d;
    int16_t mt, mr, mb, ml;
    int16_t pt, pr, pb, pl;
    uint8_t bw, bstyle;
    uint8_t talign, ws, list;
    int8_t bg;
    uint8_t rule;
} UaRule;

#define RULE_NONE 0
#define RULE_TOP  1

static const UaRule UA[] = {
    {"html",        BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"body",        BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"div",         BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"section",     BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"article",     BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"nav",         BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"aside",       BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"header",      BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"footer",      BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"main",        BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"address",     BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"form",        BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"details",     BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"dialog",      BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"marquee",     BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"center",      BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_CENTER, WS_NORMAL, LS_NONE, -1, 0},
    {"summary",     BX_BLOCK, 1,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"fieldset",    BX_BLOCK, 0,0,0, 0,0, 16,0,16,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"figcaption",  BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},

    {"p",           BX_BLOCK, 0,0,0, 0,0, 16,0,16,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"h1",          BX_BLOCK, 1,0,0, 2,1,  21,0,21,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"h2",          BX_BLOCK, 1,0,0, 3,2,  20,0,20,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"h3",          BX_BLOCK, 1,0,0, 19,16, 19,0,19,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"h4",          BX_BLOCK, 1,0,0, 1,1,  21,0,21,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"h5",          BX_BLOCK, 1,0,0, 13,16, 22,0,22,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"h6",          BX_BLOCK, 1,0,0, 11,16, 25,0,25,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},

    {"ul",          BX_BLOCK, 0,0,0, 0,0, 16,0,16,0, 0,0,0,40, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_DISC, -1, 0},
    {"ol",          BX_BLOCK, 0,0,0, 0,0, 16,0,16,0, 0,0,0,40, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_DECIMAL, -1, 0},
    {"li",          BX_LIST,  0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"dl",          BX_BLOCK, 0,0,0, 0,0, 16,0,16,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"dt",          BX_BLOCK, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"dd",          BX_BLOCK, 0,0,0, 0,0, 0,0,0,40, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"blockquote",  BX_BLOCK, 0,0,0, 0,0, 16,40,16,40, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"figure",      BX_BLOCK, 0,0,0, 0,0, 16,40,16,40, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"pre",         BX_BLOCK, 0,0,1, 0,0, 16,0,16,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_PRE, LS_NONE, -1, 0},
    {"hr",          BX_HR,    0,0,0, 0,0, 8,0,8,0, 0,0,0,0, 0,BS_SOLID, TA_LEFT, WS_NORMAL, LS_NONE, -1, RULE_TOP},
    {"br",          BX_BR,    0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},

    {"table",       BX_TABLE, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"caption",     BX_TCAP,  0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_CENTER, WS_NORMAL, LS_NONE, -1, 0},
    {"colgroup",    BX_TROWG, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"col",         BX_TROWG, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"thead",       BX_TROWG, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"tbody",       BX_TROWG, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"tfoot",       BX_TROWG, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"tr",          BX_TROW,  0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"td",          BX_TCELL, 0,0,0, 0,0, 0,0,0,0, 1,1,1,1, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"th",          BX_TCELL, 1,0,0, 0,0, 0,0,0,0, 1,1,1,1, 0,BS_NONE, TA_CENTER, WS_NORMAL, LS_NONE, -1, 0},

    {"img",         BX_IMG,   0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},

    {"button",      BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 8,8,8,8, 8,BS_SOLID, TA_CENTER, WS_NOWRAP, LS_NONE, -1, 0},
    {"input",       BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 8,8,8,8, 8,BS_SOLID, TA_LEFT, WS_NOWRAP, LS_NONE, -1, 0},
    {"select",      BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 8,8,8,8, 8,BS_SOLID, TA_LEFT, WS_NOWRAP, LS_NONE, -1, 0},
    {"textarea",    BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 8,8,8,8, 8,BS_SOLID, TA_LEFT, WS_PREWRAP, LS_NONE, -1, 0},
    {"video",       BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"audio",       BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"canvas",      BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"object",      BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"iframe",      BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 8,BS_SOLID, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"embed",       BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"svg",         BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"math",        BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"progress",    BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 8,BS_SOLID, TA_LEFT, WS_NOWRAP, LS_NONE, -1, 0},
    {"meter",       BX_IBLOCK,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 8,BS_SOLID, TA_LEFT, WS_NOWRAP, LS_NONE, -1, 0},
    {"label",       BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},

    {"b",           BX_INLINE,1,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"strong",      BX_INLINE,1,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"i",           BX_INLINE,0,1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"em",          BX_INLINE,0,1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"cite",        BX_INLINE,0,1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"dfn",         BX_INLINE,0,1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"var",         BX_INLINE,0,1,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"code",        BX_INLINE,0,0,1, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"kbd",         BX_INLINE,0,0,1, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"samp",        BX_INLINE,0,0,1, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"small",       BX_INLINE,0,0,0, 10,12, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"big",         BX_INLINE,0,0,0, 6,5,  0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"sub",         BX_INLINE,0,0,0, 5,6,  0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"sup",         BX_INLINE,0,0,0, 5,6,  0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"mark",        BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, 3, 0},
    {"u",           BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"s",           BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"del",         BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"ins",         BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"span",        BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"a",           BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"abbr",        BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"q",           BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"time",        BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"output",      BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
    {"wbr",         BX_INLINE,0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE, TA_LEFT, WS_NORMAL, LS_NONE, -1, 0},
};

static const UaRule *ua_for(const char *t) {
    for (size_t i = 0; i < sizeof UA / sizeof *UA; i++)
        if (!strcmp(UA[i].tag, t)) return &UA[i];
    return 0;
}

static Box *bnew(Node *n) {
    Box *b = calloc(1, sizeof *b);
    if (!b) oom();
    b->n = n;
    b->fgcolor = b->bgcolor = b->bcolor = -1;
    b->font = vx_font_default();
    b->lh = vx_cell_h();
    b->role = BX_INLINE;
    b->basis = M_AUTO;
    b->attr_w = b->attr_h = M_AUTO;
    b->grow = 1;
    b->shrink = 1;
    b->gcs = 1;
    b->grs = 1;
    return b;
}

static void box_link(Box *p, Box *c) {
    c->par = p;
    if (!p->first) p->first = c;
    else p->last->next = c;
    p->last = c;
}

static int block_level(const Box *b) {
    switch (b->role) {
        case BX_BLOCK: case BX_ANON: case BX_TABLE: case BX_TCAP:
        case BX_TROWG: case BX_TROW: case BX_TCELL:
        case BX_FLEX: case BX_GRID: case BX_LIST: case BX_HR:
            return 1;
        default:
            return 0;
    }
}

static int establishes_bfc(const Box *b) {
    if (b->role == BX_ROOT) return 1;
    if (b->flt != FL_NONE || b->pos == POS_ABS || b->pos == POS_FIXED) return 1;
    if (b->ovf != 0) return 1;
    switch (b->role) {
        case BX_IBLOCK: case BX_TCELL: case BX_FLEX: case BX_GRID: case BX_TABLE:
            return 1;
        default:
            return 0;
    }
}

static Box *first_block_child(Box *b) {
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide) continue;
        if (c->flt != FL_NONE) continue;
        if (c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        return block_level(c) ? c : 0;
    }
    return 0;
}

static Box *last_block_child(Box *b) {
    Box *found = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide) continue;
        if (c->flt != FL_NONE) continue;
        if (c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        if (!block_level(c)) continue;
        found = c;
    }
    return found;
}

static void collapse_margins(Box *b) {
    for (Box *c = b->first; c; c = c->next) collapse_margins(c);
    if (establishes_bfc(b)) return;
    if (b->pt + b->bt == 0) {
        Box *fc = first_block_child(b);
        if (fc && !fc->mt_collapsed) {
            int m = fc->mt == M_AUTO ? 0 : fc->mt;
            if (b->mt == M_AUTO || m > b->mt) b->mt = m;
            fc->mt = 0;
            fc->mt_collapsed = 1;
        }
    }
    if (b->pb + b->bb == 0) {
        Box *lc = last_block_child(b);
        if (lc && lc->mb != M_AUTO) {
            if (b->mb == M_AUTO || lc->mb > b->mb) b->mb = lc->mb;
            lc->mb = 0;
        }
    }
}

static int role_from_kw(const char *d) {
    if (kwp(d, "none")) return 255;
    if (kwp(d, "block")) return BX_BLOCK;
    if (kwp(d, "inline-block")) return BX_IBLOCK;
    if (kwp(d, "inline-flex")) return BX_FLEX;
    if (kwp(d, "flex")) return BX_FLEX;
    if (kwp(d, "inline-grid")) return BX_GRID;
    if (kwp(d, "grid")) return BX_GRID;
    if (kwp(d, "list-item")) return BX_LIST;
    if (kwp(d, "inline-table")) return BX_TABLE;
    if (kwp(d, "table")) return BX_TABLE;
    if (kwp(d, "table-caption")) return BX_TCAP;
    if (kwp(d, "table-header-group") || kwp(d, "table-footer-group") ||
        kwp(d, "table-row-group") || kwp(d, "table-column-group") ||
        kwp(d, "table-column")) return BX_TROWG;
    if (kwp(d, "table-row")) return BX_TROW;
    if (kwp(d, "table-cell")) return BX_TCELL;
    if (kwp(d, "inline")) return BX_INLINE;
    return -1;
}

static void compute_style(Box *b, Box *par) {
    Node *n = b->n;
    int is_text = (n->def->f & T_TEXTN) != 0;
    if (is_text) b->role = BX_TEXT;
    const UaRule *ua = ua_for(n->tag);
    UaRule def = {"", BX_INLINE, 0,0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,BS_NONE,
                  TA_LEFT, WS_NORMAL, LS_NONE, -1, 0};
    if (!ua) ua = &def;

    int pfont = par ? par->font : vx_font_default();

    b->font = pfont;
    const char *v = sv(n, &P_FSIZE);
    if (v) {
        Len l;
        if (len_parse(v, v + strlen(v), &l) && l.u != U_AUTO) {
            if (l.u == U_PCT) b->font = len_px(l, pfont, pfont);
            else if (l.u == U_PX) b->font = len_px(l, pfont, pfont);
            else if (l.u == U_EM) b->font = (int)((long long)pfont * l.v / 64);
            else if (l.u == U_REM) b->font = (int)((long long)vx_font_default() * l.v / 64);
            else b->font = len_px(l, pfont, pfont);
        }
    } else if (ua->fs_n && ua->fs_d) {
        b->font = pfont * ua->fs_n / ua->fs_d;
    }
    if (b->font < 1) b->font = 1;

    b->lh = vx_cell_h();
    const char *lh = sv(n, &P_LH);
    if (lh) {
        Len l;
        if (kwp(lh, "normal")) b->lh = vx_cell_h();
        else if (len_parse(lh, lh + strlen(lh), &l) && l.u != U_AUTO) {
            if (l.u == U_PX || l.u == U_PT) b->lh = len_px(l, b->font, b->font);
            else if (l.u == U_PCT) b->lh = len_px(l, b->font, b->font);
            else b->lh = (int)((long long)b->font * l.v / 64);
        }
    } else if (par) {
        b->lh = par->lh;
    }
    if (b->lh < 1) b->lh = 1;

    b->fgcolor = par ? par->fgcolor : -1;
    v = sv(n, &P_COLOR);
    if (v) { int c = fg_of(v); if (c >= 0) b->fgcolor = c; }

    b->talign = par ? par->talign : TA_LEFT;
    v = sv(n, &P_ALIGN);
    if (v) {
        if (kwp(v, "center")) b->talign = TA_CENTER;
        else if (kwp(v, "right") || kwp(v, "end")) b->talign = TA_RIGHT;
        else if (kwp(v, "justify")) b->talign = TA_JUSTIFY;
        else b->talign = TA_LEFT;
    } else if (ua->talign == TA_CENTER) {
        b->talign = TA_CENTER;
    }

    b->ws = par ? par->ws : WS_NORMAL;
    v = sv(n, &P_WS);
    if (v) {
        if (kwp(v, "nowrap")) b->ws = WS_NOWRAP;
        else if (kwp(v, "pre")) b->ws = WS_PRE;
        else if (kwp(v, "pre-wrap")) b->ws = WS_PREWRAP;
        else if (kwp(v, "pre-line")) b->ws = WS_PRELINE;
        else b->ws = WS_NORMAL;
    } else if (ua->ws == WS_PRE) {
        b->ws = WS_PRE;
    }

    b->list_style = par ? par->list_style : LS_NONE;
    v = sv(n, &P_LISTSTYLE);
    if (v) {
        if (kwp(v, "none")) b->list_style = LS_NONE;
        else if (kwp(v, "circle")) b->list_style = LS_CIRCLE;
        else if (kwp(v, "square")) b->list_style = LS_SQUARE;
        else if (kwp(v, "decimal")) b->list_style = LS_DECIMAL;
        else if (kwp(v, "disc")) b->list_style = LS_DISC;
    } else if (ua->list) {
        b->list_style = ua->list;
    }

    b->text_indent = 0;
    b->ttrans = par ? par->ttrans : 0;
    v = sv(n, &P_TRANS);
    if (v) {
        if (kwp(v, "uppercase")) b->ttrans = 1;
        else if (kwp(v, "lowercase")) b->ttrans = 2;
        else if (kwp(v, "capitalize")) b->ttrans = 3;
        else b->ttrans = 0;
    }
    {
        const char *ti = sv(n, &P_TEXTINDENT);
        if (ti) {
            Len l;
            if (len_parse(ti, ti + strlen(ti), &l) && l.u != U_AUTO)
                b->text_indent = len_px(l, vx_width(), b->font);
        }
    }

    b->tflag = par ? par->tflag : 0;
    v = sv(n, &P_WEIGHT);
    if (v) { if (kwp(v, "bold") || atoi(v) >= 600) b->tflag |= FL_BOLD; else if (kwp(v,"normal")) b->tflag &= (uint8_t)~FL_BOLD; }
    else if (ua->bold) b->tflag |= FL_BOLD;
    v = sv(n, &P_FS);
    if (v) { if (kwp(v, "italic") || kwp(v, "oblique")) b->tflag |= FL_ITAL; else if (kwp(v,"normal")) b->tflag &= (uint8_t)~FL_ITAL; }
    else if (ua->ital) b->tflag |= FL_ITAL;
    v = sv(n, &P_DECO);
    if (v) {
        if (strstr(v, "underline")) b->tflag |= FL_UL;
        if (strstr(v, "line-through")) b->tflag |= FL_STRIKE;
    }

    b->bgcolor = bg_of(sv(n, &P_BG));
    v = sv(n, &P_BGS);
    if (v && b->bgcolor < 0) b->bgcolor = bg_of(v);
    if (b->bgcolor < 0 && ua->bg >= 0) b->bgcolor = ua->bg;

    v = sv(n, &P_VIS);
    if (v && kwp(v, "hidden")) b->vis_hidden = 1;
    else if (par && par->vis_hidden && !v) b->vis_hidden = 1;

    if (is_text) return;

    v = sv(n, &P_DISPLAY);
    if (v) {
        int r = role_from_kw(v);
        if (r == 255) { b->hide = 1; return; }
        b->role = (uint8_t)(r >= 0 ? r : BX_BLOCK);
    } else {
        b->role = ua->role;
    }

    v = sv(n, &P_POS);
    if (v) {
        if (kwp(v, "relative")) b->pos = POS_REL;
        else if (kwp(v, "absolute")) b->pos = POS_ABS;
        else if (kwp(v, "fixed")) b->pos = POS_FIXED;
    }
    v = sv(n, &P_FLOAT);
    if (v) {
        if (kwp(v, "left")) b->flt = FL_LEFT;
        else if (kwp(v, "right")) b->flt = FL_RIGHT;
    }
    if (b->role == BX_INLINE && (b->flt != FL_NONE || b->pos == POS_ABS || b->pos == POS_FIXED))
        b->role = BX_BLOCK;

    v = sv(n, &P_CLEAR);
    if (v) {
        if (kwp(v, "left")) b->clear = 1;
        else if (kwp(v, "right")) b->clear = 2;
        else if (kwp(v, "both")) b->clear = 3;
    }
    v = sv(n, &P_Z);
    if (v) b->z = atoi(v);
    v = sv(n, &P_OVERFLOW);
    if (v) b->ovf = (uint8_t)(kwp(v, "hidden") ? 1 : kwp(v, "scroll") ? 2 : kwp(v, "auto") ? 3 : 0);
    v = sv(n, &P_ORDER);
    if (v) b->order = atoi(v);

    b->mt = ua->mt; b->mr = ua->mr; b->mb = ua->mb; b->ml = ua->ml;
    v = sv(n, &P_MARGIN);
    if (v) { Len lv[4]; int cnt = split_vals(v, lv); if (cnt) edges4(lv, cnt, vx_width(), b->font, &b->mt, &b->mr, &b->mb, &b->ml); }
    int au;
    b->mt = len_auto(n, &P_MT, vx_width(), b->font, b->mt, &au);
    if (au) b->mt = M_AUTO;
    b->mr = len_auto(n, &P_MR, vx_width(), b->font, b->mr, &au);
    if (au) b->mr = M_AUTO;
    b->mb = len_auto(n, &P_MB, vx_width(), b->font, b->mb, &au);
    if (au) b->mb = M_AUTO;
    b->ml = len_auto(n, &P_ML, vx_width(), b->font, b->ml, &au);
    if (au) b->ml = M_AUTO;

    b->pt = ua->pt; b->pr = ua->pr; b->pb = ua->pb; b->pl = ua->pl;
    v = sv(n, &P_PAD);
    if (v) { Len lv[4]; int cnt = split_vals(v, lv); if (cnt) edges4(lv, cnt, vx_width(), b->font, &b->pt, &b->pr, &b->pb, &b->pl); }
    b->pt = len_of(n, &P_PT, vx_width(), b->font, b->pt);
    b->pr = len_of(n, &P_PR, vx_width(), b->font, b->pr);
    b->pb = len_of(n, &P_PB, vx_width(), b->font, b->pb);
    b->pl = len_of(n, &P_PL, vx_width(), b->font, b->pl);

    b->bstyle = ua->bstyle;
    b->bt = b->br = b->bb = b->bl = ua->bw;
    v = sv(n, &P_BW);
    if (v) { Len lv[4]; int cnt = split_vals(v, lv); if (cnt) edges4(lv, cnt, vx_width(), b->font, &b->bt, &b->br, &b->bb, &b->bl); }
    v = sv(n, &P_BORDER);
    if (v) {
        Len lv[4]; int cnt = split_vals(v, lv);
        for (int i = 0; i < cnt; i++)
            if (lv[i].u != U_AUTO && len_px(lv[i], b->font, b->font) > 0) {
                int wd = len_px(lv[i], b->font, b->font);
                b->bt = b->br = b->bb = b->bl = wd;
            }
        if (strstr(v, "dashed")) b->bstyle = BS_DASHED;
        else if (strstr(v, "dotted")) b->bstyle = BS_DOTTED;
        else if (strstr(v, "solid")) b->bstyle = BS_SOLID;
        if (strstr(v, "none") || strstr(v, "hidden")) { b->bstyle = BS_NONE; b->bt = b->br = b->bb = b->bl = 0; }
        int c = bg_of(v);
        if (c < 0) c = fg_of(v);
        if (c >= 0) b->bcolor = c;
    }
    v = sv(n, &P_BS);
    if (v) {
        if (strstr(v, "dashed")) b->bstyle = BS_DASHED;
        else if (strstr(v, "dotted")) b->bstyle = BS_DOTTED;
        else if (strstr(v, "solid")) b->bstyle = BS_SOLID;
        else if (strstr(v, "none") || strstr(v, "hidden")) { b->bstyle = BS_NONE; b->bt = b->br = b->bb = b->bl = 0; }
    }
    v = sv(n, &P_BC);
    if (v) { int c = fg_of(v); if (c >= 0) b->bcolor = c; }
    if (b->bstyle == BS_NONE) b->bt = b->br = b->bb = b->bl = 0;
    if (b->bcolor < 0) b->bcolor = b->fgcolor >= 0 ? b->fgcolor : 7;
    if (ua->rule == RULE_TOP) { b->bt = ua->bw ? ua->bw : 1; b->bstyle = BS_SOLID; }

    v = sv(n, &P_JUST);
    if (v) {
        if (kwp(v, "center")) b->jc = 1;
        else if (kwp(v, "flex-end") || kwp(v, "end")) b->jc = 2;
        else if (kwp(v, "space-between")) b->jc = 3;
        else if (kwp(v, "space-around")) b->jc = 4;
        else if (kwp(v, "space-evenly")) b->jc = 5;
        else b->jc = 0;
    }
    v = sv(n, &P_AI);
    if (v) {
        if (kwp(v, "center")) b->ai = 1;
        else if (kwp(v, "flex-end") || kwp(v, "end")) b->ai = 2;
        else if (kwp(v, "stretch")) b->ai = 3;
        else if (kwp(v, "baseline")) b->ai = 4;
        else b->ai = 0;
    }
    v = sv(n, &P_DIR);
    if (v) {
        if (kwp(v, "column-reverse")) b->fdir = 3;
        else if (kwp(v, "column")) b->fdir = 2;
        else if (kwp(v, "row-reverse")) b->fdir = 1;
        else b->fdir = 0;
    }
    v = sv(n, &P_FWRAP);
    if (v) b->fwrap = (uint8_t)(kwp(v, "wrap-reverse") ? 2 : kwp(v, "wrap") ? 1 : 0);
    v = sv(n, &P_FGROW);
    if (v) b->grow = (int)(atof(v) * 1000);
    v = sv(n, &P_FSHRINK);
    if (v) b->shrink = (int)(atof(v) * 1000);
    v = sv(n, &P_FBASIS);
    if (v) {
        Len l;
        if (len_parse(v, v + strlen(v), &l))
            b->basis = (l.u == U_AUTO) ? M_AUTO : len_px(l, vx_width(), b->font);
    }

    v = sv(n, &P_FLEX);
    if (v) {
        if (kwp(v, "none")) {
            b->grow = 0;
            b->shrink = 0;
            b->basis = M_AUTO;
        } else if (kwp(v, "auto")) {
            b->grow = 1000;
            b->shrink = 1000;
            b->basis = M_AUTO;
        } else if (kwp(v, "initial")) {
            b->grow = 0;
            b->shrink = 1000;
            b->basis = M_AUTO;
        } else {
            const char *p = v;
            int nv = 0;
            b->grow = 0;
            b->shrink = 1000;
            b->basis = 0;
            while (*p && nv < 3) {
                while (*p == ' ') p++;
                if (!*p) break;
                const char *s0 = p;
                while (*p && *p != ' ') p++;
                Len l;
                if (nv == 0) { b->grow = (int)(atof(s0) * 1000); nv++; }
                else if (nv == 1 && len_parse(s0, p, &l) && l.u == U_AUTO) {
                    b->basis = M_AUTO; nv = 3;
                } else if (nv == 1) {
                    char tmp[32];
                    size_t ln = (size_t)(p - s0);
                    if (ln > 30) ln = 30;
                    memcpy(tmp, s0, ln);
                    tmp[ln] = 0;
                    if (strchr(tmp, 'p') || strchr(tmp, '%') || strchr(tmp, 'e')) {
                        Len lb;
                        if (len_parse(s0, p, &lb) && lb.u != U_AUTO)
                            b->basis = len_px(lb, vx_width(), b->font);
                        nv = 3;
                    } else {
                        b->shrink = (int)(atof(s0) * 1000);
                        nv++;
                    }
                } else if (nv == 2) {
                    Len lb;
                    if (len_parse(s0, p, &lb) && lb.u != U_AUTO)
                        b->basis = len_px(lb, vx_width(), b->font);
                    nv++;
                } else break;
            }
        }
    }

    v = sv(n, &P_GCOL);
    if (v) grid_place(v, &b->gcol, &b->gcs);
    v = sv(n, &P_GROW);
    if (v) grid_place(v, &b->grw, &b->grs);

    v = attr_get(n, "colspan");
    if (v) b->colspan = atoi(v);
    v = attr_get(n, "rowspan");
    if (v) b->rowspan = atoi(v);
    if (b->colspan < 1) b->colspan = 1;
    if (b->rowspan < 1) b->rowspan = 1;

    if (b->role == BX_IMG) {
        v = attr_get(n, "width");
        if (v) { Len l; if (len_parse(v, v + strlen(v), &l)) b->attr_w = len_px(l, vx_width(), b->font); }
        v = attr_get(n, "height");
        if (v) { Len l; if (len_parse(v, v + strlen(v), &l)) b->attr_h = len_px(l, vx_height(), b->font); }
    }
    if (b->role == BX_TABLE || b->role == BX_TCELL) {
        v = attr_get(n, "width");
        if (v) { Len l; if (len_parse(v, v + strlen(v), &l) && l.u != U_AUTO) b->attr_w = len_px(l, vx_width(), b->font); }
        v = attr_get(n, "height");
        if (v) { Len l; if (len_parse(v, v + strlen(v), &l) && l.u != U_AUTO) b->attr_h = len_px(l, vx_height(), b->font); }
    }
    v = attr_get(n, "align");
    if (v) {
        if (kwp(v, "center")) b->talign = TA_CENTER;
        else if (kwp(v, "right")) b->talign = TA_RIGHT;
    }
}

static void build_kids(Box *par, Node *n);

static Box *build_one(Box *par, Node *n) {
    if (n->def->f & T_HIDDEN) return 0;
    Box *b = bnew(n);
    compute_style(b, par);
    if (b->hide) { free(b); return 0; }
    if (b->role == BX_TEXT) {
        b->text = n->text;
        b->tlen = n->tlen;
        return b;
    }
    if (b->role == BX_IMG || b->role == BX_BR || b->role == BX_HR) return b;
    build_kids(b, n);
    return b;
}

static int ws_only(Node *n) {
    if (!(n->def->f & T_TEXTN)) return 0;
    for (int i = 0; i < n->tlen; i++)
        if (!ISWS(n->text[i])) return 0;
    return 1;
}

static void build_kids(Box *par, Node *n) {
    int nkid = n->nchild, nblock = 0, nflow = 0;
    for (int i = 0; i < nkid; i++) {
        Node *c = n->child[i];
        if (c->def->f & T_HIDDEN) continue;
        if (c->tag[0] == '#') { nflow++; continue; }
        Box probe = {0};
        probe.n = c;
        probe.font = par->font;
        probe.lh = par->lh;
        probe.talign = par->talign;
        probe.ws = par->ws;
        probe.list_style = par->list_style;
        probe.fgcolor = par->fgcolor;
        compute_style(&probe, par);
        if (probe.hide) continue;
        nflow++;
        if (block_level(&probe)) nblock++;
    }
    int wrap = (nblock > 0 && nblock < nflow);
    Box *anon = 0;

    for (int i = 0; i < nkid; i++) {
        Node *c = n->child[i];
        if (c->def->f & T_HIDDEN) continue;
        if (wrap && ws_only(c)) continue;
        Box *b = build_one(par, c);
        if (!b) continue;
        if (wrap && !block_level(b) && b->flt == FL_NONE &&
            b->pos != POS_ABS && b->pos != POS_FIXED) {
            if (!anon) {
                anon = bnew(0);
                anon->role = BX_ANON;
                anon->font = par->font;
                anon->lh = par->lh;
                anon->fgcolor = par->fgcolor;
                anon->bgcolor = -1;
                anon->talign = par->talign;
                anon->ws = par->ws;
                anon->tflag = par->tflag;
                anon->list_style = par->list_style;
                anon->mt = anon->mr = anon->mb = anon->ml = 0;
                box_link(par, anon);
            }
            box_link(anon, b);
            continue;
        }
        anon = 0;
        box_link(par, b);
    }
}

Box *box_build(Node *root) {
    if (!root) return 0;
    Box *r = bnew(root);
    r->role = BX_ROOT;
    r->font = vx_font_default();
    r->lh = vx_cell_h();
    r->fgcolor = -1;
    r->talign = TA_LEFT;
    r->ws = WS_NORMAL;
    r->mt = r->mr = r->mb = r->ml = 0;
    build_kids(r, root);
    collapse_margins(r);
    return r;
}

void box_free(Box *b) {
    if (!b) return;
    Box *c = b->first;
    while (c) { Box *nx = c->next; box_free(c); c = nx; }
    for (int i = 0; i < b->nline; i++) free(b->lines[i].frag);
    free(b->lines);
    free(b);
}

int box_is_block(const Box *b) { return block_level(b); }

static const char *role_name(uint8_t r) {
    switch (r) {
        case BX_BLOCK: return "block";
        case BX_INLINE: return "inline";
        case BX_TEXT: return "text";
        case BX_ANON: return "anon";
        case BX_IBLOCK: return "iblock";
        case BX_TABLE: return "table";
        case BX_TROWG: return "rowgroup";
        case BX_TROW: return "row";
        case BX_TCELL: return "cell";
        case BX_TCAP: return "caption";
        case BX_FLEX: return "flex";
        case BX_GRID: return "grid";
        case BX_IMG: return "img";
        case BX_BR: return "br";
        case BX_HR: return "hr";
        case BX_LIST: return "list";
        case BX_ROOT: return "root";
    }
    return "?";
}

static void dump_box(Box *b, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", stdout);
    printf("%s", role_name(b->role));
    if (b->n && b->n->tag[0] != '#') printf(" <%s>", b->n->tag);
    printf(" [%d,%d %dx%d]", b->x, b->y, b->w, b->h);
    if (b->mt != 0 || b->mr != 0 || b->mb != 0 || b->ml != 0)
        printf(" m=%d,%d,%d,%d", b->mt, b->mr, b->mb, b->ml);
    if (b->pt || b->pr || b->pb || b->pl) printf(" p=%d,%d,%d,%d", b->pt, b->pr, b->pb, b->pl);
    if (b->bt || b->br || b->bb || b->bl) printf(" b=%d,%d,%d,%d", b->bt, b->br, b->bb, b->bl);
    if (b->text) printf(" \"%.*s\"", b->tlen > 24 ? 24 : b->tlen, b->text);
    putchar('\n');
    for (Box *c = b->first; c; c = c->next) dump_box(c, depth + 1);
}

void box_dump(Box *r) { if (r) dump_box(r, 0); }
