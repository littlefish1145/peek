#include "peek.h"
#include "box.h"

static const char *sp(Box *b, const Prop *p) {
    if (!b->n) return 0;
    for (int i = 0; i < b->n->nst; i++)
        if (b->n->st[i].p == p) return b->n->st[i].v;
    return 0;
}

static int len_of(Box *b, const Prop *p, int base) {
    const char *v = sp(b, p);
    if (!v) return -1;
    Len l;
    if (!len_parse(v, v + strlen(v), &l) || l.u == U_AUTO) return -1;
    return len_px(l, base, b->font);
}

static int m0_of(Box *b, int row) { return row ? (b->ml == M_AUTO ? 0 : b->ml) : (b->mt == M_AUTO ? 0 : b->mt); }
static int m1_of(Box *b, int row) { return row ? (b->mr == M_AUTO ? 0 : b->mr) : (b->mb == M_AUTO ? 0 : b->mb); }
static int c0_of(Box *b, int row) { return row ? (b->mt == M_AUTO ? 0 : b->mt) : (b->ml == M_AUTO ? 0 : b->ml); }
static int c1_of(Box *b, int row) { return row ? (b->mb == M_AUTO ? 0 : b->mb) : (b->mr == M_AUTO ? 0 : b->mr); }

static void collect(Box *b, Box ***out, int *n) {
    int k = 0;
    for (Box *c = b->first; c; c = c->next)
        if (!c->hide && c->pos != POS_ABS && c->pos != POS_FIXED) k++;
    Box **v = k ? malloc((size_t)k * sizeof(Box *)) : 0;
    if (k && !v) oom();
    k = 0;
    for (Box *c = b->first; c; c = c->next)
        if (!c->hide && c->pos != POS_ABS && c->pos != POS_FIXED) v[k++] = c;

    for (int i = 1; i < k; i++) {
        Box *cur = v[i];
        int j = i - 1;
        while (j >= 0 && v[j]->order > cur->order) { v[j + 1] = v[j]; j--; }
        v[j + 1] = cur;
    }
    *out = v;
    *n = k;
}

typedef struct {
    Box *b;
    int base;
    int m0, m1, c0, c1;
    int main, cross;
} FItem;

void flex_lay(Box *b, int cbw, Box *cbanc) {
    int row = (b->fdir == 0 || b->fdir == 1);
    int rev = (b->fdir == 1 || b->fdir == 3);
    int wrap = (b->fwrap == 1 || b->fwrap == 2);

    if (row) {
        b->cw = cbw - b->pl - b->pr - b->bl - b->br - (b->ml == M_AUTO ? 0 : 0);
        if (b->cw < 0) b->cw = 0;
    } else if (b->cw <= 0) {
        b->cw = cbw - b->pl - b->pr - b->bl - b->br;
        if (b->cw < 0) b->cw = 0;
    }
    b->w = b->cw + b->pl + b->pr + b->bl + b->br;

    int x0 = b->x + b->bl + b->pl;
    int y0 = b->y + b->bt + b->pt;

    Box **kids;
    int n;
    collect(b, &kids, &n);
    if (!n) {
        b->ch = 0;
        b->h = b->pt + b->pb + b->bt + b->bb;
        free(kids);
        return;
    }

    FItem *it = calloc((size_t)n, sizeof(FItem));
    if (!it) oom();
    for (int i = 0; i < n; i++) {
        Box *c = kids[i];
        it[i].b = c;
        it[i].m0 = m0_of(c, row);
        it[i].m1 = m1_of(c, row);
        it[i].c0 = c0_of(c, row);
        it[i].c1 = c1_of(c, row);
        int base = c->basis != M_AUTO ? c->basis
                 : len_of(c, row ? &P_WIDTH : &P_HEIGHT, row ? b->cw : vx_height());
        if (base < 0) {
            int mx, hh;
            box_intrinsic(c, 1, &mx, &hh);
            base = row ? mx : hh;
        }
        it[i].base = base;
    }

    int gap = len_of(b, &P_GAP, row ? b->cw : vx_height());
    if (gap < 0) gap = 0;

    int avail = row ? b->cw : 0;
    if (!row) {
        int h = len_of(b, &P_HEIGHT, vx_height());
        avail = h >= 0 ? h - b->pt - b->pb - b->bt - b->bb : -1;
    }

    int *ls = calloc((size_t)n + 1, sizeof(int));
    int *le = calloc((size_t)n + 1, sizeof(int));
    if (!ls || !le) oom();
    int nlines = 0;
    for (int s = 0; s < n;) {
        int e = s, used = 0;
        while (e < n) {
            int need = it[e].base + it[e].m0 + it[e].m1 + (e > s ? gap : 0);
            if (wrap && avail > 0 && e > s && used + need > avail) break;
            used += need;
            e++;
        }
        ls[nlines] = s;
        le[nlines] = e;
        nlines++;
        s = e;
        if (!wrap) break;
    }

    int ymain = y0;
    int cross_total = 0;

    for (int li = 0; li < nlines; li++) {
        int s = ls[li], e = le[li], cnt = e - s;
        if (cnt <= 0) continue;

        long long nat = (long long)gap * (cnt - 1);
        int grow_sum = 0;
        long long shrink_w = 0;
        for (int i = s; i < e; i++) {
            nat += it[i].base + it[i].m0 + it[i].m1;
            grow_sum += it[i].b->grow;
            shrink_w += (long long)it[i].b->shrink * it[i].base;
        }

        if (avail > 0) {
            int free_sp = (int)(avail - nat);
            for (int i = s; i < e; i++) {
                long long v = it[i].base;
                if (free_sp > 0 && grow_sum > 0)
                    v += (long long)free_sp * it[i].b->grow / grow_sum;
                else if (free_sp < 0 && shrink_w > 0)
                    v += (long long)free_sp * it[i].b->shrink * it[i].base / shrink_w;
                it[i].main = v > 0 ? (int)v : 0;
            }
            long long sum = (long long)gap * (cnt - 1);
            for (int i = s; i < e; i++) sum += it[i].main + it[i].m0 + it[i].m1;
            it[e - 1].main += (int)(avail - sum);
            if (it[e - 1].main < 0) it[e - 1].main = 0;
        } else {
            for (int i = s; i < e; i++) it[i].main = it[i].base;
        }

        long long used = (long long)gap * (cnt - 1);
        for (int i = s; i < e; i++) used += it[i].main + it[i].m0 + it[i].m1;
        int spare = avail > 0 ? (int)(avail - used) : 0;
        int pos = 0, between = gap;
        if (spare > 0) {
            if (b->jc == 1) pos = spare / 2;
            else if (b->jc == 2) pos = spare;
            else if (b->jc == 3 && cnt > 1) between = gap + spare / (cnt - 1);
            else if (b->jc == 4) { between = gap + spare / cnt; pos = spare / (2 * cnt); }
            else if (b->jc == 5 && cnt > 1) between = gap + spare / (cnt - 1);
        }

        int cursor = pos;
        int cross_max = 0;
        for (int i = s; i < e; i++) {
            Box *c = it[i].b;
            int mp = (rev ? (avail > 0 ? avail - cursor - it[i].main - it[i].m0 - it[i].m1 : cursor)
                          : cursor) + it[i].m0;
            int cx = row ? x0 + mp : x0 + it[i].c0;
            int cy = row ? ymain + it[i].c0 : y0 + mp;
            int mw = it[i].main + it[i].m0 + it[i].m1;
            if (row) lay_box_dispatch(c, cx, cy, mw, cbanc);
            else {
                c->w = it[i].main;
                c->cw = it[i].main - c->pl - c->pr - c->bl - c->br;
                if (c->cw < 0) c->cw = 0;
                lay_box_dispatch(c, cx, cy, c->cw, cbanc);
            }
            it[i].cross = row ? c->h : c->w;
            if (it[i].cross > cross_max) cross_max = it[i].cross;
            cursor += it[i].main + it[i].m0 + it[i].m1 + between;
        }

        for (int i = s; i < e; i++) {
            Box *c = it[i].b;
            int align = c->as ? c->as : b->ai;
            if (align == 3 || align == 0) {
                if (row) {
                    int want = cross_max - it[i].c0 - it[i].c1;
                    if (want > c->h) { c->h = want; c->ch = want - c->pt - c->pb - c->bt - c->bb; }
                }
            }
        }
        cross_total += cross_max;
        ymain += cross_max;
    }

    if (!row) {
        int h = len_of(b, &P_HEIGHT, vx_height());
        if (h < 0) b->ch = cross_total + gap * (nlines - 1);
        else b->ch = h - b->pt - b->pb - b->bt - b->bb;
    } else {
        if (!b->ch) b->ch = cross_total + gap * (nlines - 1);
    }
    if (b->ch < 0) b->ch = 0;
    b->h = b->ch + b->pt + b->pb + b->bt + b->bb;

    free(ls);
    free(le);
    free(it);
    free(kids);
}

enum { TR_PX, TR_PCT, TR_FR, TR_AUTO };

typedef struct { uint8_t kind; int v; } Track;

typedef struct { Track *v; int n; } Tracks;

static void tr_push(Tracks *t, Track x) {
    Track *p = realloc(t->v, (size_t)(t->n + 1) * sizeof(Track));
    if (!p) oom();
    t->v = p;
    t->v[t->n++] = x;
}

static void parse_tracks(const char *v, Tracks *out, int base) {
    const char *p = v;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (!pk_strncasecmp(p, "repeat(", 7)) {
            const char *q = p + 7;
            int rep = atoi(q);
            const char *cm = strchr(q, ',');
            const char *cl = cm ? strchr(cm, ')') : 0;
            if (!cm || !cl) break;
            char *inner = sdup(cm + 1, (size_t)(cl - cm - 1));
            Tracks sub = {0};
            parse_tracks(inner, &sub, base);
            free(inner);
            for (int r = 0; r < (rep > 0 ? rep : 1); r++)
                for (int i = 0; i < sub.n; i++) tr_push(out, sub.v[i]);
            free(sub.v);
            p = cl + 1;
            continue;
        }
        Track tr = {TR_AUTO, 0};
        if (!pk_strncasecmp(p, "minmax(", 7)) {
            const char *cm = strchr(p, ',');
            const char *cl = strchr(p, ')');
            if (cm) {
                const char *q = p + 7;
                Len l;
                while (*q == ' ') q++;
                if (len_parse(q, cm, &l) && l.u != U_AUTO) {
                    tr.kind = l.u == U_PCT ? TR_PCT : TR_PX;
                    tr.v = l.u == U_PCT ? l.v : len_px(l, base, vx_font_default());
                }
            }
            p = cl ? cl + 1 : p + strlen(p);
        } else if (!pk_strncasecmp(p, "auto", 4)) { p += 4; }
        else if (!pk_strncasecmp(p, "min-content", 11)) { p += 11; }
        else if (!pk_strncasecmp(p, "max-content", 11)) { p += 11; }
        else {
            const char *s = p;
            while (*p && *p != ' ') p++;
            if (p - s >= 2 && (!pk_strncasecmp(p - 2, "fr", 2))) {
                char tmp[32];
                size_t l = (size_t)(p - s - 2);
                if (l > 30) l = 30;
                memcpy(tmp, s, l);
                tmp[l] = 0;
                tr.kind = TR_FR;
                tr.v = (int)(atof(tmp) * 1000);
            } else {
                Len l;
                if (len_parse(s, p, &l) && l.u != U_AUTO) {
                    tr.kind = l.u == U_PCT ? TR_PCT : TR_PX;
                    tr.v = l.u == U_PCT ? l.v : len_px(l, base, vx_font_default());
                }
            }
        }
        tr_push(out, tr);
    }
}

static int track_px(const Track *t, int base, int fr_share) {
    switch (t->kind) {
        case TR_PX: return t->v;
        case TR_PCT: return (int)((long long)base * t->v / 6400);
        case TR_FR: return (int)((long long)fr_share * t->v / 1000);
        default: return 0;
    }
}

void grid_lay(Box *b, int cbw, Box *cbanc) {
    b->cw = cbw - b->pl - b->pr - b->bl - b->br;
    if (b->cw < 0) b->cw = 0;
    b->w = b->cw + b->pl + b->pr + b->bl + b->br;
    int x0 = b->x + b->bl + b->pl;
    int y0 = b->y + b->bt + b->pt;

    Box **kids;
    int n;
    collect(b, &kids, &n);
    if (!n) {
        b->ch = 0;
        b->h = b->pt + b->pb + b->bt + b->bb;
        free(kids);
        return;
    }

    Tracks cols = {0}, rows = {0};
    const char *gv = sp(b, &P_GTC);
    if (gv) parse_tracks(gv, &cols, b->cw);
    if (!cols.n) { Track a = {TR_FR, 1000}; tr_push(&cols, a); }
    gv = sp(b, &P_GTR);
    if (gv) parse_tracks(gv, &rows, vx_height());

    int gap = len_of(b, &P_GAP, b->cw);
    if (gap < 0) gap = 0;

    int nc = cols.n;
    int nr = rows.n;

    int *ipc = calloc((size_t)n, sizeof(int));
    int *ipr = calloc((size_t)n, sizeof(int));
    if (!ipc || !ipr) oom();

    int gmaxr = n + nr + 2;
    int gmaxc = nc + n + 2;
    unsigned char *occ = calloc((size_t)(gmaxr * gmaxc), 1);
    if (!occ) oom();
#define OCC(r, c) occ[(r) * gmaxc + (c)]

    int auto_r = 0, auto_c = 0;
    for (int i = 0; i < n; i++) {
        Box *c = kids[i];
        int span = c->gcs > 0 ? c->gcs : 1;
        int row, col;
        if (c->gcol > 0) {
            col = c->gcol - 1;
            row = c->grw > 0 ? c->grw - 1 : auto_r;
            for (;; row++) {
                if (row + 1 > gmaxr) { row = gmaxr > 0 ? gmaxr - 1 : 0; break; }
                int free_run = 1;
                for (int k = 0; k < span; k++)
                    if (col + k >= gmaxc || OCC(row, col + k)) { free_run = 0; break; }
                if (free_run) break;
            }
        } else {
            row = auto_r;
            col = auto_c;
            for (;;) {
                if (col + span > nc || col + span > gmaxc) { col = 0; row++; continue; }
                if (row + 1 > gmaxr) { row = gmaxr > 0 ? gmaxr - 1 : 0; col = 0; break; }
                int free_run = 1;
                for (int k = 0; k < span; k++) if (OCC(row, col + k)) { free_run = 0; break; }
                if (free_run) break;
                col++;
            }
            auto_r = row;
            auto_c = col + span;
            if (auto_c >= nc) { auto_c = 0; auto_r = row + 1; }
        }
        ipc[i] = col;
        ipr[i] = row;
        for (int k = 0; k < span && col + k < gmaxc; k++) OCC(row, col + k) = 1;
    }
#undef OCC
    free(occ);

    for (int i = 0; i < n; i++) {
        if (ipc[i] + 1 > nc) nc = ipc[i] + 1;
        if (ipr[i] + 1 > nr) nr = ipr[i] + 1;
    }
    while (cols.n < nc) { Track a = {TR_AUTO, 0}; tr_push(&cols, a); }
    while (rows.n < nr) { Track a = {TR_AUTO, 0}; tr_push(&rows, a); }

    int *cwid = calloc((size_t)nc, sizeof(int));
    int *rhei = calloc((size_t)nr, sizeof(int));
    if (!cwid || !rhei) oom();

    long long fr_sum = 0;
    int fixed_w = 0;
    for (int c = 0; c < nc; c++) {
        if (cols.v[c].kind == TR_FR) fr_sum += cols.v[c].v;
        else { cwid[c] = track_px(cols.v + c, b->cw, 0); fixed_w += cwid[c]; }
    }
    int gaps_w = gap * (nc - 1);
    int fr_space = b->cw - fixed_w - gaps_w;
    if (fr_space < 0) fr_space = 0;
    for (int c = 0; c < nc; c++)
        if (cols.v[c].kind == TR_FR)
            cwid[c] = fr_sum ? (int)((long long)fr_space * cols.v[c].v / fr_sum) : 0;

    long long fr_sum_r = 0;
    for (int r = 0; r < nr; r++)
        if (rows.v[r].kind == TR_FR) fr_sum_r += rows.v[r].v;

    int *cx = calloc((size_t)nc, sizeof(int));
    if (!cx) oom();
    {
        int x = x0;
        for (int c = 0; c < nc; c++) { cx[c] = x; x += cwid[c] + gap; }
    }

    for (int pass = 0; pass < 2; pass++) {
        int y = y0;
        for (int r = 0; r < nr; r++) {
            int rh = 0;
            for (int i = 0; i < n; i++) {
                if (ipr[i] != r) continue;
                int w = 0, span = kids[i]->gcs > 0 ? kids[i]->gcs : 1;
                for (int k = 0; k < span && ipc[i] + k < nc; k++) {
                    w += cwid[ipc[i] + k];
                    if (k) w += gap;
                }
                if (pass == 1) lay_box_dispatch(kids[i], cx[ipc[i]], y, w, cbanc);
                else box_intrinsic(kids[i], 0, &w, &rh);
                int hh = pass == 1 ? kids[i]->h : rh;
                if (hh > rh) rh = hh;
            }
            rhei[r] = rh;
            y += rh + gap;
        }
        if (pass == 1) break;
    }

    for (int r = 0; r < nr; r++) {
        if (rows.v[r].kind == TR_FR) continue;
        int hv = track_px(rows.v + r, vx_height(), 0);
        if (hv > rhei[r]) rhei[r] = hv;
    }

    {
        int y = y0;
        int *ry = calloc((size_t)nr, sizeof(int));
        if (!ry) oom();
        for (int r = 0; r < nr; r++) { ry[r] = y; y += rhei[r] + gap; }
        for (int i = 0; i < n; i++) {
            int r = ipr[i];
            if (r >= nr) continue;
            int w = 0;
            int span = kids[i]->gcs > 0 ? kids[i]->gcs : 1;
            for (int k = 0; k < span && ipc[i] + k < nc; k++) {
                w += cwid[ipc[i] + k];
                if (k) w += gap;
            }
            lay_box_dispatch(kids[i], cx[ipc[i]], ry[r], w, cbanc);
            int align = kids[i]->as ? kids[i]->as : b->ai;
            if (align == 0 || align == 3) {
                if (rhei[r] > kids[i]->h) {
                    kids[i]->h = rhei[r];
                    kids[i]->ch = rhei[r] - kids[i]->pt - kids[i]->pb - kids[i]->bt - kids[i]->bb;
                }
            } else if (align == 1 && rhei[r] > kids[i]->h) {
                kids[i]->y = ry[r] + (rhei[r] - kids[i]->h) / 2;
            } else if (align == 2 && rhei[r] > kids[i]->h) {
                kids[i]->y = ry[r] + rhei[r] - kids[i]->h;
            }
        }
        b->ch = y - gap - y0;
        if (b->ch < 0) b->ch = 0;
        b->h = b->ch + b->pt + b->pb + b->bt + b->bb;
        free(ry);
    }

    free(cwid);
    free(rhei);
    free(cx);
    free(ipc);
    free(ipr);
    free(cols.v);
    free(rows.v);
    free(kids);
}
