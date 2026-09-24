#include "peek.h"
#include "box.h"

enum { IT_TEXT, IT_SPACE, IT_BREAK, IT_BOX };

typedef struct {
    const char *s;
    int len;
    Box *b;
    int w, h;
    uint8_t type;
    uint8_t brk;
} Item;

typedef struct { Item *v; int n, cap; } Items;

static void ipush(Items *it, const char *s, int len, Box *b, int w, int h, int type, int brk) {
    if (it->n >= it->cap) {
        it->cap = it->cap ? it->cap * 2 : 64;
        Item *p = realloc(it->v, (size_t)it->cap * sizeof(Item));
        if (!p) oom();
        it->v = p;
    }
    Item *x = it->v + it->n++;
    x->s = s; x->len = len; x->b = b; x->w = w; x->h = h;
    x->type = (uint8_t)type; x->brk = (uint8_t)brk;
}

static void ifree(Items *it) { free(it->v); it->v = 0; it->n = it->cap = 0; }

static const char *sv(Node *n, const Prop *p) {
    if (!n) return 0;
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p == p) return n->st[i].v;
    return 0;
}

static int style_len(Box *b, const Prop *p, int base, int *is_auto) {
    *is_auto = 0;
    const char *v = sv(b->n, p);
    if (!v) return 0;
    Len l;
    if (!len_parse(v, v + strlen(v), &l)) return 0;
    if (l.u == U_AUTO) { *is_auto = 1; return 0; }
    return len_px(l, base, b->font);
}

static void text_items(Box *tb, Items *out) {
    const char *s = tb->text;
    int n = tb->tlen;
    int ws = tb->ws;
    if (!s || n <= 0) return;

    if (ws == WS_PRE || ws == WS_PREWRAP) {
        int start = 0;
        for (int i = 0; i < n; i++)
            if (s[i] == '\n') {
                if (i > start)
                    ipush(out, s + start, i - start, tb,
                          vx_text_px(s + start, i - start, tb->font), tb->lh, IT_TEXT, 0);
                ipush(out, 0, 0, tb, 0, 0, IT_BREAK, 0);
                start = i + 1;
            }
        if (n > start)
            ipush(out, s + start, n - start, tb,
                  vx_text_px(s + start, n - start, tb->font), tb->lh, IT_TEXT, 0);
        return;
    }

    int i = 0, brk = 0;
    while (i < n) {
        if (ISWS(s[i])) {
            int saw_nl = 0;
            while (i < n && ISWS(s[i])) { if (s[i] == '\n') saw_nl = 1; i++; }
            if (ws == WS_PRELINE && saw_nl) {
                ipush(out, 0, 0, tb, 0, 0, IT_BREAK, 0);
                brk = 0;
            } else {
                ipush(out, " ", 1, tb, vx_cell_w(), tb->lh, IT_SPACE, 0);
                brk = 0;
            }
            continue;
        }
        int start = i;
        if (vx_chcols(s, i, n) == 2) {
            i += vx_chlen(s, i, n);
        } else {
            while (i < n && !ISWS(s[i])) {
                if (vx_chcols(s, i, n) == 2) break;
                int l = vx_chlen(s, i, n);
                if (l <= 0) { i++; break; }
                char last = s[i + l - 1];
                i += l;
                if (l == 1 && (last == '-' || last == '/')) break;
            }
        }
        if (i == start) i++;
        ipush(out, s + start, i - start, tb,
              vx_text_px(s + start, i - start, tb->font), tb->lh, IT_TEXT, brk);
        brk = 1;
    }
}

static void measure_items(const Items *it, int which, int *w, int *h) {
    int line = 0, best = 0, maxh = 0, run = 0;
    for (int i = 0; i < it->n; i++) {
        const Item *x = it->v + i;
        if (x->h > maxh) maxh = x->h;
        if (x->type == IT_BREAK) {
            if (line > best) best = line;
            if (run > best) best = run;
            line = run = 0;
            continue;
        }
        if (x->type == IT_SPACE) {
            if (which) line += x->w;
            if (run > best) best = run;
            run = 0;
            continue;
        }
        if (x->brk && run > best) best = run;
        line += x->w;
        run += x->w;
        if (!which && run > best) best = run;
    }
    if (line > best) best = line;
    if (run > best) best = run;
    *w = best;
    *h = maxh > vx_cell_h() ? maxh : vx_cell_h();
}

typedef struct { int x, y, w, h; uint8_t side; } FloatRec;
typedef struct { FloatRec *v; int n, cap; } Floats;

static void fpush(Floats *f, int x, int y, int w, int h, int side) {
    if (f->n >= f->cap) {
        f->cap = f->cap ? f->cap * 2 : 16;
        FloatRec *p = realloc(f->v, (size_t)f->cap * sizeof(FloatRec));
        if (!p) oom();
        f->v = p;
    }
    FloatRec *r = f->v + f->n++;
    r->x = x; r->y = y; r->w = w; r->h = h; r->side = (uint8_t)side;
}

static void avail_at(Floats *f, int cbx, int cbw, int y, int *ax, int *aw) {
    int l = cbx, r = cbx + cbw;
    if (f)
        for (int i = 0; i < f->n; i++) {
            FloatRec *q = f->v + i;
            if (y >= q->y && y < q->y + q->h) {
                if (q->side == FL_LEFT) { if (q->x + q->w > l) l = q->x + q->w; }
                else if (q->x < r) r = q->x;
            }
        }
    if (r < l) r = l;
    *ax = l;
    *aw = r - l;
}

static int clear_y(Floats *f, int y, int clr) {
    int best = y;
    for (int i = 0; i < f->n; i++) {
        FloatRec *q = f->v + i;
        if (clr == 3 || (clr == 1 && q->side == FL_LEFT) || (clr == 2 && q->side == FL_RIGHT))
            if (q->y + q->h > best) best = q->y + q->h;
    }
    return best;
}

typedef struct { Box *b; Box *cb; } AbsRec;
static AbsRec *ABSR;
static int NABSR, CABSR;

static void abs_push(Box *b, Box *cb) {
    if (NABSR >= CABSR) {
        CABSR = CABSR ? CABSR * 2 : 16;
        AbsRec *p = realloc(ABSR, (size_t)CABSR * sizeof(AbsRec));
        if (!p) oom();
        ABSR = p;
    }
    ABSR[NABSR].b = b;
    ABSR[NABSR].cb = cb ? cb : b->par;
    NABSR++;
}

static void lay_box_at(Box *b, int cbx, int cby, int cbw, Box *cbanc);
static int  shrink_to_fit(Box *b, int avail);
static void intrinsic(Box *b, int which, int *w, int *h);
static int  est_bfc(Box *b);
static void lay_block_kids(Box *b, Box *cbanc);

static void flatten_rec(Box *c, Items *out, int avail, Box *cbanc, int mode) {
    if (c->hide) return;
    if (c->pos == POS_ABS || c->pos == POS_FIXED) {
        if (mode < 0) abs_push(c, cbanc);
        return;
    }
    if (c->flt != FL_NONE) return;

    switch (c->role) {
        case BX_TEXT:
            text_items(c, out);
            return;
        case BX_BR:
            ipush(out, 0, 0, c, 0, 0, IT_BREAK, 0);
            return;
        case BX_IMG: {
            int w = c->attr_w != M_AUTO ? c->attr_w : 10 * vx_cell_w();
            int h = c->attr_h != M_AUTO ? c->attr_h : vx_cell_h();
            ipush(out, 0, 0, c, w, h, IT_BOX, 0);
            return;
        }
        case BX_IBLOCK: case BX_FLEX: case BX_GRID: case BX_TABLE: {
            if (mode < 0) {
                int w = shrink_to_fit(c, avail);
                int ml = c->ml == M_AUTO ? 0 : c->ml;
                int mr = c->mr == M_AUTO ? 0 : c->mr;
                lay_box_at(c, 0, 0, w + ml + mr, cbanc);
                ipush(out, 0, 0, c, c->w, c->h > c->lh ? c->h : c->lh, IT_BOX, 0);
            } else {
                int w, h;
                intrinsic(c, mode, &w, &h);
                ipush(out, 0, 0, c, w, h, IT_BOX, 0);
            }
            return;
        }
        default:
            for (Box *g = c->first; g; g = g->next) flatten_rec(g, out, avail, cbanc, mode);
            return;
    }
}

static void line_push(Line *ln, Frag fr) {
    if (ln->nfrag >= ln->fcap) {
        ln->fcap = ln->fcap ? ln->fcap * 2 : 8;
        Frag *p = realloc(ln->frag, (size_t)ln->fcap * sizeof(Frag));
        if (!p) oom();
        ln->frag = p;
    }
    ln->frag[ln->nfrag++] = fr;
}

static void box_add_line(Box *b, Line *ln) {
    if (b->nline >= b->lcap) {
        b->lcap = b->lcap ? b->lcap * 2 : 4;
        Line *p = realloc(b->lines, (size_t)b->lcap * sizeof(Line));
        if (!p) oom();
        b->lines = p;
    }
    b->lines[b->nline++] = *ln;
    memset(ln, 0, sizeof *ln);
}

static void clear_lines(Box *b) {
    for (int i = 0; i < b->nline; i++) free(b->lines[i].frag);
    b->nline = 0;
}

static int line_width(const Line *L) {
    int last = L->nfrag;
    while (last > 0 && L->frag[last - 1].space) last--;
    int w = 0;
    for (int k = 0; k < last; k++) w += L->frag[k].w;
    return w;
}

static void flush_line(Box *b, Line *ln, int x, int y, int *yp) {
    ln->x = x;
    ln->y = y;
    ln->w = line_width(ln);
    int h = b->lh;
    for (int k = 0; k < ln->nfrag; k++)
        if (ln->frag[k].h > h) h = ln->frag[k].h;
    int rows = (h + vx_cell_h() - 1) / vx_cell_h();
    if (rows < 1) rows = 1;
    ln->h = rows * vx_cell_h();
    ln->baseline = y + ln->h;
    int next_y = y + ln->h;
    box_add_line(b, ln);
    *yp = next_y;
}

static void frag_of(Box *owner, const Item *x, int xpos, Frag *fr) {
    memset(fr, 0, sizeof *fr);
    fr->s = x->s;
    fr->len = x->len;
    fr->x = xpos;
    fr->w = x->w;
    fr->h = x->h;
    fr->fg = owner ? owner->fgcolor : -1;
    fr->bg = owner ? owner->bgcolor : -1;
    fr->fl = owner ? owner->tflag : 0;
    fr->font = owner ? owner->font : vx_font_default();
    fr->replaced = (uint8_t)(x->type == IT_BOX);
    fr->space = (uint8_t)(x->type == IT_SPACE);
}

static int lay_ifc(Box *b, int cx, int cy, int cw, Floats *fl, Box *cbanc) {
    clear_lines(b);

    Items it = {0};
    for (Box *c = b->first; c; c = c->next) flatten_rec(c, &it, cw, cbanc, -1);
    if (getenv("PEEK_DBG"))
        fprintf(stderr, "[ifc %s] kids_items=%d cw=%d first=%p\n",
                b->n ? b->n->tag : "anon", it.n, cw, (void *)b->first);

    int y = cy;
    int used = 0;
    int bi = -1, bu = 0, bn = 0;
    int indent = b->text_indent;
    Line ln;
    memset(&ln, 0, sizeof ln);

    for (int i = 0; i < it.n; i++) {
        Item *x = it.v + i;

        if (x->type == IT_BREAK) {
            if (ln.nfrag) {
                int ax, aw;
                avail_at(fl, cx, cw, y, &ax, &aw);
                flush_line(b, &ln, ax, y, &y);
            }
            used = 0; bi = -1; bn = 0; indent = 0;
            continue;
        }

        int ax, aw;
        avail_at(fl, cx, cw, y, &ax, &aw);
        int ex = (ln.nfrag || used) ? 0 : indent;
        int room = aw - ex;
        if (room < vx_cell_w()) room = vx_cell_w();

        if (x->type == IT_SPACE) {
            if (!ln.nfrag) continue;
            bi = i;
            bu = used;
            bn = ln.nfrag;
            Frag fr;
            frag_of(x->b, x, ax + ex + used, &fr);
            line_push(&ln, fr);
            used += x->w;
            continue;
        }

        if (used + x->w > room && ln.nfrag) {
            if (bi >= 0 && bn > 0) {
                ln.nfrag = bn;
                used = bu;
                flush_line(b, &ln, ax, y, &y);
                used = 0;
                indent = 0;
                i = bi;
                bi = -1; bn = 0;
                continue;
            }
            flush_line(b, &ln, ax, y, &y);
            used = 0;
            indent = 0;
            i--;
            continue;
        }

        Frag fr;
        frag_of(x->b, x, ax + ex + used, &fr);
        if (x->type == IT_BOX && x->b) {
            Box *rb = x->b;
            if (rb->role == BX_IBLOCK || rb->role == BX_FLEX || rb->role == BX_GRID || rb->role == BX_TABLE) {
                int ml = rb->ml == M_AUTO ? 0 : rb->ml;
                int mr = rb->mr == M_AUTO ? 0 : rb->mr;
                lay_box_at(rb, fr.x, y, x->w + ml + mr, cbanc);
                fr.x = rb->x;
                fr.w = rb->w;
                fr.h = rb->h;
            }
        }
        line_push(&ln, fr);
        used += x->w;
    }

    if (ln.nfrag) {
        int ax, aw;
        avail_at(fl, cx, cw, y, &ax, &aw);
        flush_line(b, &ln, ax, y, &y);
    } else if (ln.frag) {
        free(ln.frag);
    }

    for (int i = 0; i < b->nline; i++) {
        Line *L = b->lines + i;
        int ax, aw;
        avail_at(fl, cx, cw, L->y, &ax, &aw);
        int w = line_width(L);
        int slack = aw - w;
        if (slack <= 0) continue;

        int cells = slack / vx_cell_w();
        if (b->talign == TA_CENTER) {
            int dx = (cells / 2) * vx_cell_w();
            for (int k = 0; k < L->nfrag; k++) L->frag[k].x += dx;
        } else if (b->talign == TA_RIGHT) {
            int dx = cells * vx_cell_w();
            for (int k = 0; k < L->nfrag; k++) L->frag[k].x += dx;
        } else if (b->talign == TA_JUSTIFY && i + 1 < b->nline) {
            int nsp = 0;
            for (int k = 0; k < L->nfrag; k++) if (L->frag[k].space) nsp++;
            if (nsp) {
                int each = (cells / nsp) * vx_cell_w(), acc = 0;
                for (int k = 0; k < L->nfrag; k++) {
                    L->frag[k].x += acc;
                    if (L->frag[k].space) acc += each;
                }
            }
        }
    }

    ifree(&it);
    if (getenv("PEEK_DBG"))
        fprintf(stderr, "[ifc done] nline=%d ret=%d\n", b->nline, y - cy);
    return y - cy;
}

static void intrinsic(Box *b, int which, int *out_w, int *out_h) {
    if (b->role == BX_IMG) {
        *out_w = b->attr_w != M_AUTO ? b->attr_w : 10 * vx_cell_w();
        *out_h = b->attr_h != M_AUTO ? b->attr_h : vx_cell_h();
        return;
    }
    if (b->role == BX_BR) { *out_w = 0; *out_h = b->lh; return; }
    if (b->role == BX_TEXT) {
        Items it = {0};
        text_items(b, &it);
        measure_items(&it, which, out_w, out_h);
        ifree(&it);
        return;
    }

    int has_block = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide || c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        if (c->flt != FL_NONE) continue;
        switch (c->role) {
            case BX_TEXT: case BX_INLINE: case BX_BR: case BX_IMG: case BX_IBLOCK:
                continue;
            default:
                has_block = 1;
        }
        if (has_block) break;
    }

    if (!has_block) {
        Items it = {0};
        for (Box *c = b->first; c; c = c->next) flatten_rec(c, &it, 0, 0, which);
        measure_items(&it, which, out_w, out_h);
        ifree(&it);
        *out_w += b->pl + b->pr + b->bl + b->br;
        *out_h += b->pt + b->pb + b->bt + b->bb;
        return;
    }

    int w = 0, h = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide || c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        int cw, ch;
        intrinsic(c, which, &cw, &ch);
        if (cw > w) w = cw;
        h += ch + (c->mt == M_AUTO ? 0 : c->mt) + (c->mb == M_AUTO ? 0 : c->mb);
    }
    *out_w = w + b->pl + b->pr + b->bl + b->br;
    *out_h = h + b->pt + b->pb + b->bt + b->bb;
}

static int shrink_to_fit(Box *b, int avail) {
    int mn, mx, hh;
    intrinsic(b, 0, &mn, &hh);
    intrinsic(b, 1, &mx, &hh);
    int w = mx < avail ? mx : (mn < avail ? avail : mn);
    return w > 0 ? w : 0;
}

void box_intrinsic(Box *b, int which, int *w, int *h) { intrinsic(b, which, w, h); }
int box_shrink_to_fit(Box *b, int avail) { return shrink_to_fit(b, avail); }

static void resolve_block_size(Box *b, int cbw) {
    int is_auto = 0;
    int w = style_len(b, &P_WIDTH, cbw, &is_auto);
    if (sv(b->n, &P_WIDTH) && !is_auto) {
        if (sv(b->n, &P_BOXSIZING) && !strcmp(sv(b->n, &P_BOXSIZING), "border-box"))
            w -= b->pl + b->pr + b->bl + b->br;
        b->cw = w;
    } else if (b->attr_w != M_AUTO) {
        b->cw = b->attr_w;
    } else {
        int ml = b->ml == M_AUTO ? 0 : b->ml;
        int mr = b->mr == M_AUTO ? 0 : b->mr;
        b->cw = cbw - ml - mr - b->pl - b->pr - b->bl - b->br;
    }
    int mw = style_len(b, &P_MAXW, cbw, &is_auto);
    if (sv(b->n, &P_MAXW) && !is_auto && b->cw > mw) b->cw = mw;
    int mn = style_len(b, &P_MINW, cbw, &is_auto);
    if (sv(b->n, &P_MINW) && !is_auto && b->cw < mn) b->cw = mn;
    if (b->cw < 0) b->cw = 0;
    b->w = b->cw + b->pl + b->pr + b->bl + b->br;
}

static int est_bfc(Box *b) {
    if (b->role == BX_ROOT) return 1;
    if (b->flt != FL_NONE || b->pos == POS_ABS || b->pos == POS_FIXED) return 1;
    if (b->ovf != 0) return 1;
    if (b->role == BX_IBLOCK || b->role == BX_TCELL || b->role == BX_FLEX ||
        b->role == BX_GRID || b->role == BX_TABLE) return 1;
    return 0;
}

static int inline_level(const Box *c) {
    switch (c->role) {
        case BX_TEXT: case BX_INLINE: case BX_BR: case BX_IMG: case BX_IBLOCK:
            return 1;
        default:
            return 0;
    }
}

static int has_block_child(Box *b) {
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide) continue;
        if (c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        if (!inline_level(c)) return 1;
    }
    return 0;
}

static Box *first_in_flow(Box *b) {
    for (Box *c = b->first; c; c = c->next) {
        if (c->hide) continue;
        if (c->pos == POS_ABS || c->pos == POS_FIXED) continue;
        if (c->flt != FL_NONE) continue;
        return c;
    }
    return 0;
}

static void lay_block_kids(Box *b, Box *cbanc) {
    int cx = b->x + b->pl + b->bl;
    int cy = b->y + b->pt + b->bt;
    int cw = b->cw;

    Floats fl = {0};
    int cursor = cy;
    int prev_mb = 0;
    int first = 1;
    Box *cbfor = (b->pos != POS_STATIC) ? b : (cbanc ? cbanc : b);

    for (Box *c = b->first; c; c = c->next) {
        if (c->hide) continue;

        if (c->pos == POS_ABS || c->pos == POS_FIXED) {
            abs_push(c, cbfor);
            continue;
        }

        if (c->flt != FL_NONE) {
            int w = shrink_to_fit(c, (cw * 3) / 4);
            int ml = c->ml == M_AUTO ? 0 : c->ml;
            int mr = c->mr == M_AUTO ? 0 : c->mr;
            int mt = c->mt == M_AUTO ? 0 : c->mt;
            int ax, aw;
            avail_at(&fl, cx, cw, cursor, &ax, &aw);
            int x = (c->flt == FL_RIGHT) ? ax + aw - w - ml : ax + ml;
            lay_box_at(c, x, cursor + mt, w + ml + mr, cbfor);
            fpush(&fl, c->x, c->y, c->w, c->h, c->flt);
            continue;
        }

        if (c->clear) cursor = clear_y(&fl, cursor, c->clear);

        int mt = c->mt == M_AUTO ? 0 : c->mt;
        int gap;
        if (first) {
            gap = mt;
        } else {
            gap = mt > prev_mb ? mt : prev_mb;
        }

        int ax, aw;
        avail_at(&fl, cx, cw, cursor + gap, &ax, &aw);
        lay_box_at(c, ax, cursor + gap - mt, aw > 0 ? aw : cw, cbfor);
        cursor = c->y + c->h;
        prev_mb = c->mb == M_AUTO ? 0 : c->mb;
        first = 0;
    }

    int bottom = cursor;
    if (b->pb + b->bb != 0) {
        bottom += prev_mb;
    } else if (prev_mb > (b->mb == M_AUTO ? 0 : b->mb)) {
        b->mb = prev_mb;
    }

    int h = bottom - cy;
    if (h < 0) h = 0;
    b->ch = h;
    b->h = h + b->pt + b->pb + b->bt + b->bb;

    if (est_bfc(b)) {
        for (int i = 0; i < fl.n; i++) {
            int fb = fl.v[i].y + fl.v[i].h - cy;
            if (fb > b->ch) { b->ch = fb; b->h = fb + b->pt + b->pb + b->bt + b->bb; }
        }
    }
    free(fl.v);
}

static void lay_box_at(Box *b, int cbx, int cby, int cbw, Box *cbanc) {
    if (b->role == BX_TABLE || b->role == BX_TROWG || b->role == BX_TROW ||
        b->role == BX_TCELL || b->role == BX_TCAP) {
        table_lay(b, cbx, cby, cbw);
        return;
    }
    if (b->role == BX_FLEX) {
        b->x = cbx;
        b->y = cby;
        flex_lay(b, cbw, cbanc);
        return;
    }
    if (b->role == BX_GRID) {
        b->x = cbx;
        b->y = cby;
        grid_lay(b, cbw, cbanc);
        return;
    }
    lay_block_content(b, cbx, cby, cbw, cbanc);
}

void lay_box_dispatch(Box *b, int cbx, int cby, int cbw, Box *cbanc) {
    lay_box_at(b, cbx, cby, cbw, cbanc);
}

void lay_block_content(Box *b, int cbx, int cby, int cbw, Box *cbanc) {
    b->x = cbx;
    b->y = cby;

    if (b->role == BX_IMG) {
        b->cw = b->attr_w != M_AUTO ? b->attr_w : 10 * vx_cell_w();
        b->ch = b->attr_h != M_AUTO ? b->attr_h : vx_cell_h();
        b->w = b->cw + b->pl + b->pr;
        b->h = b->ch + b->pt + b->pb;
        b->x = cbx + (b->ml == M_AUTO ? 0 : b->ml);
        b->y = cby + (b->mt == M_AUTO ? 0 : b->mt);
        return;
    }

    resolve_block_size(b, cbw);

    b->x = cbx + (b->ml == M_AUTO ? 0 : b->ml);
    if (b->ml == M_AUTO && b->mr == M_AUTO && b->w < cbw)
        b->x = cbx + (cbw - b->w) / 2;
    b->y = cby + (b->mt == M_AUTO ? 0 : b->mt);

    int is_auto = 0;
    int fh = style_len(b, &P_HEIGHT, vx_height(), &is_auto);
    int fixed = sv(b->n, &P_HEIGHT) && !is_auto;
    if (fixed) {
        if (sv(b->n, &P_BOXSIZING) && !strcmp(sv(b->n, &P_BOXSIZING), "border-box"))
            fh -= b->pt + b->pb + b->bt + b->bb;
        b->ch = fh;
    } else {
        b->ch = 0;
    }

    Box *next_cb = (b->pos != POS_STATIC) ? b : cbanc;

    if (has_block_child(b)) {
        int keep = b->ch;
        if (!fixed) b->ch = 0;
        lay_block_kids(b, next_cb);
        if (fixed) b->ch = keep;
    } else {
        int h = lay_ifc(b, b->x + b->bl + b->pl, b->y + b->bt + b->pt, b->cw, 0, next_cb);
        if (!fixed) b->ch = h;
    }
    if (b->role == BX_HR) b->ch = 0;

    b->h = b->ch + b->pt + b->pb + b->bt + b->bb;

    int mh = style_len(b, &P_MINH, vx_height(), &is_auto);
    if (sv(b->n, &P_MINH) && !is_auto && b->h < mh) { b->h = mh; b->ch = mh - b->pt - b->pb - b->bt - b->bb; }
    mh = style_len(b, &P_MAXH, vx_height(), &is_auto);
    if (sv(b->n, &P_MAXH) && !is_auto && b->h > mh) { b->h = mh; b->ch = mh - b->pt - b->pb - b->bt - b->bb; }
    if (b->ch < 0) b->ch = 0;

    if (b->pos == POS_REL) {
        int a;
        int t = style_len(b, &P_TOP, vx_height(), &a);
        if (sv(b->n, &P_TOP) && !a) b->y += t;
        int l = style_len(b, &P_LEFT, cbw, &a);
        if (sv(b->n, &P_LEFT) && !a) b->x += l;
    }
}

static void lay_abs(void) {
    for (int i = 0; i < NABSR; i++) {
        Box *b = ABSR[i].b;
        Box *cb = ABSR[i].cb;
        int cx, cy, cw, chh;
        if (b->pos == POS_FIXED || !cb) {
            cx = 0; cy = 0; cw = vx_width(); chh = vx_height();
        } else {
            cx = cb->x + cb->bl + cb->pl;
            cy = cb->y + cb->bt + cb->pt;
            cw = cb->cw;
            chh = cb->ch;
        }
        int a = 0;
        int w = style_len(b, &P_WIDTH, cw, &a);
        if (!sv(b->n, &P_WIDTH) || a) w = shrink_to_fit(b, cw);
        int ml = b->ml == M_AUTO ? 0 : b->ml;
        int mr = b->mr == M_AUTO ? 0 : b->mr;

        int x = cx, y = cy;
        int lv = style_len(b, &P_LEFT, cw, &a);
        int have_l = sv(b->n, &P_LEFT) && !a;
        int rv = style_len(b, &P_RIGHT, cw, &a);
        int have_r = sv(b->n, &P_RIGHT) && !a;
        int tv = style_len(b, &P_TOP, chh, &a);
        int have_t = sv(b->n, &P_TOP) && !a;
        int bv = style_len(b, &P_BOTTOM, chh, &a);
        int have_b = sv(b->n, &P_BOTTOM) && !a;

        if (have_l) x = cx + lv;
        else if (have_r) x = cx + cw - rv - w - ml - mr;
        y = cy + (b->mt == M_AUTO ? 0 : b->mt);
        if (have_t) y = cy + tv;

        lay_box_at(b, x - ml, y - (b->mt == M_AUTO ? 0 : b->mt), w + ml + mr, cb);

        if (have_b && !have_t) {
            int hh = b->h;
            b->y = cy + chh - bv - hh;
        }
    }
    NABSR = 0;
}

void lay_layout(Box *root) {
    root->x = 0;
    root->y = 0;
    root->cw = vx_width();
    root->w = vx_width();
    root->mt = root->mr = root->mb = root->ml = 0;
    root->ch = 0;

    int guard = 0;
    do {
        if (has_block_child(root)) {
            root->ch = 0;
            lay_block_kids(root, root);
        } else {
            root->ch = lay_ifc(root, root->pl, root->pt, root->cw, 0, root);
        }
        root->h = root->ch;
        lay_abs();
    } while (NABSR && ++guard < 8);
}
