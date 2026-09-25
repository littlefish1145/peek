#ifndef PEEK_H
#define PEEK_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include <quickjs.h>

static int pk_strncasecmp(const char *a, const char *b, size_t n) {
    while (n--) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}

#define WS ((1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13))
#define ISWS(c) ((c) == ' ' || ((unsigned)(c) < 32 && WS >> (c) & 1))

#define RESET "\x1b[0m"
#define CLEAR "\x1b[2J\x1b[H"

#define K1(a) (uint64_t)(unsigned char)(a)
#define K2(a, b) (K1(a) | K1(b) << 8)
#define K3(a, b, c) (K2(a, b) | K1(c) << 16)
#define K4(a, b, c, d) (K3(a, b, c) | K1(d) << 24)
#define K5(a, b, c, d, e) (K4(a, b, c, d) | K1(e) << 32)
#define K6(a, b, c, d, e, f) (K5(a, b, c, d, e) | K1(f) << 40)
#define K7(a, b, c, d, e, f, g) (K6(a, b, c, d, e, f) | K1(g) << 48)
#define K8(a, b, c, d, e, f, g, h) (K7(a, b, c, d, e, f, g) | K1(h) << 56)

#define K_CLASS K5('c', 'l', 'a', 's', 's')
#define K_ID K2('i', 'd')
#define K_STYLE K5('s', 't', 'y', 'l', 'e')
#define K_A K1('a')

void oom(void);
#define GROW(a, n, cap, type) do { \
    if ((n) >= (cap)) { \
        (cap) = (cap) ? (cap) << 1 : 8; \
        void *p_ = realloc((a), (size_t)(cap) * sizeof(type)); \
        if (!p_) oom(); \
        (a) = p_; \
    } \
} while (0)

uint64_t pk(const char *s);
char *cut(char *s, char *e);
int unescape(char *s, int len);
int utf8_put(char *w, unsigned cp);
int rgb256(int r, int g, int b);
void hsl_rgb(int h, int s, int l, int *R, int *G, int *B);
int ansi_color(const char *v);
char *sdup(const char *s, size_t n);

typedef struct Node Node;
typedef struct Tag Tag;

typedef struct Prop Prop;
struct Prop { const char *name; uint8_t f; };
enum { P_INH = 1 };

int bg_first_color(const char *v, char *out);
char *var_expand(const char *v);
extern Node **BTNS;
extern Node *FOC;
extern int NBTN, FOCI;

extern const Prop
    P_COLOR, P_BG, P_WEIGHT, P_FS, P_DECO, P_ALIGN,
    P_TRANS, P_PAD, P_WIDTH, P_BORDER, P_FSIZE, P_MARGIN, P_DISPLAY,
    P_BW, P_RADIUS, P_MINW, P_LH, P_POS, P_TOP, P_LEFT,
    P_GAP, P_JUST, P_DIR, P_AI, P_BGS,
    P_HEIGHT, P_MAXW, P_MAXH, P_MINH,
    P_MT, P_MR, P_MB, P_ML, P_PT, P_PR, P_PB, P_PL,
    P_BTW, P_BRW, P_BBW, P_BLW, P_BS, P_BC,
    P_FLOAT, P_CLEAR, P_Z, P_OVERFLOW, P_VIS, P_WS, P_VALIGN,
    P_TEXTINDENT, P_LISTSTYLE, P_BOXSIZING, P_RIGHT, P_BOTTOM,
    P_FWRAP, P_FGROW, P_FSHRINK, P_FBASIS, P_FLEX, P_ORDER, P_ASELF, P_ACONTENT,
    P_GTC, P_GTR, P_GCOL, P_GROW, P_GAFLOW,
    P_TLAYOUT, P_BCOLLAPSE, P_BSPACING, P_SRC, P_ALT,
    P_OPACITY, P_FAMILY, P_LETTERSP, P_WORDSP, P_FONT, P_CONTENT;
const Prop *prop_find(const char *k);
void ua_bold(Node *n);
void ua_italic(Node *n);
void ua_link(Node *n);
typedef struct { const Prop *p; const char *v; uint8_t imp; } Decl;
enum { PE_NONE = 0, PE_BEFORE, PE_AFTER, PE_UNSUP };
typedef struct {
    char **ps; uint8_t *sep; int np, pcap;
    Decl *d; int nd;
    char *media;
    uint8_t pe;
} Rule;
void parse_css(char *css);
void css_reset(void);
void css_viewport(int w, int h);
void css_get_viewport(int *w, int *h);
int media_match(const char *q);
const Rule *css_rules(int *n);
int rule_sel(const Rule *r, char *out, int n);
void presplit(Rule *r, char *sel);
int match_selector(const Rule *r, Node *n);
void apply_styles(Node *n);
extern Node **QL;
extern int NQL;
int qquery(const char *sel, size_t sl);
int qquery_at(Node *root, const char *sel, size_t sl);

typedef struct Attr { char *k, *v; } Attr;
typedef struct { const Prop *p; const char *v; int spec; } St;

struct Node {
    const Tag *def;
    char *tag; uint64_t tagpk; uint8_t taglen;
    char *text; int tlen;
    Attr *attrs; int nattr, acap;
    Node **child; int nchild, ccap;
    Node *parent;
    St *st; int nst, scap;
    Node *frag;
    uint8_t gen;
};

enum { T_VOID = 1, T_BLOCK = 2, T_HIDDEN = 4, T_LIST = 8, T_TEXTN = 16 };
struct Tag { const char *name; uint8_t f; void (*ua)(Node *); };

extern char N_TEXT[16], N_ROOT[16];
extern const Tag TAGS[], TAG_ANY;
const Tag *tag_find(const char *s);
extern Node *DOM;
Node *node(char *tag);
Node *text_node(const char *s, size_t n);
void dom_append(Node *p, Node *c);
void dom_insert_before(Node *p, Node *c, Node *ref);
void dom_remove(Node *c);
void push_child(Node *p, Node *c);
void st_push(Node *n, const Prop *p, const char *v, int spec);
char *attr_get(Node *n, const char *k);
void attr_set(Node *n, const char *k, const char *v);
void collect_text(Node *n, char *out, size_t cap);
Node *find_tag(Node *n, uint64_t k);
char *css_collect(Node *root, const char *page);

Node *parse_html(char *src);

int url_is(const char *);
char *url_join(const char *, const char *);
char *http_get(char *, size_t *);
char *http_req(const char *method, const char *url, const char *body, size_t *, int *);
typedef struct {
    const char *method, *body, *hdrs;
    char *url;
    size_t blen, *len;
    int *code;
    char **head;
} HttpReq;
char *http_do(HttpReq *r);

typedef struct NetStream NetStream;
NetStream *http_open(const char *method, const char *url, const char *hdrs,
                     char **head_out, int *code_out);
NetStream *ns_connect(const char *host, int port, int tls);
pk_socket_t ns_fd(NetStream *);
long ns_read(NetStream *, char *buf, long n);
long ns_write(NetStream *, const char *buf, long n);
void ns_close(NetStream *);

void js_init(void);
void js_done(void);
void js_set_base(const char *dir);
void js_pexc(const char *where);
void run_scripts(Node *n);
void js_load_dyn(Node *n);
JSValue mk_el(JSContext *ctx, Node *n);
void js_fire(Node *n, const char *ty);
void js_win_event(const char *type);
void js_viewport(int w, int h);
char *load_url(const char *src, size_t *out);
int oc_find(Node *n);
void js_click(int i);
int js_pump(void);
double js_next_wait(void);
extern char **LOGS, **ALERTS;
extern int NLOG, NAL;

#include "box.h"

#endif
