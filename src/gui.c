#include "peek.h"
#include <gtk/gtk.h>
#include <cairo.h>

typedef struct {
    GdkRGBA col, bg;
    double fsz, lh;
    int bold, ital, und, strike, up, lo, center, right, just;
    int fszset, bgset, pad, wid, mt, ms, mauto;
} FS;

typedef struct {
    GtkBox *box;
    GString *m;
    FS fs;
} Para;

static GtkWidget *WIN, *CONTENT, *ENTRY, *CANVAS, *OVER;
static Node *FOCN;
static int FOCP;
static GtkWidget *REFCUS;
static char *CURURL;
static int LOADF;
static char *HIST[32];
static int NH, ASEEN, LOGSEEN;

static void ptext(Para *P);
static void widget_button(Node *n, GtkBox *box, int ctr);
static void widget_img(Node *n, GtkBox *box, int ctr);
static void widget_input(Node *n, GtkBox *box, int ctr);
static void submit_form(Node *n);
static void build_tag(Node *n, GtkBox *box, int depth);
static void build_block(Node *n, GtkBox *box, FS base, int depth);
static gboolean nav_link(GtkLabel *l, char *uri, gpointer u);
static void on_click(GtkButton *b, gpointer u);
static void reload(void);
static void nav(const char *href);
int load_page(const char *u);

static const char *st_find(Node *n, const Prop *p) {
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p == p) return n->st[i].v;
    return 0;
}

static int hidden_css(Node *n) {
    const char *v = st_find(n, &P_DISPLAY);
    return v && !strcmp(v, "none");
}

static int css_rgb(const char *v, GdkRGBA *c) {
    int n = ansi_color(v);
    if (n < 0) return 0;
    unsigned r, g, b;
    if (n < 16) {
        static const unsigned B[16] = {0x000000, 0x800000, 0x008000, 0x808000,
                                       0x000080, 0x800080, 0x008080, 0xc0c0c0,
                                       0x808080, 0xff0000, 0x00ff00, 0xffff00,
                                       0x0000ff, 0xff00ff, 0x00ffff, 0xffffff};
        r = B[n] >> 16, g = B[n] >> 8 & 0xff, b = B[n] & 0xff;
    } else if (n < 232) {
        static const unsigned char L[6] = {0, 95, 135, 175, 215, 255};
        n -= 16;
        r = L[n / 36], g = L[n / 36 % 36], b = L[n % 36];
    } else {
        r = g = b = 8 + (n - 232) * 10;
    }
    c->red = r / 255.0;
    c->green = g / 255.0;
    c->blue = b / 255.0;
    c->alpha = 1;
    return 1;
}

static FS fs0(void) {
    FS s;
    memset(&s, 0, sizeof s);
    s.col.red = s.col.green = s.col.blue = 0.9;
    s.col.alpha = 1;
    s.fsz = 14;
    s.fszset = 1;
    return s;
}

static double css_px(const char *v, double base) {
    char *e;
    double x = strtod(v, &e);
    if (e == v) return 0;
    if (*e == '%') return base * x / 100;
    if (*e == 'e') return base * x;
    if (*e == 'p' && e[1] == 't') return x * 96.0 / 72;
    return x;
}

static void box4(const char *v, int *t, int *r, int *b, int *l, int *la, double base) {
    *t = *r = *b = *l = 0;
    *la = 0;
    if (!v || !*v) return;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", v);
    char *tok[4] = {0};
    int nt = 0;
    for (char *p = strtok(tmp, " \t"); p && nt < 4; p = strtok(0, " \t"))
        tok[nt++] = p;
    if (!nt) return;
    int vals[4];
    for (int i = 0; i < 4; i++) {
        const char *s = nt == 1 ? tok[0]
                      : nt == 2 ? tok[i < 2 ? 0 : 1]
                      : nt == 3 ? tok[i == 0 ? 0 : i == 3 ? 2 : 1]
                      : tok[i];
        if (!strcmp(s, "auto")) vals[i] = -1;
        else vals[i] = (int)css_px(s, base);
    }
    *t = vals[0];
    *r = vals[1];
    *b = vals[2];
    *l = vals[3];
    if (nt == 2) {
        *b = vals[0];
        *l = vals[1];
    }
    if (nt == 3) *l = *r = vals[1];
    *la = *l == -1 || *r == -1;
}

typedef struct {
    int w, hascol, hasst;
    GdkRGBA col;
    char st[10];
} Brd;

static void brd_parse(const char *v, Brd *o) {
    memset(o, 0, sizeof *o);
    o->w = -1;
    if (!v || !*v) return;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", v);
    for (char *p = strtok(tmp, " \t"); p; p = strtok(0, " \t")) {
        if ((p[0] >= '0' && p[0] <= '9') && o->w < 0) {
            o->w = (int)css_px(p, 0);
            continue;
        }
        if (!strcmp(p, "solid") || !strcmp(p, "dashed") || !strcmp(p, "dotted") ||
            !strcmp(p, "double") || !strcmp(p, "none")) {
            snprintf(o->st, sizeof o->st, "%s", p);
            o->hasst = 1;
            continue;
        }
        GdkRGBA c;
        if (css_rgb(p, &c)) {
            o->col = c;
            o->hascol = 1;
        }
    }
    if (o->w < 0) o->w = 1;
    if (!o->hasst) strcpy(o->st, "solid");
    if (!o->hascol) o->col.red = o->col.green = o->col.blue = 0.75, o->col.alpha = 1;
}

static void widget_css(GtkWidget *w, const char *body) {
    if (!body || !*body) return;
    char css[512];
    snprintf(css, sizeof css, "* { %s }", body);
    GtkCssProvider *p = gtk_css_provider_new();
    if (gtk_css_provider_load_from_data(p, css, -1, 0)) {
        gtk_style_context_add_provider(gtk_widget_get_style_context(w),
                                       GTK_STYLE_PROVIDER(p),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(p);
}

static void rgba_css(GdkRGBA *c, char *out, size_t n) {
    snprintf(out, n, "rgba(%d,%d,%d,%g)", (int)(c->red * 255), (int)(c->green * 255),
             (int)(c->blue * 255), c->alpha);
}

static void apply_box(Node *n, GtkWidget *w, double base) {
    const char *v;
    int t, r, b, l, la;
    if ((v = st_find(n, &P_MARGIN))) {
        box4(v, &t, &r, &b, &l, &la, 800);
        if (t > 0) gtk_widget_set_margin_top(w, t);
        if (b > 0) gtk_widget_set_margin_bottom(w, b);
        if (l > 0) gtk_widget_set_margin_start(w, l);
        if (r > 0) gtk_widget_set_margin_end(w, r);
        if (la) gtk_widget_set_halign(w, GTK_ALIGN_CENTER);
    }
    if ((v = st_find(n, &P_MINW))) {
        int m = (int)css_px(v, 800);
        if (m > 0) {
            int cw, ch;
            gtk_widget_get_size_request(w, &cw, &ch);
            gtk_widget_set_size_request(w, m, ch);
        }
    }
    GString *cs = g_string_new(0);
    if ((v = st_find(n, &P_PAD))) {
        box4(v, &t, &r, &b, &l, &la, 800);
        g_string_append_printf(cs, "padding:%dpx %dpx %dpx %dpx;", t, r, b, l);
    }
    Brd bd;
    int hasbd = 0;
    if ((v = st_find(n, &P_BORDER))) {
        brd_parse(v, &bd);
        if (strcmp(bd.st, "none")) hasbd = 1;
    } else if ((v = st_find(n, &P_BW))) {
        int bw = (int)css_px(v, 0);
        if (bw > 0) {
            bd.w = bw;
            strcpy(bd.st, "solid");
            bd.col.red = bd.col.green = bd.col.blue = 0.75;
            bd.col.alpha = 1;
            hasbd = 1;
        }
    }
    if (hasbd) {
        char colb[32];
        rgba_css(&bd.col, colb, sizeof colb);
        g_string_append_printf(cs, "border:%dpx %s %s;", bd.w, bd.st, colb);
    }
    if ((v = st_find(n, &P_RADIUS))) {
        double rd = css_px(v, 0);
        if (rd > 0) g_string_append_printf(cs, "border-radius:%gpx;", rd);
    }
    if (cs->len) widget_css(w, cs->str);
    g_string_free(cs, TRUE);
}

static void apply_fs(Node *n, FS *s) {
    const char *v;
    GdkRGBA c;
    if ((v = st_find(n, &P_COLOR)) && css_rgb(v, &c)) s->col = c;
    if ((v = st_find(n, &P_BG)) && css_rgb(v, &c)) s->bg = c, s->bgset = 1;
    else if ((v = st_find(n, &P_BGS))) {
        char col[32];
        if (bg_first_color(v, col) && css_rgb(col, &c)) s->bg = c, s->bgset = 1;
    }
    if ((v = st_find(n, &P_FSIZE))) {
        double f = css_px(v, s->fsz);
        if (f > 1) s->fsz = f, s->fszset = 1;
    }
    if ((v = st_find(n, &P_WEIGHT)) && strstr(v, "bold")) s->bold = 1;
    if ((v = st_find(n, &P_FS)) && strstr(v, "italic")) s->ital = 1;
    if ((v = st_find(n, &P_DECO))) {
        if (strstr(v, "underline")) s->und = 1;
        if (strstr(v, "line-through")) s->strike = 1;
    }
    if ((v = st_find(n, &P_TRANS))) {
        if (strstr(v, "uppercase")) s->up = 1, s->lo = 0;
        if (strstr(v, "lowercase")) s->lo = 1, s->up = 0;
    }
    if ((v = st_find(n, &P_ALIGN))) {
        if (strstr(v, "center")) s->center = 1;
        else if (strstr(v, "right") || strstr(v, "end")) s->right = 1;
        else if (strstr(v, "justify")) s->just = 1;
    }
    if ((v = st_find(n, &P_LH))) {
        double x = strtod(v, 0);
        if (*v && x > 0) s->lh = strchr(v, 'p') ? x / s->fsz : x;
    }
    if ((v = st_find(n, &P_PAD))) s->pad = (int)css_px(v, s->fsz);
    if ((v = st_find(n, &P_WIDTH))) s->wid = (int)css_px(v, 0);
    if ((v = st_find(n, &P_MARGIN))) {
        char *e, *e2;
        double a = strtod(v, &e);
        if (e != v) {
            s->mt = (int)a;
            while (*e == ' ') e++;
            if (!strncmp(e, "auto", 4)) s->mauto = 1;
            else {
                double b = strtod(e, &e2);
                s->ms = e2 > e ? (int)b : (int)a;
            }
        }
    }
}

static void esc(GString *g, const char *t, int n, int up, int lo) {
    for (int i = 0; i < n; i++) {
        unsigned char c = t[i];
        if (up && c < 128) c = (unsigned char)toupper(c);
        else if (lo && c < 128) c = (unsigned char)tolower(c);
        if (c == '&') g_string_append(g, "&amp;");
        else if (c == '<') g_string_append(g, "&lt;");
        else if (c == '>') g_string_append(g, "&gt;");
        else g_string_append_c(g, c);
    }
}

static void esc_attr(GString *g, const char *s) {
    for (; *s; s++) {
        if (*s == '&') g_string_append(g, "&amp;");
        else if (*s == '<') g_string_append(g, "&lt;");
        else if (*s == '"') g_string_append(g, "&quot;");
        else g_string_append_c(g, *s);
    }
}

static void ptext(Para *P) {
    if (!P->m || !P->m->len) {
        if (P->m) {
            g_string_free(P->m, TRUE);
            P->m = 0;
        }
        return;
    }
    for (;;) {
        char *s = P->m->str, *last = 0;
        for (char *q = strstr(s, "<a "); q; q = strstr(q + 3, "<a ")) last = q;
        if (!last || strstr(last + 3, "</a>")) break;
        char *e = strchr(last, '>');
        if (!e) break;
        g_string_erase(P->m, (gssize)(last - s), (gssize)(e - last + 1));
    }
    if (!P->m->len || !P->box) {
        g_string_free(P->m, TRUE);
        P->m = 0;
        return;
    }
    GtkWidget *l = gtk_label_new(NULL);
    if (P->fs.lh > 0.01) {
        char pre[40];
        snprintf(pre, sizeof pre, "<span line_height=\"%g\">", P->fs.lh);
        g_string_prepend(P->m, pre);
        g_string_append(P->m, "</span>");
    }
    gtk_label_set_markup(GTK_LABEL(l), P->m->str);
    gtk_label_set_selectable(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), P->fs.center ? 0.5 : P->fs.right ? 1 : 0);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
    if (P->fs.just) gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_FILL);
    gtk_widget_set_halign(l, P->fs.center ? GTK_ALIGN_CENTER
                            : P->fs.right ? GTK_ALIGN_END
                            : GTK_ALIGN_FILL);
    if (gtk_orientable_get_orientation(GTK_ORIENTABLE(P->box)) == GTK_ORIENTATION_VERTICAL)
        gtk_widget_set_hexpand(l, TRUE);
    g_signal_connect(l, "activate-link", G_CALLBACK(nav_link), 0);
    gtk_box_pack_start(P->box, l, FALSE, FALSE, 0);
    g_string_free(P->m, TRUE);
    P->m = 0;
}

static void run_text(Node *n, Para *P) {
    if (!P->m) P->m = g_string_new(0);
    GString *g = P->m;
    FS *s = &P->fs;
    if (s->bold) g_string_append(g, "<b>");
    if (s->ital) g_string_append(g, "<i>");
    g_string_append_printf(g, "<span foreground='#%02x%02x%02x'",
                           (int)(s->col.red * 255 + .5), (int)(s->col.green * 255 + .5),
                           (int)(s->col.blue * 255 + .5));
    if (s->bgset)
        g_string_append_printf(g, " background='#%02x%02x%02x'",
                               (int)(s->bg.red * 255 + .5), (int)(s->bg.green * 255 + .5),
                               (int)(s->bg.blue * 255 + .5));
    if (s->fszset) g_string_append_printf(g, " size='%d'", (int)(s->fsz * 768));
    if (s->und) g_string_append(g, " underline='single'");
    if (s->strike) g_string_append(g, " strikethrough='true'");
    g_string_append(g, ">");
    esc(g, n->text, n->tlen, s->up, s->lo);
    g_string_append(g, "</span>");
    if (s->ital) g_string_append(g, "</i>");
    if (s->bold) g_string_append(g, "</b>");
}

static void flow(Node *n, Para *P, int depth) {
    if (n->def->f & T_TEXTN) {
        if (n->gen) {
            FS save = P->fs;
            apply_fs(n, &P->fs);
            run_text(n, P);
            P->fs = save;
        } else run_text(n, P);
        return;
    }
    if ((n->def->f & T_HIDDEN) || hidden_css(n)) return;
    uint64_t k = n->tagpk;
    int tl = n->taglen;
    if (tl == 2 && k == K2('b', 'r')) {
        if (!P->m) P->m = g_string_new(0);
        g_string_append_c(P->m, '\n');
        return;
    }
    if (tl == 1 && k == K_A) {
        if (!P->m) P->m = g_string_new(0);
        char *h = attr_get(n, "href");
        g_string_append(P->m, "<a href=\"");
        if (h) esc_attr(P->m, h);
        g_string_append(P->m, "\">");
        FS save = P->fs;
        apply_fs(n, &P->fs);
        if (!st_find(n, &P_COLOR)) {
            P->fs.col.red = 0;
            P->fs.col.green = 0;
            P->fs.col.blue = 0.933;
        }
        if (!st_find(n, &P_DECO)) P->fs.und = 1;
        for (int i = 0; i < n->nchild; i++) flow(n->child[i], P, depth);
        P->fs = save;
        if (P->m) {
            int na = 0, nc = 0;
            for (char *q = P->m->str; (q = strstr(q, "<a ")); q += 3) na++;
            for (char *q = P->m->str; (q = strstr(q, "</a>")); q += 4) nc++;
            if (na > nc) g_string_append(P->m, "</a>");
        }
        return;
    }
    if (tl == 6 && k == K6('b', 'u', 't', 't', 'o', 'n')) {
        ptext(P);
        widget_button(n, P->box, P->fs.center);
        return;
    }
    if (tl == 3 && k == K3('i', 'm', 'g')) {
        ptext(P);
        widget_img(n, P->box, P->fs.center);
        return;
    }
    if (tl == 5 && k == K5('i', 'n', 'p', 'u', 't')) {
        ptext(P);
        widget_input(n, P->box, P->fs.center);
        return;
    }
    if (n->def->f & T_BLOCK) {
        ptext(P);
        build_tag(n, P->box, depth);
        return;
    }
    FS save = P->fs;
    apply_fs(n, &P->fs);
    for (int i = 0; i < n->nchild; i++) flow(n->child[i], P, depth);
    P->fs = save;
}

static char *urlencode(const char *s) {
    size_t n = 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = *p;
        int safe = (c | 32) >= 'a' && (c | 32) <= 'z';
        safe |= c >= '0' && c <= '9';
        safe |= c == '-' || c == '.' || c == '_' || c == '~';
        n += safe ? 1 : 3;
    }
    char *r = malloc(n + 1);
    if (!r) oom();
    char *w = r;
    static const char *HX = "0123456789ABCDEF";
    for (const char *p = s; *p; p++) {
        unsigned char c = *p;
        int safe = (c | 32) >= 'a' && (c | 32) <= 'z';
        safe |= c >= '0' && c <= '9';
        safe |= c == '-' || c == '.' || c == '_' || c == '~';
        if (safe) *w++ = c;
        else {
            *w++ = '%';
            *w++ = HX[c >> 4];
            *w++ = HX[c & 15];
        }
    }
    *w = 0;
    return r;
}

static Node *form_of(Node *n) {
    for (Node *p = n->parent; p; p = p->parent)
        if (p->taglen == 4 && p->tagpk == K4('f', 'o', 'r', 'm')) return p;
    return 0;
}

static void gather_inputs(Node *n, GString *q) {
    if (n->taglen == 5 && n->tagpk == K5('i', 'n', 'p', 'u', 't')) {
        char *name = attr_get(n, "name");
        char *ty = attr_get(n, "type");
        if (!name || !*name) return;
        if (ty && (!strcmp(ty, "submit") || !strcmp(ty, "button") || !strcmp(ty, "image") || !strcmp(ty, "file"))) return;
        if (ty && !strcmp(ty, "checkbox") && !attr_get(n, "checked")) return;
        char *v = attr_get(n, "value");
        char *ek = urlencode(name), *ev = urlencode(v ? v : "");
        if (q->len) g_string_append_c(q, '&');
        g_string_append_printf(q, "%s=%s", ek, ev);
        free(ek);
        free(ev);
        return;
    }
    if (n->taglen == 8 && n->tagpk == K8('t', 'e', 'x', 't', 'a', 'r', 'e', 'a')) {
        char *name = attr_get(n, "name");
        if (!name || !*name) return;
        char buf[4096];
        collect_text(n, buf, sizeof buf);
        char *ek = urlencode(name), *ev = urlencode(buf);
        if (q->len) g_string_append_c(q, '&');
        g_string_append_printf(q, "%s=%s", ek, ev);
        free(ek);
        free(ev);
        return;
    }
    for (int i = 0; i < n->nchild; i++) gather_inputs(n->child[i], q);
}

static void submit_form(Node *n) {
    Node *f = form_of(n);
    if (!f) return;
    char *action = attr_get(f, "action");
    char *u = action && *action ? url_join(CURURL, action) : sdup(CURURL, strlen(CURURL));
    GString *q = g_string_new(0);
    gather_inputs(f, q);
    GString *url = g_string_new(u);
    if (q->len) g_string_append_printf(url, "%s%s", strchr(u, '?') ? "&" : "?", q->str);
    g_string_free(q, TRUE);
    free(u);
    nav(url->str);
    g_string_free(url, TRUE);
    reload();
}

static void input_changed(GtkEditable *e, gpointer u) {
    Node *n = u;
    attr_set(n, "value", gtk_entry_get_text(GTK_ENTRY(e)));
    js_fire(n, "input");
}

static void input_activate(GtkEntry *e, gpointer u) {
    submit_form((Node *)u);
}

static void widget_button(Node *n, GtkBox *box, int ctr) {
    GtkWidget *b = gtk_button_new();
    Para P = {0, g_string_new(0), fs0()};
    apply_fs(n, &P.fs);
    for (int i = 0; i < n->nchild; i++) flow(n->child[i], &P, 0);
    GtkWidget *l = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l), P.m && P.m->len ? P.m->str : " ");
    gtk_container_add(GTK_CONTAINER(b), l);
    if (P.m) g_string_free(P.m, TRUE);
    g_signal_connect(b, "clicked", G_CALLBACK(on_click), n);
    if (ctr) gtk_widget_set_halign(b, GTK_ALIGN_CENTER);
    if (box) gtk_box_pack_start(box, b, FALSE, FALSE, 0);
}

static void widget_input(Node *n, GtkBox *box, int ctr) {
    char *ty = attr_get(n, "type");
    if (ty && !strcmp(ty, "submit")) {
        char *v = attr_get(n, "value");
        GtkWidget *b = gtk_button_new_with_label(v && *v ? v : "Submit");
        g_signal_connect(b, "clicked", G_CALLBACK(on_click), n);
        if (ctr) gtk_widget_set_halign(b, GTK_ALIGN_CENTER);
        if (box) gtk_box_pack_start(box, b, FALSE, FALSE, 0);
        return;
    }
    GtkWidget *e = gtk_entry_new();
    char *v = attr_get(n, "value");
    if (v && *v) gtk_entry_set_text(GTK_ENTRY(e), v);
    char *ph = attr_get(n, "placeholder");
    if (ph) gtk_entry_set_placeholder_text(GTK_ENTRY(e), ph);
    if (ty && !strcmp(ty, "password")) gtk_entry_set_visibility(GTK_ENTRY(e), FALSE);
    g_object_set_data(G_OBJECT(e), "pk", n);
    if (n == FOCN) {
        REFCUS = e;
        FOCN = 0;
    }
    g_signal_connect(e, "changed", G_CALLBACK(input_changed), n);
    g_signal_connect(e, "activate", G_CALLBACK(input_activate), n);
    if (ctr) gtk_widget_set_halign(e, GTK_ALIGN_CENTER);
    if (box) gtk_box_pack_start(box, e, FALSE, FALSE, 0);
}

static double img_px(const char *v, double cont, double em) {
    if (!v) return 0;
    char *e;
    strtod(v, &e);
    return css_px(v, *e == 'e' ? em : *e == '%' ? cont : 0);
}

static void widget_img(Node *n, GtkBox *box, int ctr) {
    char *src = attr_get(n, "src");
    GdkPixbuf *pb = 0;
    if (src && *src) {
        size_t ln;
        char *b = load_url(src, &ln);
        if (b) {
            GdkPixbufLoader *ld = gdk_pixbuf_loader_new();
            gdk_pixbuf_loader_write(ld, (const guchar *)b, ln, 0);
            gdk_pixbuf_loader_close(ld, 0);
            pb = gdk_pixbuf_loader_get_pixbuf(ld);
            if (pb) g_object_ref(pb);
            g_object_unref(ld);
            free(b);
        }
    }
    GtkWidget *w;
    if (!pb) {
        char *alt = attr_get(n, "alt");
        if (!alt || !*alt) {
            w = gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_BUTTON);
        } else {
            GString *g = g_string_new(0);
            g_string_append(g, "<span foreground='#8a919e' font_style='italic'>");
            esc(g, alt, (int)strlen(alt), 0, 0);
            g_string_append(g, "</span>");
            w = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(w), g->str);
            gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
            gtk_label_set_xalign(GTK_LABEL(w), 0);
            g_string_free(g, TRUE);
        }
    } else {
        int iw = gdk_pixbuf_get_width(pb), ih = gdk_pixbuf_get_height(pb);
        double em = 14, cont = 800;
        const char *fz = st_find(n, &P_FSIZE);
        if (fz) em = css_px(fz, em);
        double tw = img_px(st_find(n, &P_WIDTH), cont, em);
        double th = img_px(st_find(n, &P_HEIGHT), cont, em);
        char *aw = attr_get(n, "width"), *ah = attr_get(n, "height");
        if (!tw && aw && atoi(aw) > 0) tw = atoi(aw);
        if (!th && ah && atoi(ah) > 0) th = atoi(ah);
        int fixw = tw > 0, fixh = th > 0;
        if (fixw != fixh && iw > 0 && ih > 0) {
            if (fixw) th = tw * ih / iw;
            else tw = th * iw / ih;
        } else if (!fixw && !fixh) tw = iw, th = ih;
        double mw = img_px(st_find(n, &P_MAXW), cont, em);
        double mh = img_px(st_find(n, &P_MAXH), cont, em);
        if (mw > 0 && tw > mw) {
            if (!fixh) th *= mw / tw;
            tw = mw;
        }
        if (mh > 0 && th > mh) {
            if (!fixw) tw *= mh / th;
            th = mh;
        }
        int W = (int)(tw + .5), H = (int)(th + .5);
        if (W < 1 || H < 1 || W > 4096 || H > 4096) W = iw, H = ih;
        GdkPixbuf *out = pb;
        if (W > 0 && H > 0 && (W != iw || H != ih))
            out = gdk_pixbuf_scale_simple(pb, W, H, GDK_INTERP_BILINEAR);
        w = gtk_image_new_from_pixbuf(out);
        g_object_unref(pb);
        if (out != pb) g_object_unref(out);
    }
    if (ctr) gtk_widget_set_halign(w, GTK_ALIGN_CENTER);
    if (box) gtk_box_pack_start(box, w, FALSE, FALSE, 0);
}

static GtkWidget *vbox(int sp) {
    return gtk_box_new(GTK_ORIENTATION_VERTICAL, sp);
}

static void build_li(Node *n, GtkBox *box, uint64_t k, int idx, int depth) {
    GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    gtk_widget_set_hexpand(GTK_WIDGET(row), TRUE);
    gtk_box_pack_start(box, GTK_WIDGET(row), FALSE, FALSE, 0);
    GString *m = g_string_new(0);
    if (k == K2('o', 'l')) g_string_append_printf(m, "<span foreground='#7aa2f7'>%d.</span>", idx);
    else g_string_append(m, "<span foreground='#7aa2f7'>\xe2\x80\xa2</span>");
    GtkWidget *bl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(bl), m->str);
    gtk_label_set_xalign(GTK_LABEL(bl), 0);
    g_string_free(m, TRUE);
    gtk_box_pack_start(row, bl, FALSE, FALSE, 0);
    GtkBox *body = GTK_BOX(vbox(6));
    gtk_box_pack_start(row, GTK_WIDGET(body), TRUE, TRUE, 0);
    build_block(n, body, fs0(), depth + 1);
}

static void build_list(Node *n, GtkBox *box, uint64_t k, int depth) {
    GtkBox *v = GTK_BOX(vbox(3));
    gtk_box_pack_start(box, GTK_WIDGET(v), FALSE, FALSE, 0);
    int idx = 1;
    for (int i = 0; i < n->nchild; i++) {
        Node *c = n->child[i];
        if (c->taglen == 2 && c->tagpk == K2('l', 'i'))
            build_li(c, v, k, idx++, depth);
        else if (!(c->def->f & T_HIDDEN))
            build_tag(c, v, depth + 1);
    }
}

static void build_table(Node *n, GtkBox *box, int depth) {
    GtkBox *v = GTK_BOX(vbox(2));
    gtk_box_pack_start(box, GTK_WIDGET(v), FALSE, FALSE, 0);
    for (int i = 0; i < n->nchild; i++) {
        Node *r = n->child[i];
        if (r->taglen == 5 && (r->tagpk == K5('t', 'b', 'o', 'd', 'y') || r->tagpk == K5('t', 'h', 'e', 'a', 'd') || r->tagpk == K5('t', 'f', 'o', 'o', 't'))) {
            build_table(r, v, depth + 1);
            continue;
        }
        if (!(r->taglen == 2 && r->tagpk == K2('t', 'r'))) continue;
        GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24));
        gtk_widget_set_hexpand(GTK_WIDGET(row), TRUE);
        gtk_box_pack_start(v, GTK_WIDGET(row), FALSE, FALSE, 0);
        for (int j = 0; j < r->nchild; j++) {
            Node *d = r->child[j];
            GtkBox *cd = GTK_BOX(vbox(2));
            gtk_box_pack_start(row, GTK_WIDGET(cd), TRUE, TRUE, 0);
            if (d->taglen == 2 && (d->tagpk == K2('t', 'd') || d->tagpk == K2('t', 'h')))
                build_block(d, cd, fs0(), depth + 1);
        }
    }
}

static void build_tag(Node *n, GtkBox *box, int depth) {
    if (depth > 48) return;
    if (hidden_css(n)) return;
    uint64_t k = n->tagpk;
    int tl = n->taglen;
    if (tl == 2 && k == K2('h', 'r')) {
        GtkWidget *sp = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_hexpand(sp, TRUE);
        gtk_box_pack_start(box, sp, FALSE, FALSE, 6);
        return;
    }
    if (tl == 3 && k == K3('i', 'm', 'g')) return widget_img(n, box, 0);
    if (tl == 6 && k == K6('b', 'u', 't', 't', 'o', 'n')) return widget_button(n, box, 0);
    if (tl == 5 && k == K5('i', 'n', 'p', 'u', 't')) return widget_input(n, box, 0);
    if (tl == 5 && k == K5('t', 'a', 'b', 'l', 'e')) return build_table(n, box, depth);
    if (tl == 2 && (k == K2('u', 'l') || k == K2('o', 'l'))) return build_list(n, box, k, depth);
    if (tl == 2 && k == K2('l', 'i')) return build_li(n, box, 0, 0, depth);
    FS base = fs0();
    apply_fs(n, &base);
    if (tl == 4 && (k == K4('h', 't', 'm', 'l') || k == K4('b', 'o', 'd', 'y')))
        base.bgset = base.pad = base.wid = 0;
    if (tl == 2 && n->tag[0] == 'h' && n->tag[1] >= '1' && n->tag[1] <= '6') {
        static const double HS[6] = {32, 24, 20, 16, 13, 12};
        base.bold = 1;
        if (!st_find(n, &P_FSIZE)) base.fsz = HS[n->tag[1] - '1'];
    }
    const char *posv = st_find(n, &P_POS);
    int abspos = posv && !strcmp(posv, "absolute") && OVER;
    int relpos = posv && !strcmp(posv, "relative");
    const char *disp = st_find(n, &P_DISPLAY);
    int flexh = 0;
    if (disp && (!strcmp(disp, "flex") || !strcmp(disp, "inline-flex"))) {
        const char *dv = st_find(n, &P_DIR);
        flexh = !(dv && strstr(dv, "column"));
    }
    double gapv = 0;
    const char *gv = st_find(n, &P_GAP);
    if (gv) gapv = css_px(gv, 0);
    GtkBox *v = GTK_BOX(flexh ? gtk_box_new(GTK_ORIENTATION_HORIZONTAL, (gint)gapv)
                              : vbox(6));
    int ms2 = tl == 10 && !memcmp(n->tag, "blockquote", 10) ? 28 : base.ms;
    GtkWidget *host = GTK_WIDGET(v);
    if (base.bgset || base.pad || base.wid ||
        st_find(n, &P_PAD) || st_find(n, &P_BORDER) || st_find(n, &P_BW) ||
        st_find(n, &P_RADIUS)) {
        GtkWidget *ev = gtk_event_box_new();
        if (base.bgset) gtk_widget_override_background_color(ev, GTK_STATE_FLAG_NORMAL, &base.bg);
        if (base.pad) gtk_container_set_border_width(GTK_CONTAINER(ev), (guint)base.pad);
        if (base.wid) gtk_widget_set_size_request(ev, base.wid, -1);
        gtk_container_add(GTK_CONTAINER(ev), GTK_WIDGET(v));
        host = ev;
    }
    apply_box(n, host, base.fsz);
    int justsb = 0;
    if (flexh) {
        const char *jv = st_find(n, &P_JUST);
        if (jv) {
            if (strstr(jv, "space")) justsb = 1;
            else if (strstr(jv, "flex-end") || strstr(jv, "end"))
                gtk_widget_set_halign(host, GTK_ALIGN_END);
            else if (strstr(jv, "center"))
                gtk_widget_set_halign(host, GTK_ALIGN_CENTER);
        }
        const char *av = st_find(n, &P_AI);
        if (av && strstr(av, "center")) gtk_widget_set_valign(host, GTK_ALIGN_CENTER);
    }
    if (relpos) {
        const char *tv = st_find(n, &P_TOP);
        const char *lv = st_find(n, &P_LEFT);
        if (tv) base.mt += (int)css_px(tv, 0);
        if (lv) ms2 += (int)css_px(lv, 0);
    }
    if (abspos) {
        double tv = st_find(n, &P_TOP) ? css_px(st_find(n, &P_TOP), 0) : 0;
        double lv = st_find(n, &P_LEFT) ? css_px(st_find(n, &P_LEFT), 0) : 0;
        gtk_fixed_put(GTK_FIXED(OVER), host, (gint)lv, (gint)tv);
        gtk_widget_show(host);
    } else {
        if (base.mt || ms2 || base.mauto) {
            gtk_widget_set_margin_top(host, base.mt);
            gtk_widget_set_margin_bottom(host, base.mt);
            if (base.mauto) gtk_widget_set_halign(host, GTK_ALIGN_CENTER);
            else if (ms2) {
                gtk_widget_set_margin_start(host, ms2);
                gtk_widget_set_margin_end(host, ms2);
            }
        }
        if (gtk_orientable_get_orientation(GTK_ORIENTABLE(box)) == GTK_ORIENTATION_VERTICAL)
            gtk_widget_set_hexpand(host, TRUE);
        gtk_box_pack_start(box, host, FALSE, FALSE, 0);
    }
    if (flexh && justsb) {
        GtkWidget *sp = gtk_label_new(0);
        gtk_widget_set_hexpand(sp, TRUE);
        gtk_box_pack_start(v, sp, TRUE, TRUE, 0);
    }
    build_block(n, v, base, depth + 1);
    if (flexh && justsb) {
        GtkWidget *sp = gtk_label_new(0);
        gtk_widget_set_hexpand(sp, TRUE);
        gtk_box_pack_start(v, sp, TRUE, TRUE, 0);
    }
}

static void build_block(Node *n, GtkBox *box, FS base, int depth) {
    if (depth > 48) return;
    Para P = {box, 0, base};
    for (int i = 0; i < n->nchild; i++) {
        Node *c = n->child[i];
        if (c->def->f & T_HIDDEN) continue;
        int tab = c->taglen == 2 && (c->tagpk == K2('t', 'r') || c->tagpk == K2('t', 'd') || c->tagpk == K2('t', 'h'));
        if (c->def->f & T_BLOCK || tab || (c->taglen == 5 && c->tagpk == K5('t', 'a', 'b', 'l', 'e'))) {
            ptext(&P);
            build_tag(c, box, depth);
        } else {
            if (!P.m) P.m = g_string_new(0);
            flow(c, &P, depth);
        }
    }
    ptext(&P);
}

static void clear_box(GtkBox *b) {
    GList *c = gtk_container_get_children(GTK_CONTAINER(b));
    for (GList *p = c; p; p = p->next) gtk_widget_destroy(GTK_WIDGET(p->data));
    g_list_free(c);
}

static void clear_fixed(GtkFixed *f) {
    GList *c = gtk_container_get_children(GTK_CONTAINER(f));
    for (GList *p = c; p; p = p->next)
        if (GTK_WIDGET(p->data) != CONTENT) gtk_widget_destroy(GTK_WIDGET(p->data));
    g_list_free(c);
}

static void busy_off(void) {
    if (gtk_widget_get_window(WIN))
        gdk_window_set_cursor(gtk_widget_get_window(WIN), 0);
}

static void busy_on(void) {
    gtk_window_set_title(GTK_WINDOW(WIN), "peek \xe2\x80\x94 loading...");
    static GdkCursor *W;
    if (!W) W = gdk_cursor_new_for_display(gtk_widget_get_display(WIN), GDK_WATCH);
    if (gtk_widget_get_window(WIN))
        gdk_window_set_cursor(gtk_widget_get_window(WIN), W);
    while (gtk_events_pending()) gtk_main_iteration();
}

static void reload(void) {
    busy_off();
    FOCN = 0;
    FOCP = 0;
    REFCUS = 0;
    GtkWidget *fw = gtk_window_get_focus(GTK_WINDOW(WIN));
    if (fw) {
        Node *fn = g_object_get_data(G_OBJECT(fw), "pk");
        if (fn && GTK_IS_EDITABLE(fw)) {
            FOCN = fn;
            FOCP = gtk_editable_get_position(GTK_EDITABLE(fw));
        }
    }
    js_viewport(gtk_widget_get_allocated_width(CANVAS),
                gtk_widget_get_allocated_height(CANVAS));
    apply_styles(DOM);
    clear_fixed(GTK_FIXED(OVER));
    clear_box(GTK_BOX(CONTENT));
    GdkRGBA bg = {1, 1, 1, 1};
    Node *bd = find_tag(DOM, K4('b', 'o', 'd', 'y'));
    const char *bgs = bd ? st_find(bd, &P_BG) : 0;
    if (!bgs) {
        Node *ht = find_tag(DOM, K4('h', 't', 'm', 'l'));
        bgs = ht ? st_find(ht, &P_BG) : 0;
    }
    if (bgs) css_rgb(bgs, &bg);
    gtk_widget_override_background_color(CANVAS, GTK_STATE_FLAG_NORMAL, &bg);
    FS base = fs0();
    if (bd) apply_fs(bd, &base);
    build_block(DOM, GTK_BOX(CONTENT), base, 0);
    gtk_widget_show_all(CONTENT);
    if (REFCUS) {
        gtk_widget_grab_focus(REFCUS);
        gtk_editable_set_position(GTK_EDITABLE(REFCUS), FOCP);
        REFCUS = 0;
    }
    Node *t = find_tag(DOM, K5('t', 'i', 't', 'l', 'e'));
    char buf[256] = "peek";
    if (t && t->nchild) {
        int n = t->child[0]->tlen;
        if (n > 200) n = 200;
        memcpy(buf, t->child[0]->text, n);
        buf[n] = 0;
    }
    gtk_window_set_title(GTK_WINDOW(WIN), buf);
    if (CURURL) gtk_entry_set_text(GTK_ENTRY(ENTRY), CURURL);
    if (!LOADF) {
        LOADF = 1;
        js_win_event("load");
    }
    const char *shot = getenv("PEEK_SHOT");
    if (shot && gtk_widget_get_window(CANVAS)) {
        while (gtk_events_pending()) gtk_main_iteration();
        GdkWindow *gw = gtk_widget_get_window(CANVAS);
        int sw = gdk_window_get_width(gw), sh = gdk_window_get_height(gw);
        GdkPixbuf *px = gdk_pixbuf_get_from_window(gw, 0, 0, sw, sh);
        if (px) {
            gdk_pixbuf_save(px, shot, "png", 0, 0);
            g_object_unref(px);
            fprintf(stderr, "peekg: screenshot saved %s\n", shot);
        } else {
            fprintf(stderr, "peekg: screenshot failed\n");
        }
    }
}

static void on_click(GtkButton *b, gpointer u) {
    (void)b;
    Node *n = u;
    int i = oc_find(n);
    if (i >= 0) {
        js_click(i);
        reload();
        return;
    }
    submit_form(n);
}

static gboolean nav_link(GtkLabel *l, char *uri, gpointer u) {
    (void)l;
    (void)u;
    nav(uri);
    reload();
    return TRUE;
}

static void go_entry(void) {
    const char *t = gtk_entry_get_text(GTK_ENTRY(ENTRY));
    while (*t == ' ') t++;
    if (!*t) return;
    char buf[2100];
    if (!strstr(t, "://")) {
        snprintf(buf, sizeof buf, "http://%s", t);
        t = buf;
    }
    nav(t);
    reload();
}

static void back_cb(void) {
    if (!NH) return;
    char *u = HIST[--NH];
    busy_on();
    load_page(u);
    free(u);
    reload();
}

static gboolean tick(gpointer _) {
    (void)_;
    if (js_pump()) reload();
    while (LOGSEEN < NLOG) fprintf(stderr, "js: %s\n", LOGS[LOGSEEN++]);
    while (ASEEN < NAL) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(WIN), GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", ALERTS[ASEEN++]);
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }
    return G_SOURCE_CONTINUE;
}

static char *rf(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) {
        perror(p);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    if (fl < 0) {
        fclose(f);
        perror(p);
        return 0;
    }
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)fl + 9);
    if (!b) {
        fclose(f);
        oom();
    }
    size_t got = fread(b, 1, (size_t)fl, f);
    fclose(f);
    b[got] = 0;
    *n = got;
    return b;
}

static void base_local(const char *p) {
    const char *s = strrchr(p, '/');
    if (!s) {
        js_set_base(".");
        return;
    }
    if (s == p) {
        js_set_base("/");
        return;
    }
    char *d = sdup(p, (size_t)(s - p));
    js_set_base(d);
    free(d);
}

int load_page(const char *u) {
    char ub[2048];
    snprintf(ub, sizeof ub, "%s", u);
    int url = url_is(ub);
    size_t len = 0;
    char *body = url ? http_get(ub, &len) : rf(ub, &len);
    if (!body) return 0;
    for (int i = 0; i < NLOG; i++) free(LOGS[i]);
    for (int i = 0; i < NAL; i++) free(ALERTS[i]);
    NLOG = NAL = 0;
    ASEEN = LOGSEEN = 0;
    static int JSUP;
    if (url) js_set_base(ub);
    else base_local(ub);
    if (JSUP) {
        js_done();
        js_init();
    } else {
        js_init();
        JSUP = 1;
    }
    css_reset();
    DOM = parse_html(body);
    run_scripts(DOM);
    js_pump();
    parse_css(css_collect(DOM, ub));
    LOADF = 0;
    js_win_event("domcontentloaded");
    free(CURURL);
    CURURL = sdup(ub, strlen(ub));
    return 1;
}

static void nav(const char *href) {
    if (!*href || *href == '#') return;
    busy_on();
    char *u;
    if (url_is(href)) u = sdup(href, strlen(href));
    else if (url_is(CURURL)) u = url_join(CURURL, href);
    else {
        const char *s = strrchr(CURURL, '/');
        size_t dl = s && s > CURURL ? (size_t)(s - CURURL) : 1;
        u = malloc(dl + strlen(href) + 2);
        if (!u) oom();
        if (s && s > CURURL) memcpy(u, CURURL, dl);
        else *u = '.';
        u[dl] = '/';
        memcpy(u + dl + 1, href, strlen(href) + 1);
    }
    if (NH == 32) {
        free(HIST[0]);
        memmove(HIST, HIST + 1, 31 * sizeof *HIST);
        NH = 31;
    }
    HIST[NH++] = sdup(CURURL, strlen(CURURL));
    if (!load_page(u)) free(HIST[--NH]);
    free(u);
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: %s <file.html|url>\n", argv[0]), 1;
    gtk_init(&argc, &argv);
    WIN = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(WIN), 1100, 800);
    g_signal_connect(WIN, "destroy", G_CALLBACK(gtk_main_quit), 0);
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
    GtkWidget *backb = gtk_button_new_from_icon_name("go-previous", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(backb, "clicked", G_CALLBACK(back_cb), 0);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), backb);
    ENTRY = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ENTRY), "\xe8\xbe\x93\xe5\x85\xa5\xe7\xbd\x91\xe5\x9d\x80\xe6\x88\x96\xe6\x96\x87\xe4\xbb\xb6\xe8\xb7\xaf\xe5\xbe\x84");
    gtk_widget_set_size_request(ENTRY, 520, -1);
    g_signal_connect(ENTRY, "activate", G_CALLBACK(go_entry), 0);
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), ENTRY);
    gtk_window_set_titlebar(GTK_WINDOW(WIN), hb);
    GtkWidget *scrl = gtk_scrolled_window_new(0, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrl), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(WIN), scrl);
    CANVAS = gtk_event_box_new();
    GdkRGBA white = {1, 1, 1, 1};
    gtk_widget_override_background_color(CANVAS, GTK_STATE_FLAG_NORMAL, &white);
    gtk_container_add(GTK_CONTAINER(scrl), CANVAS);
    GtkWidget *ovl = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(CANVAS), ovl);
    OVER = gtk_fixed_new();
    gtk_widget_set_hexpand(OVER, TRUE);
    gtk_widget_set_vexpand(OVER, TRUE);
    CONTENT = vbox(10);
    gtk_container_set_border_width(GTK_CONTAINER(CONTENT), 12);
    gtk_widget_set_valign(CONTENT, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(ovl), CONTENT);
    gtk_overlay_add_overlay(GTK_OVERLAY(ovl), OVER);
    gtk_widget_show_all(CANVAS);
    gtk_entry_set_text(GTK_ENTRY(ENTRY), argv[1]);
    gtk_window_set_title(GTK_WINDOW(WIN), "peek \xe2\x80\x94 loading...");
    gtk_widget_show_all(WIN);
    while (gtk_events_pending()) gtk_main_iteration();
    if (!load_page(argv[1])) {
        fprintf(stderr, "peekg: failed to load %s\n", argv[1]);
        gtk_window_set_title(GTK_WINDOW(WIN), "peek \xe2\x80\x94 load failed");
        g_timeout_add(16, tick, 0);
        gtk_main();
        js_done();
        return 0;
    }
    reload();
    g_timeout_add(16, tick, 0);
    gtk_main();
    js_done();
    return 0;
}
