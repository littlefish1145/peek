#include "peek.h"
#include <stdarg.h>

const Prop
    P_COLOR = {"color", P_INH},
    P_BG = {"background-color", 0},
    P_WEIGHT = {"font-weight", P_INH},
    P_FS = {"font-style", P_INH},
    P_DECO = {"text-decoration", 0},
    P_ALIGN = {"text-align", P_INH},
    P_TRANS = {"text-transform", P_INH},
    P_PAD = {"padding", 0},
    P_WIDTH = {"width", 0},
    P_HEIGHT = {"height", 0},
    P_BORDER = {"border", 0},
    P_FSIZE = {"font-size", P_INH},
    P_MARGIN = {"margin", 0},
    P_DISPLAY = {"display", 0},
    P_BW = {"border-width", 0},
    P_BS = {"border-style", 0},
    P_BC = {"border-color", 0},
    P_RADIUS = {"border-radius", 0},
    P_MINW = {"min-width", 0},
    P_MAXW = {"max-width", 0},
    P_MINH = {"min-height", 0},
    P_MAXH = {"max-height", 0},
    P_LH = {"line-height", P_INH},
    P_POS = {"position", 0},
    P_TOP = {"top", 0},
    P_RIGHT = {"right", 0},
    P_BOTTOM = {"bottom", 0},
    P_LEFT = {"left", 0},
    P_GAP = {"gap", 0},
    P_JUST = {"justify-content", 0},
    P_DIR = {"flex-direction", 0},
    P_AI = {"align-items", 0},
    P_ASELF = {"align-self", 0},
    P_ACONTENT = {"align-content", 0},
    P_BGS = {"background", 0},

    P_MT = {"margin-top", 0},
    P_MR = {"margin-right", 0},
    P_MB = {"margin-bottom", 0},
    P_ML = {"margin-left", 0},
    P_PT = {"padding-top", 0},
    P_PR = {"padding-right", 0},
    P_PB = {"padding-bottom", 0},
    P_PL = {"padding-left", 0},
    P_BTW = {"border-top-width", 0},
    P_BRW = {"border-right-width", 0},
    P_BBW = {"border-bottom-width", 0},
    P_BLW = {"border-left-width", 0},

    P_FLOAT = {"float", 0},
    P_CLEAR = {"clear", 0},
    P_Z = {"z-index", 0},
    P_OVERFLOW = {"overflow", 0},
    P_VIS = {"visibility", P_INH},
    P_WS = {"white-space", P_INH},
    P_VALIGN = {"vertical-align", 0},
    P_TEXTINDENT = {"text-indent", P_INH},
    P_LISTSTYLE = {"list-style-type", P_INH},
    P_BOXSIZING = {"box-sizing", 0},

    P_FWRAP = {"flex-wrap", 0},
    P_FGROW = {"flex-grow", 0},
    P_FSHRINK = {"flex-shrink", 0},
    P_FBASIS = {"flex-basis", 0},
    P_FLEX = {"flex", 0},
    P_ORDER = {"order", 0},

    P_GTC = {"grid-template-columns", 0},
    P_GTR = {"grid-template-rows", 0},
    P_GCOL = {"grid-column", 0},
    P_GROW = {"grid-row", 0},
    P_GAFLOW = {"grid-auto-flow", 0},

    P_TLAYOUT = {"table-layout", 0},
    P_BCOLLAPSE = {"border-collapse", 0},
    P_BSPACING = {"border-spacing", 0},

    P_OPACITY = {"opacity", 0},
    P_FAMILY = {"font-family", P_INH},
    P_LETTERSP = {"letter-spacing", P_INH},
    P_WORDSP = {"word-spacing", P_INH},
    P_FONT = {"font", P_INH},
    P_SRC = {"src", 0},
    P_ALT = {"alt", 0},
    P_CONTENT = {"content", 0};

typedef struct { char *k, *v; } Var;
static Var *VAR;
static int NV, VCAP;

static const char *var_get(const char *k, int kl) {
    for (int i = 0; i < NV; i++)
        if ((int)strlen(VAR[i].k) == kl && !memcmp(VAR[i].k, k, kl)) return VAR[i].v;
    return 0;
}

static void var_put(const char *k, int kl, char *v) {
    for (int i = 0; i < NV; i++)
        if ((int)strlen(VAR[i].k) == kl && !memcmp(VAR[i].k, k, kl)) {
            VAR[i].v = v;
            return;
        }
    GROW(VAR, NV, VCAP, Var);
    VAR[NV].k = sdup(k, kl);
    VAR[NV].v = v;
    NV++;
}

static char *var_expand_len(const char *v, int n);

char *var_expand(const char *v) {
    const char *p = strstr(v, "var(");
    if (!p) return 0;
    char *out = malloc(strlen(v) * 16 + 64);
    if (!out) oom();
    size_t w = 0;
    const char *s = v;
    while (1) {
        p = strstr(s, "var(");
        if (!p) break;
        memcpy(out + w, s, p - s);
        w += p - s;
        const char *in = p + 4;
        int depth = 1;
        const char *q = in;
        while (*q && depth) {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            q++;
        }
        const char *close = depth ? in + strlen(in) : q - 1;
        const char *cm = memchr(in, ',', close - in);
        const char *nm = in;
        int nl = cm ? (int)(cm - in) : (int)(close - in);
        while (nl && ISWS(nm[0])) nm++, nl--;
        while (nl && ISWS(nm[nl - 1])) nl--;
        const char *val = var_get(nm, nl);
        if (!val && cm) {
            const char *fb = cm + 1;
            int fl = (int)(close - fb);
            while (fl && ISWS(fb[0])) fb++, fl--;
            while (fl && ISWS(fb[fl - 1])) fl--;
            char *fbx = var_expand_len(fb, fl);
            if (fbx) val = fbx;
            else {
                char *fbs = malloc(fl + 1);
                if (!fbs) oom();
                memcpy(fbs, fb, fl);
                fbs[fl] = 0;
                val = fbs;
            }
        }
        if (val) {
            size_t vl = strlen(val);
            memcpy(out + w, val, vl);
            w += vl;
        }
        s = close < v + strlen(v) ? close + 1 : v + strlen(v);
    }
    strcpy(out + w, s);
    return out;
}

static char *var_expand_len(const char *v, int n) {
    char *tmp = malloc(n + 1);
    if (!tmp) oom();
    memcpy(tmp, v, n);
    tmp[n] = 0;
    char *r = var_expand(tmp);
    free(tmp);
    return r;
}

int bg_first_color(const char *v, char *out) {
    if (!v) return 0;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", v);
    for (char *p = strtok(tmp, " \t"); p; p = strtok(0, " \t")) {
        if (strstr(p, "url(") || strstr(p, "gradient")) continue;
        if (strchr(p, '(')) continue;
        if (ansi_color(p) >= 0) {
            snprintf(out, 32, "%s", p);
            return 1;
        }
    }
    return 0;
}

static const Prop *const PROPS[] = {
    &P_COLOR, &P_BG, &P_WEIGHT, &P_FS, &P_DECO, &P_ALIGN,
    &P_TRANS, &P_PAD, &P_WIDTH, &P_HEIGHT, &P_BORDER, &P_FSIZE, &P_MARGIN, &P_DISPLAY,
    &P_BW, &P_BS, &P_BC, &P_RADIUS, &P_MINW, &P_MAXW, &P_MINH, &P_MAXH, &P_LH,
    &P_POS, &P_TOP, &P_RIGHT, &P_BOTTOM, &P_LEFT,
    &P_GAP, &P_JUST, &P_DIR, &P_AI, &P_ASELF, &P_ACONTENT, &P_BGS,
    &P_MT, &P_MR, &P_MB, &P_ML, &P_PT, &P_PR, &P_PB, &P_PL,
    &P_BTW, &P_BRW, &P_BBW, &P_BLW,
    &P_FLOAT, &P_CLEAR, &P_Z, &P_OVERFLOW, &P_VIS, &P_WS, &P_VALIGN,
    &P_TEXTINDENT, &P_LISTSTYLE, &P_BOXSIZING,
    &P_FWRAP, &P_FGROW, &P_FSHRINK, &P_FBASIS, &P_FLEX, &P_ORDER,
    &P_GTC, &P_GTR, &P_GCOL, &P_GROW, &P_GAFLOW,
    &P_TLAYOUT, &P_BCOLLAPSE, &P_BSPACING,
    &P_OPACITY, &P_FAMILY, &P_LETTERSP, &P_WORDSP, &P_FONT, &P_SRC, &P_ALT,
    &P_CONTENT,
};

const Prop *prop_find(const char *k) {
    for (size_t i = 0; i < sizeof PROPS / sizeof *PROPS; i++)
        if (!strcmp(PROPS[i]->name, k)) return PROPS[i];
    return 0;
}

void ua_bold(Node *n) { st_push(n, &P_WEIGHT, "bold", -1); }

void ua_italic(Node *n) { st_push(n, &P_FS, "italic", -1); }

void ua_link(Node *n) {
    st_push(n, &P_COLOR, "blue", -1);
    st_push(n, &P_DECO, "underline", -1);
}

static Rule *R;
static int NR, RCAP;
static Decl **DL;
static int NDL, DLCAP;
static char **CD;
static int NCD, CDCAP;
static int VW = 1280, VH = 800;
static int NPE;
static void idx_build(void);
static int match_compound(const char *c, Node *n);
static void gen_apply(Node *n, int which);
static int media_eval(const char *c);

void css_viewport(int w, int h) {
    if (w > 0) VW = w;
    if (h > 0) VH = h;
}

void css_get_viewport(int *w, int *h) {
    *w = VW;
    *h = VH;
}

int media_match(const char *q) {
    return q && *q ? media_eval(q) : 1;
}

const Rule *css_rules(int *n) {
    *n = NR;
    return R;
}

static void rule_free(Rule *r) {
    free(r->ps);
    free(r->sep);
    r->ps = 0;
    r->sep = 0;
    r->np = r->pcap = 0;
}

void css_reset(void) {
    for (int i = 0; i < NR; i++) rule_free(R + i);
    NR = 0;
    NPE = 0;
    for (int i = 0; i < NDL; i++) free(DL[i]);
    NDL = 0;
    for (int i = 0; i < NCD; i++) free(CD[i]);
    NCD = 0;
    for (int i = 0; i < NV; i++) {
        free(VAR[i].k);
        free(VAR[i].v);
    }
    NV = 0;
}

static char *skip_ws(char *p, char *e) {
    while (p < e && ISWS(*p)) p++;
    return p;
}

static int span_is(const char *s, int n, const char *w) {
    return n == (int)strlen(w) && !strncasecmp(s, w, (size_t)n);
}

static char *close_paren(char *p, char *e) {
    if (*p != '(') return 0;
    char q = 0;
    int dep = 1;
    for (p++; p < e; p++) {
        if (q) {
            if (q == '[' ? *p == ']' : *p == q) q = 0;
            continue;
        }
        if (*p == '"' || *p == '\'' || *p == '[') q = *p;
        else if (*p == '(') dep++;
        else if (*p == ')' && !--dep) return p;
    }
    return 0;
}

static char *top_comma(char *s, char *e) {
    char q = 0;
    int par = 0;
    for (; s < e; s++) {
        if (q) {
            if (q == '[' ? *s == ']' : *s == q) q = 0;
            continue;
        }
        if (*s == '"' || *s == '\'' || *s == '[') q = *s;
        else if (*s == '(') par++;
        else if (*s == ')') par = par > 0 ? par - 1 : 0;
        else if (*s == ',' && !par) return s;
    }
    return 0;
}

static char *top_char(char *s, char *e, char want) {
    char q = 0;
    int par = 0;
    for (; s < e; s++) {
        if (q) {
            if (q == '[' ? *s == ']' : *s == q) q = 0;
            continue;
        }
        if (*s == '"' || *s == '\'' || *s == '[') q = *s;
        else if (*s == '(') par++;
        else if (*s == ')') par = par > 0 ? par - 1 : 0;
        else if (*s == want && !par) return s;
    }
    return 0;
}

static int media_len(const char *v, int n, int base) {
    char *e;
    double x = strtod(v, &e);
    if (e == v) return 0;
    while (e < v + n && ISWS(*e)) e++;
    int rl = (int)(v + n - e);
    if (!rl || !strncmp(e, "px", 2)) return (int)x;
    if (!strncmp(e, "%", 1)) return (int)(x * base / 100);
    if (!strncmp(e, "rem", 3) || !strncmp(e, "em", 2)) return (int)(x * 16);
    if (!strncmp(e, "pt", 2)) return (int)(x * 96 / 72);
    if (!strncmp(e, "pc", 2)) return (int)(x * 16);
    if (!strncmp(e, "in", 2)) return (int)(x * 96);
    if (!strncmp(e, "cm", 2)) return (int)(x * 96 / 2.54);
    if (!strncmp(e, "mm", 2)) return (int)(x * 96 / 25.4);
    if (!strncmp(e, "vw", 2)) return (int)(x * VW / 100);
    if (!strncmp(e, "vh", 2)) return (int)(x * VH / 100);
    return (int)x;
}

static int feat_eval(char *s, char *e) {
    char *col = top_char(s, e, ':');
    char *np = col ? col : e;
    int nl = (int)(np - s);
    while (nl && ISWS(s[0])) s++, nl--;
    while (nl && ISWS(s[nl - 1])) nl--;
    char *vp = col ? skip_ws(col + 1, e) : e;
    int vl = (int)(e - vp);
    while (vl && (ISWS(vp[vl - 1]) || vp[vl - 1] == ')')) vl--;
    int cmp = 0, kind = 0, base = 0;
    if (nl > 4 && !strncasecmp(s, "min-", 4)) s += 4, nl -= 4, cmp = 1;
    else if (nl > 4 && !strncasecmp(s, "max-", 4)) s += 4, nl -= 4, cmp = 2;
    if (span_is(s, nl, "width") || span_is(s, nl, "device-width")) kind = 1, base = VW;
    else if (span_is(s, nl, "height") || span_is(s, nl, "device-height")) kind = 2, base = VH;
    else if (span_is(s, nl, "orientation")) kind = 3;
    else if (span_is(s, nl, "aspect-ratio") ||
             span_is(s, nl, "device-aspect-ratio")) kind = 4;
    else if (span_is(s, nl, "color") || span_is(s, nl, "monochrome")) kind = 5;
    if (!kind) return 0;
    if (!col) return kind == 5;
    if (kind == 3) {
        int p = cmp == 1 ? 1 : cmp == 2 ? 0 : 1;
        int land = VW >= VH;
        int want = vl >= 8 && !strncasecmp(vp, "landscape", 8);
        return p && land == want;
    }
    if (kind == 5) return 1;
    if (kind == 4) {
        char *sl = memchr(vp, '/', vl);
        if (!sl) return 0;
        double num = atof(vp), den = atof(sl + 1);
        if (den <= 0) return 0;
        double have = (double)VW / (double)VH, want = num / den;
        return cmp == 1 ? have >= want : cmp == 2 ? have <= want : have == want;
    }
    int want = media_len(vp, vl, base);
    return cmp == 1 ? base >= want : cmp == 2 ? base <= want : base == want;
}

static int query_eval(char *s, char *e) {
    int neg = 0, res = 1;
    char *p = s;
    while (p < e) {
        p = skip_ws(p, e);
        if (p >= e) break;
        if (*p == '(') {
            char *cl = close_paren(p, e);
            if (!feat_eval(p + 1, cl ? cl : e)) res = 0;
            p = cl ? cl + 1 : e;
            continue;
        }
        char *w = p;
        while (p < e && !ISWS(*p) && *p != '(') p++;
        int wl = (int)(p - w);
        if (span_is(w, wl, "not")) neg = 1;
        else if (span_is(w, wl, "only") || span_is(w, wl, "all") ||
                 span_is(w, wl, "and") || span_is(w, wl, "screen")) {
        } else res = 0;
    }
    return neg ? !res : res;
}

static int media_eval(const char *c) {
    char *s = (char *)c, *e = s + strlen(s);
    int res = 0;
    while (s < e) {
        char *cm = top_comma(s, e);
        if (query_eval(s, cm ? cm : e)) res = 1;
        if (!cm) break;
        s = cm + 1;
    }
    return res;
}

static char *cond_add(char *s, char *e) {
    char *p = skip_ws(s, e);
    int n = (int)(e - p);
    while (n && ISWS(p[n - 1])) n--;
    if (!n) return 0;
    char *c = sdup(p, (size_t)n);
    GROW(CD, NCD, CDCAP, char *);
    CD[NCD++] = c;
    return c;
}

static void rule_grow(Rule *r, int need) {
    if (need <= r->pcap) return;
    int c = r->pcap ? r->pcap : 8;
    while (c < need) c <<= 1;
    char **ps = realloc(r->ps, (size_t)c * sizeof *r->ps);
    if (!ps) oom();
    r->ps = ps;
    uint8_t *sep = realloc(r->sep, (size_t)(c + 1) * sizeof *sep);
    if (!sep) oom();
    r->sep = sep;
    r->pcap = c;
}

static void pe_strip(Rule *r) {
    if (!r->np) return;
    char *last = r->ps[r->np - 1];
    int br = 0, par = 0;
    for (char *q = last; *q; q++) {
        if (br) {
            if (*q == ']') br = 0;
            continue;
        }
        if (par) {
            if (*q == ')') par = 0;
            continue;
        }
        if (*q == '[') {
            br = 1;
            continue;
        }
        if (*q == '(') {
            par = 1;
            continue;
        }
        if (*q != ':') continue;
        char *nm = q + 1;
        if (*nm == ':') nm++;
        if (!strcmp(nm, "before")) r->pe = PE_BEFORE;
        else if (!strcmp(nm, "after")) r->pe = PE_AFTER;
        else if (!strcmp(nm, "first-line") || !strcmp(nm, "first-letter") ||
                 !strcmp(nm, "selection") || !strcmp(nm, "placeholder")) r->pe = PE_UNSUP;
        else continue;
        *q = 0;
        return;
    }
}

void presplit(Rule *r, char *sel) {
    char *p = sel;
    r->np = 0;
    rule_grow(r, 1);
    r->sep[0] = ' ';
    while (*p) {
        if (*p == '>' || *p == '+' || *p == '~') {
            rule_grow(r, r->np + 1);
            r->sep[r->np] = *p;
            *p++ = 0;
            continue;
        }
        while (ISWS(*p)) *p++ = 0;
        if (!*p || *p == '>' || *p == '+' || *p == '~') continue;
        rule_grow(r, r->np + 2);
        r->ps[r->np++] = p;
        while (*p && !ISWS(*p) && *p != '>' && *p != '+' && *p != '~') {
            if (*p == '(') {
                char *cl = close_paren(p, p + strlen(p));
                p = cl ? cl + 1 : p + strlen(p);
                continue;
            }
            if (*p == '[') {
                char *cl = strchr(p, ']');
                p = cl ? cl + 1 : p + strlen(p);
                continue;
            }
            p++;
        }
        r->sep[r->np] = ' ';
    }
    pe_strip(r);
}

static Decl *decl_parse(char *s, char *e, int *n) {
    Decl *d = 0;
    int cap = 0, cnt = 0;
    *n = 0;
    while (s < e) {
        char *semi = top_char(s, e, ';');
        char *de = semi ? semi : e;
        char *c = top_char(s, de, ':');
        if (c) {
            char *kk = cut(s, c);
            char *vv = cut(c + 1, de);
            char *im = strstr(vv, "!important");
            uint8_t flag = 0;
            if (im) {
                flag = 1;
                *im = 0;
                while (im > vv && ISWS(im[-1])) *--im = 0;
            }
            char *ev = var_expand(vv);
            if (ev) vv = ev;
            if (kk[0] == '-' && kk[1] == '-') {
                var_put(kk, strlen(kk), vv);
            } else {
                const Prop *p = prop_find(kk);
                if (p) {
                    GROW(d, cnt, cap, Decl);
                    d[cnt].p = p;
                    d[cnt].v = vv;
                    d[cnt].imp = flag;
                    cnt++;
                }
            }
        }
        s = de + 1;
    }
    *n = cnt;
    return d;
}

static char *brace_match(char *b, char *end) {
    char q = 0;
    int dep = 1;
    for (; b < end; b++) {
        if (q) {
            if (*b == q) q = 0;
            continue;
        }
        if (*b == '"' || *b == '\'') q = *b;
        else if (*b == '{') dep++;
        else if (*b == '}' && !--dep) return b;
    }
    return 0;
}

static void rule_add(char *sel, Decl *d, int nd, char *media) {
    if (NR >= RCAP) {
        RCAP = RCAP ? RCAP << 1 : 64;
        Rule *rp = realloc(R, (size_t)RCAP * sizeof *R);
        if (!rp) oom();
        R = rp;
    }
    Rule *r = R + NR;
    memset(r, 0, sizeof *r);
    r->d = d;
    r->nd = nd;
    r->media = media;
    presplit(r, sel);
    if (!r->np) rule_free(r);
    else {
        if (r->pe) NPE++;
        NR++;
    }
}

static void parse_range(char *pos, char *end, char *media) {
    while (pos < end) {
        char *q = pos, *b = 0, *sm = 0;
        char qq = 0;
        int par = 0;
        while (q < end) {
            if (qq) {
                if (qq == '[' ? *q == ']' : *q == qq) qq = 0;
                q++;
                continue;
            }
            if (*q == '"' || *q == '\'' || *q == '[') qq = *q;
            else if (*q == '(') par++;
            else if (*q == ')') par = par > 0 ? par - 1 : 0;
            else if (!par && *q == ';') { sm = q; break; }
            else if (!par && *q == '{') { b = q; break; }
            q++;
        }
        if (!b) break;
        if (sm && sm < b) { pos = sm + 1; continue; }
        char *e = brace_match(b + 1, end);
        if (!e) break;
        char *ps = skip_ws(pos, b);
        if (ps < b && *ps == '@') {
            int ll = (int)(b - ps);
            if (ll >= 6 && !strncasecmp(ps, "@media", 6) &&
                (ll == 6 || ISWS(ps[6]) || ps[6] == '('))
                parse_range(b + 1, e, cond_add(ps + 6, b));
            pos = e + 1;
            continue;
        }
        int nd = 0;
        Decl *d = decl_parse(b + 1, e, &nd);
        if (d && nd) {
            GROW(DL, NDL, DLCAP, Decl *);
            DL[NDL++] = d;
        }
        for (char *p2 = pos; p2 < b;) {
            char *cm = top_comma(p2, b);
            char *ce = cm ? cm : b;
            char *sel = cut(p2, ce);
            if (*sel) rule_add(sel, d, nd, media);
            if (!cm) break;
            p2 = cm + 1;
        }
        pos = e + 1;
    }
}

void parse_css(char *css) {
    char *w = css, *p = css;
    char q = 0;
    while (*p) {
        if (q) {
            if (*p == q) q = 0;
            *w++ = *p++;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            q = *p;
            *w++ = *p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
            continue;
        }
        *w++ = *p++;
    }
    *w = 0;
    parse_range(css, w, 0);
    idx_build();
}

typedef struct { uint64_t k; int *r; int n, cap; } Bkt;
static Bkt *BK;
static int NBK, BCAP;
static int *GEN;
static int NGEN, GCAP;

static void bkt_add(uint64_t k, int ri) {
    for (int i = 0; i < NBK; i++)
        if (BK[i].k == k) {
            GROW(BK[i].r, BK[i].n, BK[i].cap, int);
            BK[i].r[BK[i].n++] = ri;
            return;
        }
    GROW(BK, NBK, BCAP, Bkt);
    BK[NBK].k = k;
    BK[NBK].r = 0;
    BK[NBK].n = 0;
    BK[NBK].cap = 0;
    GROW(BK[NBK].r, BK[NBK].n, BK[NBK].cap, int);
    BK[NBK].r[BK[NBK].n++] = ri;
    NBK++;
}

static void gen_add(int ri) {
    GROW(GEN, NGEN, GCAP, int);
    GEN[NGEN++] = ri;
}

static int bkt_get(uint64_t k, int **out) {
    for (int i = 0; i < NBK; i++)
        if (BK[i].k == k) {
            *out = BK[i].r;
            return BK[i].n;
        }
    return 0;
}

static int key_name(const char *p, char *out, int cap) {
    int n = 0;
    while (p[n] && p[n] != '.' && p[n] != '#' && p[n] != '[' && p[n] != ':' &&
           !ISWS(p[n]) && n < cap - 1) {
        out[n] = p[n];
        n++;
    }
    out[n] = 0;
    return n;
}

static int sel_key(const char *c, char *out, int cap) {
    int par = 0, br = 0, tl = 0;
    const char *idp = 0, *clp = 0;
    for (const char *p = c; *p; p++) {
        if (br) {
            if (*p == ']') br = 0;
            continue;
        }
        if (par) {
            if (*p == ')') par = 0;
            continue;
        }
        if (*p == '[') {
            br = 1;
            if (!tl) tl = (int)(p - c);
            continue;
        }
        if (*p == '(') {
            par = 1;
            continue;
        }
        if (*p == '#' || *p == '.' || *p == ':') {
            if (!tl) tl = (int)(p - c);
            if (*p == '#' && !idp) idp = p + 1;
            else if (*p == '.' && !clp) clp = p + 1;
            continue;
        }
    }
    if (idp) return key_name(idp, out, cap) ? 2 : 3;
    if (clp) return key_name(clp, out, cap) ? 1 : 3;
    if (!tl || *c == '*') return 3;
    return key_name(c, out, cap) ? 0 : 3;
}

static void idx_build(void) {
    NBK = NGEN = 0;
    for (int i = 0; i < NR; i++) {
        Rule *r = R + i;
        if (!r->np) continue;
        char key[128];
        int kind = sel_key(r->ps[r->np - 1], key, sizeof key);
        if (kind == 3) gen_add(i);
        else bkt_add((pk(key) << 2) | (uint64_t)kind, i);
    }
}

static int cls_has(const char *cv, const char *s, int sl) {
    if (!sl) return 0;
    for (const char *p = cv; *p;) {
        while (*p && ISWS(*p)) p++;
        const char *e = p;
        while (*e && !ISWS(*e)) e++;
        if ((int)(e - p) == sl && !memcmp(p, s, (size_t)sl)) return 1;
        p = e;
    }
    return 0;
}

static const char *class_attr(Node *n) {
    for (int i = 0; i < n->nattr; i++)
        if (pk(n->attrs[i].k) == K_CLASS) return n->attrs[i].v;
    return 0;
}

static void sib_pos(Node *n, int *idx, int *cnt, int *tidx, int *tcnt) {
    Node *pa = n->parent;
    *idx = *cnt = *tidx = *tcnt = 0;
    if (!pa) return;
    for (int i = 0; i < pa->nchild; i++) {
        Node *s = pa->child[i];
        if (s->tag[0] == '#' || s->gen) continue;
        if (s == n) *idx = *cnt;
        (*cnt)++;
        if (s->taglen == n->taglen && !memcmp(s->tag, n->tag, n->taglen)) {
            if (s == n) *tidx = *tcnt;
            (*tcnt)++;
        }
    }
}

static int parse_anb(const char *s, int n, int *A, int *B) {
    while (n && ISWS(s[0])) s++, n--;
    while (n && ISWS(s[n - 1])) n--;
    if (!n) return 0;
    if (span_is(s, n, "odd")) return *A = 2, *B = 1, 1;
    if (span_is(s, n, "even")) return *A = 2, *B = 0, 1;
    const char *p = s, *e = s + n;
    int sign = 1, num = 0, has = 0;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    while (p < e && isdigit((unsigned char)*p)) num = num * 10 + (*p++ - '0'), has = 1;
    if (p < e && (*p == 'n' || *p == 'N')) {
        p++;
        *A = sign * (has ? num : 1);
        while (p < e && ISWS(*p)) p++;
        int bsign = 1;
        if (p < e && (*p == '+' || *p == '-')) bsign = *p++ == '-' ? -1 : 1;
        while (p < e && ISWS(*p)) p++;
        int b = 0, hb = 0;
        while (p < e && isdigit((unsigned char)*p)) b = b * 10 + (*p++ - '0'), hb = 1;
        *B = bsign * (hb ? b : 0);
        return p == e;
    }
    if (!has || p != e) return 0;
    *A = 0;
    *B = sign * num;
    return 1;
}

static int nth_hit(int A, int B, int pos) {
    if (!A) return pos == B;
    int d = pos - B;
    if (A > 0) return d >= 0 && !(d % A);
    return d <= 0 && !(-d % -A);
}

static int is_empty(Node *n) {
    for (int i = 0; i < n->nchild; i++) {
        Node *c = n->child[i];
        if (c->gen) continue;
        if (c->tag[0] != '#') return 0;
        if ((c->def->f & T_TEXTN) && c->tlen) return 0;
    }
    return 1;
}

static int form_field(Node *n) {
    return span_is(n->tag, n->taglen, "input") ||
           span_is(n->tag, n->taglen, "button") ||
           span_is(n->tag, n->taglen, "select") ||
           span_is(n->tag, n->taglen, "textarea") ||
           span_is(n->tag, n->taglen, "option") ||
           span_is(n->tag, n->taglen, "optgroup") ||
           span_is(n->tag, n->taglen, "fieldset");
}

static int not_has(Node *n, const char *arg, int al) {
    char *tmp = sdup(arg, (size_t)al);
    char *p = tmp;
    int hit = 0;
    while (p && *p) {
        char *cm = top_comma(p, p + strlen(p));
        char *seg = cut(p, cm ? cm : p + strlen(p));
        if (*seg) {
            Rule r = {0};
            presplit(&r, seg);
            if (r.np && match_selector(&r, n) >= 0) hit = 1;
            rule_free(&r);
        }
        p = cm ? cm + 1 : 0;
    }
    free(tmp);
    return hit;
}

static int pseudo(const char *name, int nl, const char *arg, int al, Node *n) {
    int idx, cnt, tidx, tcnt;
    if (span_is(name, nl, "first-child"))
        return sib_pos(n, &idx, &cnt, &tidx, &tcnt), idx == 0;
    if (span_is(name, nl, "last-child"))
        return sib_pos(n, &idx, &cnt, &tidx, &tcnt), idx == cnt - 1;
    if (span_is(name, nl, "only-child"))
        return sib_pos(n, &idx, &cnt, &tidx, &tcnt), idx == 0 && cnt == 1;
    if (span_is(name, nl, "first-of-type"))
        return sib_pos(n, &idx, &cnt, &tidx, &tcnt), tidx == 0;
    if (span_is(name, nl, "last-of-type"))
        return sib_pos(n, &idx, &cnt, &tidx, &tcnt), tidx == tcnt - 1;
    if (span_is(name, nl, "only-of-type"))
        return sib_pos(n, &idx, &cnt, &tidx, &tcnt), tidx == 0 && tcnt == 1;
    if (span_is(name, nl, "nth-child") || span_is(name, nl, "nth-last-child") ||
        span_is(name, nl, "nth-of-type") || span_is(name, nl, "nth-last-of-type")) {
        if (!arg) return 0;
        int A, B;
        if (!parse_anb(arg, al, &A, &B)) return 0;
        sib_pos(n, &idx, &cnt, &tidx, &tcnt);
        int oftype = nl >= 7 && !strncasecmp(name + nl - 7, "-of-type", 8);
        int last = nl >= 9 && !strncasecmp(name + 4, "-last", 5);
        int pos = (oftype ? tidx : idx) + 1;
        int tot = oftype ? tcnt : cnt;
        if (last) pos = tot - pos + 1;
        return nth_hit(A, B, pos);
    }
    if (span_is(name, nl, "empty")) return is_empty(n);
    if (span_is(name, nl, "root"))
        return !n->parent || n->parent->tag[0] == '#';
    if (span_is(name, nl, "checked")) {
        if (span_is(n->tag, n->taglen, "option")) return attr_get(n, "selected") != 0;
        if (!span_is(n->tag, n->taglen, "input")) return 0;
        char *ty = attr_get(n, "type");
        if (ty && strcmp(ty, "checkbox") && strcmp(ty, "radio")) return 0;
        return attr_get(n, "checked") != 0;
    }
    if (span_is(name, nl, "disabled"))
        return form_field(n) && attr_get(n, "disabled") != 0;
    if (span_is(name, nl, "enabled"))
        return form_field(n) && !attr_get(n, "disabled");
    if (span_is(name, nl, "required"))
        return form_field(n) && attr_get(n, "required") != 0;
    if (span_is(name, nl, "not")) return arg && !not_has(n, arg, al);
    return 0;
}

static int pack_spec(int ids, int cls, int tags) {
    if (ids > 255) ids = 255;
    if (cls > 255) cls = 255;
    if (tags > 255) tags = 255;
    return (ids << 16) | (cls << 8) | tags;
}

static int spec_compound(const char *c) {
    int ids = 0, cls = 0, tags = 0;
    const char *p = c;
    while (*p) {
        if (*p == '[') {
            while (*p && *p != ']') p++;
            if (*p) p++;
            cls++;
            continue;
        }
        if (*p == ':') {
            if (p[1] == ':') {
                p += 2;
                while (*p && (isalnum((unsigned char)*p) || *p == '-')) p++;
                tags++;
                continue;
            }
            p++;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) p++;
            if (*p == '(') {
                int dep = 1;
                for (p++; *p && dep; p++) {
                    if (*p == '(') dep++;
                    else if (*p == ')') dep--;
                }
            }
            cls++;
            continue;
        }
        if (*p == '#' || *p == '.') {
            char w = *p++;
            while (*p && *p != '#' && *p != '.' && *p != '[' && *p != ':' && !ISWS(*p)) p++;
            if (w == '#') ids++;
            else cls++;
            continue;
        }
        if (*p == '*') {
            p++;
            continue;
        }
        if (isalpha((unsigned char)*p)) {
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) p++;
            tags++;
            continue;
        }
        p++;
    }
    return pack_spec(ids, cls, tags);
}

static int pseudo_spec(const char *name, int nl, const char *arg, int al) {
    if (!span_is(name, nl, "not") || !arg) return 1 << 8;
    char *tmp = sdup(arg, (size_t)al);
    int best = 0;
    char *p = tmp;
    while (p && *p) {
        char *cm = top_comma(p, p + strlen(p));
        char *seg = cut(p, cm ? cm : p + strlen(p));
        if (*seg) {
            Rule r = {0};
            presplit(&r, seg);
            int ids = 0, cls = 0, tags = 0;
            for (int i = 0; i < r.np; i++) {
                int sp = spec_compound(r.ps[i]);
                ids += sp >> 16;
                cls += (sp >> 8) & 0xff;
                tags += sp & 0xff;
            }
            rule_free(&r);
            int sp = pack_spec(ids, cls, tags);
            if (sp > best) best = sp;
        }
        p = cm ? cm + 1 : 0;
    }
    free(tmp);
    return best;
}

static int attr_test(char *s, char *e, Node *n) {
    char *eq = top_char(s, e, '=');
    char op = 0;
    char *name = s;
    int nl;
    char *val = 0;
    int vl = 0;
    if (eq) {
        nl = (int)(eq - name);
        if (nl > 1 && (eq[-1] == '^' || eq[-1] == '$' || eq[-1] == '*' ||
                       eq[-1] == '~' || eq[-1] == '|')) {
            op = eq[-1];
            nl--;
        }
        val = skip_ws(eq + 1, e);
        vl = (int)(e - val);
        while (vl && ISWS(val[vl - 1])) vl--;
        if (vl >= 2 && (*val == '"' || *val == '\'') && val[vl - 1] == *val) {
            val++;
            vl -= 2;
        }
    } else {
        nl = (int)(e - name);
    }
    while (nl && ISWS(name[nl - 1])) nl--;
    const char *v = 0;
    for (int i = 0; i < n->nattr; i++)
        if ((int)strlen(n->attrs[i].k) == nl && !strncasecmp(n->attrs[i].k, name, (size_t)nl)) {
            v = n->attrs[i].v;
            break;
        }
    if (!v) return 0;
    if (!op && !val) return 1;
    int sl = (int)strlen(v);
    switch (op) {
        case 0:  return sl == vl && !memcmp(v, val, (size_t)vl);
        case '*': return vl && strstr(v, val) != 0;
        case '^': return sl >= vl && !memcmp(v, val, (size_t)vl);
        case '$': return sl >= vl && !memcmp(v + sl - vl, val, (size_t)vl);
        case '~': return cls_has(v, val, vl);
        case '|': return (sl == vl && !memcmp(v, val, (size_t)vl)) ||
                         (sl > vl + 1 && v[vl] == '-' && !memcmp(v, val, (size_t)vl));
    }
    return 0;
}

static int match_compound(const char *c, Node *n) {
    if (!c || n->tag[0] == '#') return -1;
    int ids = 0, cls = 0, tags = 0;
    const char *p = c;
    while (*p) {
        if (ISWS(*p)) { p++; continue; }
        if (*p == '\\') { p += 2; continue; }
        if (*p == '*') {
            p++;
            if (*p == '|') p++;
            continue;
        }
        if (*p == '|') { p++; continue; }
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *s = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '%')) p++;
            if (n->taglen != (int)(p - s) || strncasecmp(n->tag, s, (size_t)(p - s))) return -1;
            tags++;
            continue;
        }
        if (*p == '.' || *p == '#') {
            char want = *p++;
            const char *s = p;
            while (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':' && !ISWS(*p)) p++;
            int l = (int)(p - s);
            if (want == '.') {
                const char *cv = class_attr(n);
                if (!cv || !cls_has(cv, s, l)) return -1;
                cls++;
            } else {
                const char *v = attr_get(n, "id");
                if (!v || (int)strlen(v) != l || memcmp(v, s, (size_t)l)) return -1;
                ids++;
            }
            continue;
        }
        if (*p == '[') {
            const char *s = ++p;
            char qq = 0;
            while (*p) {
                if (qq) {
                    if (*p == qq) qq = 0;
                } else if (*p == '"' || *p == '\'') qq = *p;
                else if (*p == ']') break;
                p++;
            }
            if (!attr_test((char *)s, (char *)p, n)) return -1;
            if (*p == ']') p++;
            cls++;
            continue;
        }
        if (*p == ':') {
            int dbl = p[1] == ':';
            p += dbl ? 2 : 1;
            const char *s = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) p++;
            int nl = (int)(p - s);
            const char *arg = 0;
            int al = 0;
            if (*p == '(') {
                char *cl = close_paren((char *)p, (char *)p + (int)strlen(p));
                arg = p + 1;
                al = cl ? (int)(cl - arg) : (int)strlen(p + 1);
                p = cl ? cl + 1 : p + strlen(p);
            }
            if (dbl) return -1;
            if (!pseudo(s, nl, arg, al, n)) return -1;
            int sp = pseudo_spec(s, nl, arg, al);
            ids += sp >> 16;
            cls += (sp >> 8) & 0xff;
            tags += sp & 0xff;
            continue;
        }
        p++;
    }
    return pack_spec(ids, cls, tags);
}

int match_selector(const Rule *r, Node *n) {
    if (!r->np) return -1;
    if (r->media && !media_eval(r->media)) return -1;
    int s0 = match_compound(r->ps[r->np - 1], n);
    if (s0 < 0) return -1;
    int ids = s0 >> 16, cls = (s0 >> 8) & 0xff, tags = s0 & 0xff;
    Node *m = n;
    for (int k = r->np - 2; k >= 0; k--) {
        const char *part = r->ps[k];
        Node *hit = 0;
        int s = 0;
        switch (r->sep[k + 1]) {
            case '>':
                if (!m->parent) return -1;
                s = match_compound(part, m->parent);
                if (s < 0) return -1;
                hit = m->parent;
                break;
            case '+': case '~': {
                Node *q = m->parent;
                if (!q) return -1;
                int mi = -1;
                for (int i = 0; i < q->nchild; i++)
                    if (q->child[i] == m) { mi = i; break; }
                if (mi < 0) return -1;
                for (int i = mi - 1; i >= 0; i--) {
                    Node *sib = q->child[i];
                    if (sib->tag[0] == '#') continue;
                    s = match_compound(part, sib);
                    if (s >= 0) { hit = sib; break; }
                    if (r->sep[k + 1] == '+') return -1;
                }
                if (!hit) return -1;
                break;
            }
            default: {
                Node *q = m->parent;
                while (q && (s = match_compound(part, q)) < 0) q = q->parent;
                if (!q) return -1;
                hit = q;
                break;
            }
        }
        ids += s >> 16;
        cls += (s >> 8) & 0xff;
        tags += s & 0xff;
        m = hit;
    }
    return pack_spec(ids, cls, tags);
}

static void apply_decls(Node *n, const Decl *d, int nd, int spec) {
    for (int i = 0; i < nd; i++) {
        const Prop *p = d[i].p;
        const char *vv = d[i].v;
        char *ev = var_expand(vv);
        if (ev) vv = ev;
        int sp = d[i].imp ? (1 << 25) | (spec & ((1 << 25) - 1)) : spec;
        int j = 0;
        while (j < n->nst && n->st[j].p != p) j++;
        if (j == n->nst) st_push(n, p, vv, sp);
        else if (!n->st[j].v || sp >= n->st[j].spec) {
            n->st[j].v = vv;
            n->st[j].spec = sp;
        }
    }
}

static int *CAND;
static int NCAND, CCAP;

static void cand_push(const int *r, int n) {
    for (int i = 0; i < n; i++) {
        int j = 0;
        while (j < NCAND && CAND[j] != r[i]) j++;
        if (j == NCAND) {
            GROW(CAND, NCAND, CCAP, int);
            CAND[NCAND++] = r[i];
        }
    }
}

void apply_styles(Node *n) {
    if (n->tag[0] != '#') {
        NCAND = 0;
        int *rl;
        int rn = bkt_get(n->tagpk << 2, &rl);
        cand_push(rl, rn);
        for (int i = 0; i < n->nattr; i++) {
            if (pk(n->attrs[i].k) == K_CLASS) {
                const char *cv = n->attrs[i].v;
                char buf[64];
                int bl = 0;
                for (int ci = 0;; ci++) {
                    char ch = cv[ci];
                    if (ch && !ISWS(ch)) {
                        if (bl < 63) buf[bl++] = ch;
                        continue;
                    }
                    buf[bl] = 0;
                    if (bl) {
                        rn = bkt_get((pk(buf) << 2) | (uint64_t)1, &rl);
                        cand_push(rl, rn);
                    }
                    bl = 0;
                    if (!ch) break;
                }
            } else if (!strcmp(n->attrs[i].k, "id")) {
                rn = bkt_get((pk(n->attrs[i].v) << 2) | (uint64_t)2, &rl);
                cand_push(rl, rn);
            }
        }
        cand_push(GEN, NGEN);
        for (int i = 0; i < NCAND; i++) {
            Rule *r = R + CAND[i];
            if (r->pe) continue;
            int spec = match_selector(r, n);
            if (spec >= 0) apply_decls(n, r->d, r->nd, spec);
        }
        for (int i = 0; i < n->nattr; i++)
            if (!strcmp(n->attrs[i].k, "style")) {
                char *v = n->attrs[i].v;
                int nd;
                Decl *d = decl_parse(v, v + strlen(v), &nd);
                apply_decls(n, d, nd, 1 << 24);
                free(d);
            }
        if (NPE) {
            gen_apply(n, PE_BEFORE);
            gen_apply(n, PE_AFTER);
        }
    }
    for (int c = 0; c < n->nchild; c++) {
        Node *ch = n->child[c];
        if (ch->tag[0] == '#') continue;
        for (int i = 0; i < n->nst; i++) {
            const Prop *p = n->st[i].p;
            if (!(p->f & P_INH)) continue;
            int j = 0;
            while (j < ch->nst && ch->st[j].p != p) j++;
            if (j == ch->nst) st_push(ch, p, n->st[i].v, -1);
        }
        apply_styles(ch);
    }
}

Node **QL;
int NQL;
static int QCAP;

static void qpush(Node *n) {
    for (int i = 0; i < NQL; i++) if (QL[i] == n) return;
    GROW(QL, NQL, QCAP, Node *);
    QL[NQL++] = n;
}

static void qsel(Node *n, Rule *r) {
    for (int i = 0; i < n->nchild; i++) {
        Node *c = n->child[i];
        if (c->tag[0] != '#' && !r->pe && match_selector(r, c) >= 0) qpush(c);
        qsel(c, r);
    }
}

int qquery_at(Node *root, const char *sel, size_t sl) {
    NQL = 0;
    char *tmp = sdup(sel, sl);
    char *p = tmp;
    while (p && *p) {
        char *cm = top_comma(p, p + strlen(p));
        char *seg = cut(p, cm ? cm : p + strlen(p));
        if (*seg) {
            Rule r = {0};
            presplit(&r, seg);
            qsel(root, &r);
            rule_free(&r);
        }
        p = cm ? cm + 1 : 0;
    }
    free(tmp);
    return NQL;
}

int qquery(const char *sel, size_t sl) {
    return qquery_at(DOM, sel, sl);
}

typedef struct { char *b; int n, cap; } CSB;

static void csb_add(CSB *s, const char *p, int n) {
    if (s->n + n + 1 > s->cap) {
        s->cap = s->cap ? s->cap : 4096;
        while (s->cap < s->n + n + 1) s->cap <<= 1;
        char *b = realloc(s->b, (size_t)s->cap);
        if (!b) oom();
        s->b = b;
    }
    if (n > 0) {
        memcpy(s->b + s->n, p, (size_t)n);
        s->n += n;
    }
    s->b[s->n] = 0;
}

static char *css_slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    if (fl < 0) {
        fclose(f);
        return 0;
    }
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)fl + 9);
    if (!b) oom();
    size_t got = fread(b, 1, (size_t)fl, f);
    if (ferror(f)) {
        fclose(f);
        free(b);
        return 0;
    }
    fclose(f);
    b[got] = 0;
    *n = got;
    return b;
}

static void css_fetch_into(CSB *s, const char *page, const char *href) {
    char *u;
    if (url_is(href) || !page) u = sdup(href, strlen(href));
    else if (url_is(page)) u = url_join(page, href);
    else {
        const char *sl = page ? strrchr(page, '/') : 0;
        size_t dl = sl && sl > page ? (size_t)(sl - page) : 0;
        u = malloc(dl + strlen(href) + 2);
        if (!u) oom();
        memcpy(u, page, dl);
        if (dl) u[dl++] = '/';
        memcpy(u + dl, href, strlen(href) + 1);
    }
    size_t n = 0;
    char *b = url_is(u) ? http_get(u, &n) : css_slurp(u, &n);
    free(u);
    if (!b) return;
    csb_add(s, b, (int)n);
    csb_add(s, "\n", 1);
    free(b);
}

static void css_gather(Node *n, CSB *s, const char *page) {
    if (n->taglen == 5 && n->tagpk == K5('s', 't', 'y', 'l', 'e')) {
        if (n->nchild && (n->child[0]->def->f & T_TEXTN)) {
            csb_add(s, n->child[0]->text, n->child[0]->tlen);
            csb_add(s, "\n", 1);
        }
    } else if (n->taglen == 4 && n->tagpk == K4('l', 'i', 'n', 'k')) {
        char *rel = attr_get(n, "rel");
        char *href = attr_get(n, "href");
        if (rel && strstr(rel, "style") && href && *href) css_fetch_into(s, page, href);
    }
    for (int i = 0; i < n->nchild; i++)
        if (n->child[i]->tag[0] != '#') css_gather(n->child[i], s, page);
}

char *css_collect(Node *root, const char *page) {
    CSB s = {0};
    css_gather(root, &s, page);
    csb_add(&s, "", 0);
    return s.b;
}

static Node *gen_child(Node *n, int which) {
    for (int i = 0; i < n->nchild; i++)
        if (n->child[i]->gen == which) return n->child[i];
    return 0;
}

static Node *gen_make(Node *n, int which) {
    Node *t = node(N_TEXT);
    t->gen = (uint8_t)which;
    t->text = sdup("", 0);
    t->tlen = 0;
    if (which == PE_BEFORE && n->nchild) dom_insert_before(n, t, n->child[0]);
    else dom_append(n, t);
    return t;
}

static void gen_drop(Node *g) {
    dom_remove(g);
    free(g->text);
    g->text = 0;
    g->tlen = 0;
    g->nst = 0;
    g->gen = 0;
}

static const char *css_hex(const char *p, const char *e, unsigned *cp) {
    unsigned v = 0;
    int n = 0;
    while (p < e && n < 6 && isxdigit((unsigned char)*p)) {
        char c = (char)tolower((unsigned char)*p);
        v = v * 16 + (c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10));
        p++;
        n++;
    }
    if (p < e && ISWS(*p)) p++;
    *cp = v;
    return p;
}

static void content_attr(CSB *s, Node *el, const char *q, int al) {
    char nm[64];
    int nl = 0;
    while (nl < al && nl < 63 && !ISWS(q[nl]) && q[nl] != '"' && q[nl] != '\'') {
        nm[nl] = (char)tolower((unsigned char)q[nl]);
        nl++;
    }
    nm[nl] = 0;
    const char *v = nl ? attr_get(el, nm) : 0;
    if (v) csb_add(s, v, (int)strlen(v));
}

static char *content_str(Node *el, const char *v) {
    if (!v) return 0;
    CSB s = {0};
    const char *p = v, *e = v + strlen(v);
    while (p < e) {
        while (p < e && ISWS(*p)) p++;
        if (p >= e) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (p < e && *p != q) {
                if (*p != '\\' || p + 1 >= e) { csb_add(&s, p, 1); p++; continue; }
                p++;
                if (*p == '\n') { p++; continue; }
                if (isxdigit((unsigned char)*p)) {
                    unsigned cp;
                    p = css_hex(p, e, &cp);
                    if (!cp || cp > 0x10FFFF) cp = 0xFFFD;
                    char w[4];
                    csb_add(&s, w, utf8_put(w, cp));
                    continue;
                }
                csb_add(&s, p, 1);
                p++;
            }
            if (p < e) p++;
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '-') {
            const char *w = p;
            while (p < e && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) p++;
            int wl = (int)(p - w);
            while (p < e && ISWS(*p)) p++;
            if (p >= e || *p != '(') {
                if (span_is(w, wl, "none") || span_is(w, wl, "normal")) goto none;
                continue;
            }
            const char *q = ++p;
            int dep = 1;
            while (p < e && dep) {
                if (*p == '(') dep++;
                else if (*p == ')') dep--;
                if (dep) p++;
            }
            int al = (int)(p - q);
            if (p < e && *p == ')') p++;
            if (span_is(w, wl, "attr")) content_attr(&s, el, q, al);
            continue;
        }
        p++;
    }
    if (!s.n) goto none;
    return s.b;
none:
    free(s.b);
    return 0;
}

static const char *gen_prop(Node *g, const Prop *p) {
    for (int i = 0; i < g->nst; i++)
        if (g->st[i].p == p) return g->st[i].v;
    return 0;
}

static void gen_apply(Node *n, int which) {
    Node *g = gen_child(n, which);
    if (g) g->nst = 0;
    int hit = 0;
    for (int i = 0; i < NCAND; i++) {
        Rule *r = R + CAND[i];
        if (r->pe != which) continue;
        int spec = match_selector(r, n);
        if (spec < 0) continue;
        if (!g) g = gen_make(n, which);
        apply_decls(g, r->d, r->nd, spec);
        hit = 1;
    }
    if (!hit) {
        if (g) gen_drop(g);
        return;
    }
    const char *dv = gen_prop(g, &P_DISPLAY);
    if (dv && !strcmp(dv, "none")) {
        gen_drop(g);
        return;
    }
    char *txt = content_str(n, gen_prop(g, &P_CONTENT));
    int tl = txt ? (int)strlen(txt) : 0;
    if (!tl) {
        free(txt);
        gen_drop(g);
        return;
    }
    if (g->tlen != tl || memcmp(g->text, txt, (size_t)tl)) {
        free(g->text);
        g->text = txt;
        g->tlen = tl;
    } else free(txt);
}

static void app(char *o, int n, int *w, const char *f, ...) {
    if (*w >= n - 1) return;
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(o + *w, (size_t)(n - *w), f, ap);
    va_end(ap);
    if (r > 0) *w += r;
    if (*w > n - 1) *w = n - 1;
}

int rule_sel(const Rule *r, char *out, int n) {
    int w = 0;
    if (n < 1) return 0;
    out[0] = 0;
    if (r->media) app(out, n, &w, "@media %s ", r->media);
    for (int i = 0; i < r->np; i++) {
        if (i) app(out, n, &w, r->sep[i] == ' ' ? " " : " %c ", r->sep[i]);
        app(out, n, &w, "%s", r->ps[i]);
    }
    if (r->pe == PE_BEFORE) app(out, n, &w, "::before");
    else if (r->pe == PE_AFTER) app(out, n, &w, "::after");
    else if (r->pe == PE_UNSUP) app(out, n, &w, "::unsupported");
    return w;
}
