#include "peek.h"
#include "box.h"

static Node *FOCUS;

void paint_set_focus(Node *n) { FOCUS = n; }

static void paint_bg(Box *b, const PaintBackend *bk, void *u) {
    if (b->bgcolor < 0 || b->w <= 0 || b->h <= 0) return;
    bk->rect(u, b->x, b->y, b->w, b->h, b->bgcolor);
}

static void paint_border(Box *b, const PaintBackend *bk, void *u) {
    if (b->bstyle == BS_NONE || b->w <= 0 || b->h <= 0) return;
    int col = b->bcolor < 0 ? 7 : b->bcolor;

    if (b->bt > 0) bk->hline(u, b->x, b->y, b->w, col, b->bstyle);
    if (b->bb > 0) bk->hline(u, b->x, b->y + b->h - 1, b->w, col, b->bstyle);
    if (b->bl > 0) bk->vline(u, b->x, b->y, b->h, col, b->bstyle);
    if (b->br > 0) bk->vline(u, b->x + b->w - 1, b->y, b->h, col, b->bstyle);
}

static int list_index(Box *b) {
    int idx = 0;
    Box *p = b->par;
    if (!p) return 1;
    for (Box *c = p->first; c; c = c->next) {
        if (c->role != BX_LIST) continue;
        idx++;
        if (c == b) return idx;
    }
    return 1;
}

static void paint_marker(Box *b, const PaintBackend *bk, void *u) {
    if (b->list_style == 0 || b->nline == 0) return;
    Line *L = b->lines;
    char m[24];
    int ml = 0;
    switch (b->list_style) {
        case 1: m[0] = (char)0xE2; m[1] = (char)0x80; m[2] = (char)0xA2; ml = 3; break;
        case 2: ml = sprintf(m, "%d.", list_index(b)); break;
        case 3: m[0] = (char)0xE2; m[1] = (char)0x97; m[2] = (char)0xA6; ml = 3; break;
        case 4: m[0] = (char)0xE2; m[1] = (char)0x96; m[2] = (char)0xAA; ml = 3; break;
        default: return;
    }
    int fx = b->x - vx_cell_w() * 3;
    bk->text(u, fx, L->y, m, ml, b->fgcolor, 0);
}

static void paint_frag(const Frag *f, int line_y, const PaintBackend *bk, void *u) {
    if (f->bg >= 0 && f->w > 0 && !f->space)
        bk->rect(u, f->x, line_y, f->w, f->h, f->bg);
    if (f->replaced) {
        const char *alt = 0;
        if (f->bx && f->bx->n) alt = attr_get((Node *)f->bx->n, "alt");
        if (alt && *alt) {
            bk->text(u, f->x, line_y, alt, (int)strlen(alt), f->fg, f->fl);
        } else if (f->bx && f->bx->bstyle != BS_NONE) {
            Box tmp = *f->bx;
            tmp.x = f->x; tmp.y = line_y; tmp.w = f->w; tmp.h = f->h;
            bk->hline(u, f->x, line_y, f->w, tmp.bcolor < 0 ? 7 : tmp.bcolor, BS_SOLID);
            bk->hline(u, f->x, line_y + f->h - 1, f->w, tmp.bcolor < 0 ? 7 : tmp.bcolor, BS_SOLID);
            bk->vline(u, f->x, line_y, f->h, tmp.bcolor < 0 ? 7 : tmp.bcolor, BS_SOLID);
            bk->vline(u, f->x + f->w - 1, line_y, f->h, tmp.bcolor < 0 ? 7 : tmp.bcolor, BS_SOLID);
        }
        return;
    }
    if (!f->s || f->len <= 0) return;

    char stackbuf[512];
    const char *s = f->s;
    int n = f->len;
    if (f->ttrans && n < (int)sizeof stackbuf) {
        for (int i = 0; i < n; i++) {
            char c = f->s[i];
            if (f->ttrans == 1) stackbuf[i] = (char)toupper((unsigned char)c);
            else if (f->ttrans == 2) stackbuf[i] = (char)tolower((unsigned char)c);
            else stackbuf[i] = c;
        }
        s = stackbuf;
    }
    bk->text(u, f->x, line_y, s, n, f->fg, f->fl);
}

static void paint_lines(Box *b, const PaintBackend *bk, void *u) {
    for (int i = 0; i < b->nline; i++) {
        Line *L = b->lines + i;
        for (int k = 0; k < L->nfrag; k++) paint_frag(L->frag + k, L->y, bk, u);
    }
}

static int skip_in_traversal(const Box *c) {
    switch (c->role) {
        case BX_TEXT:
        case BX_INLINE:
        case BX_BR:
        case BX_IMG:
            return 1;
        default:
            return 0;
    }
}

static int zkey(const Box *b) { return b->z; }

static void paint_box(Box *b, const PaintBackend *bk, void *u) {
    if (b->hide) return;
    if (b->role == BX_ROOT) {
        for (Box *c = b->first; c; c = c->next) paint_box(c, bk, u);
        return;
    }
    if (b->vis_hidden) return;

    paint_bg(b, bk, u);
    paint_border(b, bk, u);
    paint_marker(b, bk, u);
    paint_lines(b, bk, u);

    for (Box *c = b->first; c; c = c->next) {
        if (c->hide || skip_in_traversal(c)) continue;
        if (c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        paint_box(c, bk, u);
    }

    Box *stack[64];
    int ns = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide || skip_in_traversal(c)) continue;
        if (c->pos != POS_ABS && c->pos != POS_FIXED) continue;
        if (ns < 64) stack[ns++] = c;
    }
    for (int i = 1; i < ns; i++) {
        Box *cur = stack[i];
        int j = i - 1;
        while (j >= 0 && zkey(stack[j]) > zkey(cur)) { stack[j + 1] = stack[j]; j--; }
        stack[j + 1] = cur;
    }
    for (int i = 0; i < ns; i++) paint_box(stack[i], bk, u);
}

static void invert_rect(Canvas *cv, int x, int y, int w, int h) {
    int x0 = vx_col(x), y0 = vx_row(y);
    int x1 = vx_col(x + w - 1) + 1, y1 = vx_row(y + h - 1) + 1;
    for (int yy = y0; yy < y1; yy++) {
        if (yy < 0 || yy >= cv->h) continue;
        for (int xx = x0; xx < x1; xx++) {
            if (xx < 0 || xx >= cv->w) continue;
            cv->p[yy * cv->w + xx].fl ^= FL_REV;
        }
    }
}

static int find_box(Box *b, Node *n, Box **out) {
    if (b->n == n) { *out = b; return 1; }
    for (Box *c = b->first; c; c = c->next)
        if (find_box(c, n, out)) return 1;
    return 0;
}

void paint_tree(Box *root, const PaintBackend *bk, void *ctx) {
    paint_box(root, bk, ctx);
    if (!FOCUS) return;
    TermCtx *tc = ctx;
    if (!tc || !tc->cv) return;
    Box *found = 0;
    if (find_box(root, FOCUS, &found) && found)
        invert_rect(tc->cv, found->x, found->y,
                    found->w > 0 ? found->w + vx_cell_w() : vx_cell_w(),
                    found->h > 0 ? found->h : vx_cell_h());
}
