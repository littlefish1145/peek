#include "peek.h"
#include "box.h"

#define MAXR 256
#define MAXC 64

typedef struct { Box *cell; int row, col, cs, rs; } Slot;
typedef struct { Slot *v; int n, cap; } Slots;

static void slpush(Slots *s, Box *cell, int row, int col, int cs, int rs) {
    if (s->n >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        Slot *p = realloc(s->v, (size_t)s->cap * sizeof(Slot));
        if (!p) oom();
        s->v = p;
    }
    Slot *x = s->v + s->n++;
    x->cell = cell;
    x->row = row;
    x->col = col;
    x->cs = cs;
    x->rs = rs;
}

static void place_row(Box *row, Slots *sl, unsigned char occ[MAXR][MAXC], int r, int *ncols) {
    int col = 0;
    for (Box *c = row->first; c; c = c->next) {
        if (c->hide || c->role != BX_TCELL) continue;
        while (col < MAXC && occ[r][col]) col++;
        if (col >= MAXC) break;
        int cs = c->colspan < 1 ? 1 : c->colspan;
        int rs = c->rowspan < 1 ? 1 : c->rowspan;
        if (col + cs > MAXC) cs = MAXC - col;
        if (r + rs > MAXR) rs = MAXR - r;
        slpush(sl, c, r, col, cs, rs);
        for (int rr = r; rr < r + rs; rr++)
            for (int cc = col; cc < col + cs; cc++)
                occ[rr][cc] = 1;
        if (col + cs > *ncols) *ncols = col + cs;
        col += cs;
    }
}

static void collect(Box *t, Slots *sl, int *pncols, int *pnrows) {
    static unsigned char occ[MAXR][MAXC];
    memset(occ, 0, sizeof occ);
    int nrows = 0, ncols = 0;

    for (Box *c = t->first; c; c = c->next) {
        if (c->hide || c->role == BX_TCAP) continue;
        if (c->role == BX_TROWG) {
            for (Box *r = c->first; r; r = r->next) {
                if (r->hide || r->role != BX_TROW) continue;
                if (nrows >= MAXR) break;
                place_row(r, sl, occ, nrows, &ncols);
                nrows++;
            }
        } else if (c->role == BX_TROW) {
            if (nrows >= MAXR) break;
            place_row(c, sl, occ, nrows, &ncols);
            nrows++;
        } else if (c->role == BX_TCELL) {
            if (nrows >= MAXR) break;
            slpush(sl, c, nrows, 0, 1, 1);
            if (ncols < 1) ncols = 1;
            nrows++;
        }
    }
    *pncols = ncols < 1 ? 1 : ncols;
    *pnrows = nrows < 1 ? 1 : nrows;
}

static int table_spacing(Box *t) {
    const char *v = 0;
    if (t->n)
        for (int i = 0; i < t->n->nst; i++)
            if (t->n->st[i].p == &P_BSPACING) v = t->n->st[i].v;
    if (!v) return 2;
    Len l;
    if (!len_parse(v, v + strlen(v), &l)) return 2;
    return len_px(l, 0, t->font);
}

static int cell_border(Box *c) { return c->pl + c->pr + c->bl + c->br; }

static void lay_table(Box *t, int cbx, int cby, int cbw) {
    int ncols = 1, nrows = 1;
    Slots sl = {0};
    collect(t, &sl, &ncols, &nrows);

    int explicit_w = 0;
    if (t->n)
        for (int i = 0; i < t->n->nst; i++)
            if (t->n->st[i].p == &P_WIDTH) {
                Len l;
                if (len_parse(t->n->st[i].v, t->n->st[i].v + strlen(t->n->st[i].v), &l) && l.u != U_AUTO)
                    explicit_w = len_px(l, cbw, t->font);
            }

    int sp = table_spacing(t);
    int gap = sp * (ncols + 1);

    int *cmin = calloc((size_t)ncols, sizeof(int));
    int *cmax = calloc((size_t)ncols, sizeof(int));
    int *cwid = calloc((size_t)ncols, sizeof(int));
    int *cx = calloc((size_t)ncols, sizeof(int));
    int *rh = calloc((size_t)nrows, sizeof(int));
    int *ry = calloc((size_t)nrows, sizeof(int));
    if (!cmin || !cmax || !cwid || !cx || !rh || !ry) oom();

    for (int i = 0; i < sl.n; i++) {
        Slot *s = sl.v + i;
        if (s->cs != 1 || s->col >= ncols) continue;
        int mn, mx, hh;
        box_intrinsic(s->cell, 0, &mn, &hh);
        box_intrinsic(s->cell, 1, &mx, &hh);
        mn += cell_border(s->cell);
        mx += cell_border(s->cell);
        if (mn > cmin[s->col]) cmin[s->col] = mn;
        if (mx > cmax[s->col]) cmax[s->col] = mx;
    }
    for (int i = 0; i < sl.n; i++) {
        Slot *s = sl.v + i;
        if (s->cs <= 1) continue;
        int mn, mx, hh;
        box_intrinsic(s->cell, 0, &mn, &hh);
        box_intrinsic(s->cell, 1, &mx, &hh);
        mn += cell_border(s->cell);
        mx += cell_border(s->cell);
        int have_mn = 0, have_mx = 0, n = 0;
        for (int k = 0; k < s->cs && s->col + k < ncols; k++) {
            have_mn += cmin[s->col + k];
            have_mx += cmax[s->col + k];
            n++;
        }
        if (!n) continue;
        if (mn > have_mn) {
            int need = mn - have_mn;
            for (int k = 0; k < n; k++) cmin[s->col + k] += need / n;
        }
        if (mx > have_mx) {
            int need = mx - have_mx;
            for (int k = 0; k < n; k++) cmax[s->col + k] += need / n;
        }
    }
    for (int c = 0; c < ncols; c++) {
        if (cmin[c] < vx_cell_w()) cmin[c] = vx_cell_w();
        if (cmax[c] < cmin[c]) cmax[c] = cmin[c];
    }

    int sum_min = 0, sum_max = 0;
    for (int c = 0; c < ncols; c++) { sum_min += cmin[c]; sum_max += cmax[c]; }

    int avail = (explicit_w ? explicit_w : cbw) - gap;

    if (!explicit_w && avail > sum_max) avail = sum_max;
    if (avail < 0) avail = 0;

    if (avail <= sum_min) {
        for (int c = 0; c < ncols; c++) cwid[c] = cmin[c];
    } else if (avail <= sum_max) {
        int span = sum_max - sum_min, extra = avail - sum_min;
        for (int c = 0; c < ncols; c++)
            cwid[c] = cmin[c] + (span ? (cmax[c] - cmin[c]) * extra / span : 0);
    } else {
        int extra = avail - sum_max;
        for (int c = 0; c < ncols; c++) cwid[c] = cmax[c] + extra / ncols;
    }

    int total = gap;
    for (int c = 0; c < ncols; c++) total += cwid[c];
    if (!explicit_w && total > cbw) total = cbw;

    int ml = t->ml == M_AUTO ? 0 : t->ml;
    int mr = t->mr == M_AUTO ? 0 : t->mr;
    t->x = cbx + ml;
    if (t->ml == M_AUTO && t->mr == M_AUTO && total < cbw) t->x = cbx + (cbw - total) / 2;
    t->y = cby;
    t->cw = total;
    t->w = total + t->pl + t->pr + t->bl + t->br;

    int x0 = t->x + t->bl + t->pl;
    int y0 = t->y + t->bt + t->pt;

    int caph = 0;
    for (Box *c = t->first; c; c = c->next)
        if (c->role == BX_TCAP && !c->hide) {
            lay_block_content(c, x0, y0, total, c);
            caph += c->h;
        }

    {
        int x = x0 + sp;
        for (int c = 0; c < ncols; c++) { cx[c] = x; x += cwid[c] + sp; }
    }

    int y = y0 + caph + sp;
    for (int r = 0; r < nrows; r++) {
        ry[r] = y;
        int maxh = 0;
        for (int i = 0; i < sl.n; i++) {
            Slot *s = sl.v + i;
            if (s->row != r) continue;
            int w = 0;
            for (int k = 0; k < s->cs && s->col + k < ncols; k++) {
                w += cwid[s->col + k];
                if (k) w += sp;
            }
            lay_block_content(s->cell, cx[s->col], y, w, s->cell);
            if (s->cell->h > maxh) maxh = s->cell->h;
        }
        rh[r] = maxh;
        y += maxh + sp;
    }

    for (int i = 0; i < sl.n; i++) {
        Slot *s = sl.v + i;
        if (s->rs <= 1) continue;
        int have = sp * (s->rs - 1);
        for (int r = s->row; r < s->row + s->rs && r < nrows; r++) have += rh[r];
        if (s->cell->h > have) {
            int add = (s->cell->h - have) / s->rs;
            for (int r = s->row; r < s->row + s->rs && r < nrows; r++) rh[r] += add;
        }
    }

    y = y0 + caph + sp;
    for (int r = 0; r < nrows; r++) {
        ry[r] = y;
        for (int i = 0; i < sl.n; i++) {
            Slot *s = sl.v + i;
            if (s->row != r) continue;
            int w = 0;
            for (int k = 0; k < s->cs && s->col + k < ncols; k++) {
                w += cwid[s->col + k];
                if (k) w += sp;
            }
            lay_block_content(s->cell, cx[s->col], y, w, s->cell);
            int extra = rh[r] - s->cell->h;
            if (extra > 0) {
                if (s->cell->vval == VA_MIDDLE) s->cell->y += extra / 2;
                else if (s->cell->vval == VA_BOTTOM) s->cell->y += extra;
            }
        }
        y += rh[r] + sp;
    }

    t->ch = y - y0;
    if (t->ch < 0) t->ch = 0;
    t->h = t->ch + t->pt + t->pb + t->bt + t->bb;

    free(sl.v);
    free(cmin);
    free(cmax);
    free(cwid);
    free(cx);
    free(rh);
    free(ry);
}

void table_lay(Box *b, int cbx, int cby, int cbw) {
    if (b->role == BX_TABLE) { lay_table(b, cbx, cby, cbw); return; }
    lay_block_content(b, cbx, cby, cbw, b);
}
