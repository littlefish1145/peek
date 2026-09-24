#include "peek.h"
#include "scan.h"

static void parse_attrs(char *s, Node *n) {
    while (*s) {
        while (ISWS(*s)) *s++ = 0;
        if (!*s) break;
        char *k = s, *v = "";
        while (*s && !ISWS(*s) && *s != '=') s++;
        char *ke = s;
        for (char *q = k; q < ke; q++) *q = (char)tolower((unsigned char)*q);
        if (*s == '=') {
            *s++ = 0;
            char q = 0;
            if (*s == '"' || *s == '\'') q = *s++;
            v = s;
            while (*s && *s != q && (q || !ISWS(*s))) s++;
            if (*s) *s++ = 0;
        } else if (*s) *s++ = 0;
        *ke = 0;
        unescape(v, strlen(v));
        GROW(n->attrs, n->nattr, n->acap, Attr);
        n->attrs[n->nattr].k = k;
        n->attrs[n->nattr].v = v;
        n->nattr++;
    }
}

static void add_text(char *s, char *e, Node *parent, int amp) {
    char sv = *e;
    *e = 0;
    if (*s) {
        Node *n = node(N_TEXT);
        n->text = s;
        n->tlen = amp ? unescape(s, (int)strlen(s)) : (int)(e - s);
        n->parent = parent;
        push_child(parent, n);
    }
    *e = sv;
}

static char *tag_end(char *p, char *end) {
    char q = 0;
    for (char *s = p; s < end; s++) {
        if (q) {
            if (*s == q) q = 0;
        } else if (*s == '"' || *s == '\'') {
            q = *s;
        } else if (*s == '>') {
            return s;
        }
    }
    return 0;
}

static char *raw_end(char *p, char *end, const char *name, int nl) {
    char *q = p;
    while (q < end) {
        char *lt = memchr(q, '<', (size_t)(end - q));
        if (!lt) return end;
        if (lt + 1 < end && lt[1] == '/' && lt + 2 + nl <= end &&
            !strncasecmp(lt + 2, name, (size_t)nl)) {
            char c = lt + 2 + nl < end ? lt[2 + nl] : (char)0;
            if (lt + 2 + nl >= end || c == '>' || ISWS(c) || c == '/') return lt;
        }
        q = lt + 1;
    }
    return end;
}

static int name_eq(Node *n, const char *t) {
    int l = (int)strlen(t);
    return n->taglen == l && !memcmp(n->tag, t, (size_t)l);
}

static int name_in(const char *t, const char *const *list) {
    for (int i = 0; list[i]; i++)
        if (!strcmp(t, list[i])) return 1;
    return 0;
}

static int node_in(Node *n, const char *const *list) {
    for (int i = 0; list[i]; i++)
        if (name_eq(n, list[i])) return 1;
    return 0;
}

static int raw_text(const char *t) {
    static const char *const RT[] = {"script", "style", "textarea", "title",
                                     "iframe", "noembed", "noframes", "xmp", 0};
    return name_in(t, RT);
}

static void close_implied(Node **stk, int *top, const char *t) {
    static const char *const BLK[] = {"address", "article", "aside", "blockquote",
        "details", "div", "dl", "fieldset", "figcaption", "figure", "footer",
        "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr",
        "main", "menu", "nav", "ol", "p", "pre", "section", "table", "ul", 0};
    static const char *const P_ONLY[] = {"p", 0};
    static const char *const LI[] = {"p", "li", 0};
    static const char *const DL[] = {"p", "dt", "dd", 0};
    static const char *const TD[] = {"td", "th", 0};
    static const char *const TR[] = {"td", "th", "tr", 0};
    static const char *const SEC[] = {"td", "th", "tr", "thead", "tbody", "tfoot", 0};
    static const char *const OPT[] = {"option", "optgroup", 0};
    static const char *const RT[] = {"rt", "rp", 0};
    const char *const *set = 0;
    if (name_in(t, BLK)) set = P_ONLY;
    else if (!strcmp(t, "li")) set = LI;
    else if (!strcmp(t, "dt") || !strcmp(t, "dd")) set = DL;
    else if (!strcmp(t, "td") || !strcmp(t, "th")) set = TD;
    else if (!strcmp(t, "tr")) set = TR;
    else if (!strcmp(t, "thead") || !strcmp(t, "tbody") || !strcmp(t, "tfoot"))
        set = SEC;
    else if (!strcmp(t, "option")) set = OPT;
    else if (!strcmp(t, "rt") || !strcmp(t, "rp")) set = RT;
    else return;
    while (*top > 1 && node_in(stk[*top - 1], set)) (*top)--;
}

static void move_children(Node *from, Node *to, Node *keep1, Node *keep2) {
    int n = from->nchild;
    if (n <= 0) return;
    Node **tmp = malloc((size_t)n * sizeof *tmp);
    if (!tmp) oom();
    int c = 0;
    for (int i = 0; i < from->nchild; i++) {
        Node *x = from->child[i];
        if (x == to || x == keep1 || x == keep2) continue;
        tmp[c++] = x;
    }
    for (int i = 0; i < c; i++) dom_append(to, tmp[i]);
    free(tmp);
}

static void dom_normalize(Node *root) {
    Node *html = 0;
    for (int i = 0; i < root->nchild; i++)
        if (!html && root->child[i]->tag[0] != '#' && name_eq(root->child[i], "html"))
            html = root->child[i];
    if (!html) {
        html = node("html");
        html->parent = root;
        push_child(root, html);
    }
    move_children(root, html, 0, 0);
    Node *head = 0, *body = 0;
    for (int i = 0; i < html->nchild; i++) {
        Node *c = html->child[i];
        if (c->tag[0] == '#') continue;
        if (!head && name_eq(c, "head")) head = c;
        if (!body && name_eq(c, "body")) body = c;
    }
    if (!body) {
        body = node("body");
        body->parent = html;
        push_child(html, body);
    }
    move_children(html, body, head, body);
}

Node *parse_html(char *src) {
    Node **stk = 0;
    int scap = 0, top = 0;
    GROW(stk, 1, scap, Node *);
    stk[top++] = node(N_ROOT);
    char *end = src + strlen(src);
    char *p = src;
    while (p < end) {
        if (*p != '<') {
            int amp = 0;
            char *e = scan_next(p, end, &amp);
            add_text(p, e, stk[top - 1], amp);
            p = e;
        } else {
            if (p + 4 <= end && !strncmp(p + 1, "!--", 3)) {
                char *c = strstr(p + 2, "-->");
                p = c ? c + 3 : end;
                continue;
            }
            char *e = tag_end(p, end);
            if (!e) {
                int amp = 0;
                char *t = p + 1 < end ? scan_next(p + 1, end, &amp) : end;
                add_text(p + 1, t, stk[top - 1], amp);
                p = t;
                continue;
            }
            char *t = cut(p + 1, e);
            p = e + 1;
            if (*t == '!' || *t == '?') {
            } else if (*t == '/') {
                for (char *q = t + 1; *q; q++) *q = (char)tolower((unsigned char)*q);
                char *sp = strchr(t + 1, ' ');
                if (sp) *sp = 0;
                int f = 0;
                for (int k = top - 1; k > 0; k--)
                    if (name_eq(stk[k], t + 1)) { f = k; break; }
                if (f) top = f;
            } else if (*t) {
                int sc = t[strlen(t) - 1] == '/';
                if (sc) t[strlen(t) - 1] = 0;
                char *sp = strchr(t, ' ');
                char *te = sp ? sp : t + strlen(t);
                for (char *q = t; q < te; q++) *q = (char)tolower((unsigned char)*q);
                Node *n = node(sp ? cut(t, sp) : t);
                close_implied(stk, &top, n->tag);
                if (sp) parse_attrs(sp + 1, n);
                n->parent = stk[top - 1];
                push_child(stk[top - 1], n);
                int raw = 0;
                if (!sc && !(n->def->f & T_VOID)) raw = raw_text(n->tag);
                if (raw) {
                    char *re = raw_end(p, end, n->tag, n->taglen);
                    add_text(p, re, n, 1);
                    char *close = memchr(re, '>', (size_t)(end - re));
                    p = close ? close + 1 : end;
                } else if (!sc && !(n->def->f & T_VOID)) {
                    GROW(stk, top + 1, scap, Node *);
                    stk[top++] = n;
                }
            }
        }
    }
    Node *root = stk[0];
    free(stk);
    dom_normalize(root);
    return root;
}
