#ifndef PEEK_BOX_H
#define PEEK_BOX_H

#include <stdint.h>

int  vx_cols(void);
int  vx_rows(void);
int  vx_width(void);
int  vx_height(void);
int  vx_cell_w(void);
int  vx_cell_h(void);
int  vx_col(int px);
int  vx_row(int px);
int  vx_font_default(void);
void vx_init(void);
void vx_sync(void);

int  vx_chlen(const char *s, int i, int n);
int  vx_chwidth(const char *s, int i, int n);
int  vx_chcols(const char *s, int i, int n);
int  vx_text_px(const char *s, int n, int font);

enum { U_AUTO, U_PX, U_PCT, U_EM, U_REM, U_CH, U_EX, U_PT, U_PC, U_CM, U_MM, U_IN, U_VW, U_VH };

typedef struct { int v; uint8_t u; } Len;
#define LEN_PXV(p) ((Len){ (p) * 64, U_PX })
#define LEN_AUTO   ((Len){ 0, U_AUTO })

int  len_parse(const char *s, const char *e, Len *out);
Len  len_read(const char *s);
int  len_px(Len l, int base, int font);
int  len_is_auto(Len l);

enum {
    BX_BLOCK, BX_INLINE, BX_TEXT, BX_ANON, BX_IBLOCK,
    BX_TABLE, BX_TROWG, BX_TROW, BX_TCELL, BX_TCAP,
    BX_FLEX, BX_GRID, BX_IMG, BX_BR, BX_HR, BX_LIST, BX_ROOT
};
enum { POS_STATIC, POS_REL, POS_ABS, POS_FIXED };
enum { FL_NONE, FL_LEFT, FL_RIGHT };
enum { TA_LEFT, TA_CENTER, TA_RIGHT, TA_JUSTIFY };
enum { WS_NORMAL, WS_NOWRAP, WS_PRE, WS_PREWRAP, WS_PRELINE };
enum { VA_BASELINE, VA_TOP, VA_MIDDLE, VA_BOTTOM };
enum { BS_NONE, BS_SOLID, BS_DASHED, BS_DOTTED };

enum { FL_BOLD = 1, FL_ITAL = 2, FL_UL = 4, FL_STRIKE = 8, FL_REV = 16 };

typedef struct Frag Frag;
struct Frag {
    const char *s;
    int len;
    int x, w, h;
    int fg, bg, font;
    uint8_t fl;
    uint8_t replaced;
    uint8_t space;
    uint8_t ttrans;
    const struct Box *bx;
};

typedef struct Line {
    int x, y, w, h, baseline;
    Frag *frag;
    int nfrag, fcap;
} Line;

typedef struct Box Box;
struct Box {
    Node *n;
    uint8_t role, pos, flt, clr;
    uint8_t hide;
    uint8_t vis_hidden;
    uint8_t ovf;
    uint8_t bstyle, list_style;
    int bcolor, bgcolor, fgcolor;

    int mt, mr, mb, ml;
    int pt, pr, pb, pl;
    int bt, br, bb, bl;

    int x, y, w, h;
    int cw, ch;

    int font, lh;
    uint8_t tflag, talign, ws, vval, ttrans;
    int text_indent;

    int z, order;
    uint8_t clear;

    int grow, shrink, basis;
    uint8_t jc, ai, as, fdir, fwrap;

    int gcol, gcs, grw, grs;

    int colspan, rowspan;

    uint8_t mt_collapsed;

    int attr_w, attr_h;

    const char *text;
    int tlen;

    Line *lines;
    int nline, lcap;

    Box *par, *first, *last, *next;
};

#define M_AUTO (-(1 << 24))

typedef struct { uint32_t ch; uint8_t fg, bg, fl, cont; } Cell;
typedef struct { Cell *p; int w, h; } Canvas;

Canvas *cv_new(int w, int h);
void    cv_free(Canvas *c);
void    cv_clear(Canvas *c, int bg);

typedef struct PaintBackend {
    void (*rect)(void *u, int x, int y, int w, int h, int color);
    void (*text)(void *u, int x, int y, const char *s, int len, int color, int fl);
    void (*hline)(void *u, int x, int y, int w, int color, int style);
    void (*vline)(void *u, int x, int y, int h, int color, int style);
} PaintBackend;

typedef struct { Canvas *cv; uint8_t rev; } TermCtx;
const PaintBackend *term_backend(void);

Box *box_build(Node *root);
void box_free(Box *b);
void lay_layout(Box *root);
void paint_tree(Box *root, const PaintBackend *bk, void *ctx);
void term_flush(Canvas *cv);
void term_frame(Canvas *cv);
void box_dump(Box *r);

void table_lay(Box *b, int cbx, int cby, int cbw);
void flex_lay(Box *b, int cbw, Box *cbanc);
void grid_lay(Box *b, int cbw, Box *cbanc);

void lay_block_content(Box *b, int cbx, int cby, int cbw, Box *cbanc);

void box_intrinsic(Box *b, int which, int *w, int *h);
int  box_shrink_to_fit(Box *b, int avail);

void lay_box_dispatch(Box *b, int cbx, int cby, int cbw, Box *cbanc);

void render(Node *n);
void draw_dialog(const char *m);

void peek_layout(void);
void peek_paint(void);
void peek_dump(void);
Box *peek_root(void);

void paint_set_focus(Node *n);

#endif
