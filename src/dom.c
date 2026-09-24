#include "peek.h"

char N_TEXT[16] = "#text", N_ROOT[16] = "#root";
Node *DOM;

#define CHUNK 128
typedef struct Chunk { struct Chunk *next; Node n[CHUNK]; } Chunk;
static Chunk FIRST, *CH;
static int CN;

void push_child(Node *p, Node *c) {
    GROW(p->child, p->nchild, p->ccap, Node *);
    p->child[p->nchild++] = c;
}

void st_push(Node *n, const Prop *p, const char *v, int spec) {
    GROW(n->st, n->nst, n->scap, St);
    St *s = n->st + n->nst++;
    s->p = p; s->v = v; s->spec = spec;
}

const Tag TAGS[] = {
    {.name = N_ROOT}, {.name = N_TEXT, .f = T_TEXTN},
    {.name = "html", .f = T_BLOCK}, {.name = "body", .f = T_BLOCK},
    {.name = "head", .f = T_HIDDEN}, {.name = "title", .f = T_HIDDEN},
    {.name = "style", .f = T_HIDDEN}, {.name = "script", .f = T_HIDDEN},
    {.name = "template", .f = T_HIDDEN}, {.name = "noscript", .f = T_HIDDEN},
    {.name = "base", .f = T_VOID | T_HIDDEN}, {.name = "meta", .f = T_VOID | T_HIDDEN},
    {.name = "link", .f = T_VOID | T_HIDDEN}, {.name = "source", .f = T_VOID},
    {.name = "track", .f = T_VOID}, {.name = "area", .f = T_VOID},
    {.name = "param", .f = T_VOID}, {.name = "col", .f = T_VOID},
    {.name = "#comment", .f = T_HIDDEN},

    {.name = "p", .f = T_BLOCK}, {.name = "div", .f = T_BLOCK},
    {.name = "address", .f = T_BLOCK}, {.name = "center", .f = T_BLOCK},
    {.name = "h1", .f = T_BLOCK, .ua = ua_bold}, {.name = "h2", .f = T_BLOCK, .ua = ua_bold},
    {.name = "h3", .f = T_BLOCK, .ua = ua_bold}, {.name = "h4", .f = T_BLOCK, .ua = ua_bold},
    {.name = "h5", .f = T_BLOCK, .ua = ua_bold}, {.name = "h6", .f = T_BLOCK, .ua = ua_bold},
    {.name = "ul", .f = T_BLOCK}, {.name = "ol", .f = T_BLOCK},
    {.name = "menu", .f = T_BLOCK},
    {.name = "li", .f = T_BLOCK | T_LIST},
    {.name = "dl", .f = T_BLOCK}, {.name = "dt", .f = T_BLOCK}, {.name = "dd", .f = T_BLOCK},
    {.name = "header", .f = T_BLOCK}, {.name = "footer", .f = T_BLOCK},
    {.name = "section", .f = T_BLOCK}, {.name = "article", .f = T_BLOCK},
    {.name = "nav", .f = T_BLOCK}, {.name = "aside", .f = T_BLOCK},
    {.name = "main", .f = T_BLOCK}, {.name = "figure", .f = T_BLOCK},
    {.name = "figcaption", .f = T_BLOCK}, {.name = "blockquote", .f = T_BLOCK},
    {.name = "details", .f = T_BLOCK}, {.name = "summary", .f = T_BLOCK},
    {.name = "fieldset", .f = T_BLOCK}, {.name = "legend", .f = T_BLOCK},
    {.name = "form", .f = T_BLOCK}, {.name = "dialog", .f = T_BLOCK},
    {.name = "pre", .f = T_BLOCK}, {.name = "hr", .f = T_VOID | T_BLOCK},
    {.name = "br", .f = T_VOID}, {.name = "wbr", .f = T_VOID},

    {.name = "table", .f = T_BLOCK}, {.name = "caption", .f = T_BLOCK},
    {.name = "colgroup", .f = T_BLOCK}, {.name = "thead", .f = T_BLOCK},
    {.name = "tbody", .f = T_BLOCK}, {.name = "tfoot", .f = T_BLOCK},
    {.name = "tr", .f = T_BLOCK}, {.name = "td", .f = T_BLOCK}, {.name = "th", .f = T_BLOCK},

    {.name = "span"}, {.name = "a", .ua = ua_link}, {.name = "abbr"},
    {.name = "b", .ua = ua_bold}, {.name = "strong", .ua = ua_bold},
    {.name = "i", .ua = ua_italic}, {.name = "em", .ua = ua_italic},
    {.name = "cite", .ua = ua_italic}, {.name = "dfn", .ua = ua_italic},
    {.name = "var", .ua = ua_italic}, {.name = "u"}, {.name = "s"}, {.name = "del"},
    {.name = "ins"}, {.name = "small"}, {.name = "big"}, {.name = "mark"},
    {.name = "sub"}, {.name = "sup"}, {.name = "q"}, {.name = "time"},
    {.name = "data"}, {.name = "bdi"}, {.name = "bdo"}, {.name = "ruby"},
    {.name = "rt"}, {.name = "rp"}, {.name = "code"}, {.name = "kbd"},
    {.name = "samp"}, {.name = "output"}, {.name = "label"},
    {.name = "img", .f = T_VOID}, {.name = "input", .f = T_VOID},
    {.name = "textarea"}, {.name = "select"}, {.name = "option"}, {.name = "optgroup"},
    {.name = "button"}, {.name = "iframe"}, {.name = "canvas"}, {.name = "svg"},
    {.name = "math"}, {.name = "video"}, {.name = "audio"}, {.name = "object"},
    {.name = "embed", .f = T_VOID}, {.name = "map"}, {.name = "progress"},
    {.name = "meter"}, {.name = "marquee"},
};

const Tag TAG_ANY = {0};

static const Tag *SI[sizeof TAGS / sizeof *TAGS];
static uint64_t SK[sizeof TAGS / sizeof *TAGS];
static uint8_t SL[sizeof TAGS / sizeof *TAGS];
static int SN;

static void idx_build(void) {
    SN = (int)(sizeof TAGS / sizeof *TAGS);
    for (int i = 0; i < SN; i++) {
        SI[i] = TAGS + i;
        SK[i] = pk(TAGS[i].name);
        SL[i] = (uint8_t)strlen(TAGS[i].name);
    }
    for (int i = 1; i < SN; i++) {
        const Tag *tv = SI[i];
        uint64_t kv = SK[i];
        uint8_t lv = SL[i];
        int j = i - 1;
        while (j >= 0 && (SK[j] > kv || (SK[j] == kv && SL[j] > lv))) {
            SI[j + 1] = SI[j];
            SK[j + 1] = SK[j];
            SL[j + 1] = SL[j];
            j--;
        }
        SI[j + 1] = tv;
        SK[j + 1] = kv;
        SL[j + 1] = lv;
    }
}

const Tag *tag_find(const char *s) {
    if (!SN) idx_build();
    uint64_t k = pk(s);
    uint8_t l = (uint8_t)strlen(s);
    int lo = 0, hi = SN;
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (SK[m] < k || (SK[m] == k && SL[m] < l)) lo = m + 1;
        else hi = m;
    }
    if (lo < SN && SK[lo] == k && SL[lo] == l) return SI[lo];
    return &TAG_ANY;
}

Node *node(char *tag) {
    if (!CH) CH = &FIRST;
    if (CN >= CHUNK) {
        Chunk *c = calloc(1, sizeof *c);
        if (!c) oom();
        CH->next = c; CH = c; CN = 0;
    }
    Node *n = CH->n + CN++;
    n->tag = tag;
    n->tagpk = pk(tag);
    n->taglen = (uint8_t)strlen(tag);
    n->def = tag_find(tag);
    if (n->def->ua) n->def->ua(n);
    return n;
}

Node *text_node(const char *s, size_t n) {
    Node *t = node(N_TEXT);
    t->text = sdup(s, n);
    t->tlen = (int)n;
    return t;
}

static void detach(Node *c) {
    Node *p = c->parent;
    if (!p) { c->parent = 0; return; }
    int i = 0;
    while (i < p->nchild && p->child[i] != c) i++;
    if (i < p->nchild) {
        memmove(p->child + i, p->child + i + 1,
                (size_t)(p->nchild - i - 1) * sizeof(Node *));
        p->nchild--;
    }
    c->parent = 0;
}

void dom_append(Node *p, Node *c) {
    detach(c);
    c->parent = p;
    push_child(p, c);
}

void dom_insert_before(Node *p, Node *c, Node *ref) {
    detach(c);
    int idx = p->nchild;
    for (int i = 0; i < p->nchild; i++)
        if (p->child[i] == ref) { idx = i; break; }
    GROW(p->child, p->nchild + 1, p->ccap, Node *);
    memmove(p->child + idx + 1, p->child + idx,
            (size_t)(p->nchild - idx) * sizeof(Node *));
    p->child[idx] = c;
    p->nchild++;
    c->parent = p;
}

void dom_remove(Node *c) { detach(c); }

char *attr_get(Node *n, const char *k) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) return n->attrs[i].v;
    return 0;
}

void attr_set(Node *n, const char *k, const char *v) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) {
            n->attrs[i].v = sdup(v, strlen(v));
            return;
        }
    GROW(n->attrs, n->nattr, n->acap, Attr);
    n->attrs[n->nattr].k = sdup(k, strlen(k));
    n->attrs[n->nattr].v = sdup(v, strlen(v));
    n->nattr++;
}

void collect_text(Node *n, char *out, size_t cap) {
    if (n->def->f & T_TEXTN) {
        int room = (int)cap - (int)strlen(out) - 1;
        strncat(out, n->text, n->tlen < room ? n->tlen : room);
        return;
    }
    for (int i = 0; i < n->nchild; i++) collect_text(n->child[i], out, cap);
}

Node *find_tag(Node *n, uint64_t k) {
    if (n->tagpk == k) return n;
    for (int i = 0; i < n->nchild; i++) {
        Node *r = find_tag(n->child[i], k);
        if (r) return r;
    }
    return NULL;
}
