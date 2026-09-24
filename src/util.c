#include "peek.h"

void oom(void) { fputs("peek: out of memory\n", stderr); exit(1); }

uint64_t pk(const char *s) {
    uint64_t k = 0;
    for (int i = 0; i < 8 && s[i]; i++)
        k |= (uint64_t)(unsigned char)s[i] << (i * 8);
    return k;
}

char *cut(char *s, char *e) {
    *e = 0;
    while (e > s && ISWS(e[-1])) *--e = 0;
    while (ISWS(*s)) s++;
    return s;
}

int utf8_put(char *w, unsigned cp) {
    if (cp < 0x80) { *w = (char)cp; return 1; }
    if (cp < 0x800) {
        w[0] = (char)(0xC0 | (cp >> 6));
        w[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        w[0] = (char)(0xE0 | (cp >> 12));
        w[1] = (char)(0x80 | (cp >> 6 & 63));
        w[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    w[0] = (char)(0xF0 | (cp >> 18));
    w[1] = (char)(0x80 | (cp >> 12 & 63));
    w[2] = (char)(0x80 | (cp >> 6 & 63));
    w[3] = (char)(0x80 | (cp & 63));
    return 4;
}

int unescape(char *s, int len) {
    char *w = s, *r = s, *e = s + len;
    while (r < e) {
        if (*r != '&' || e - r < 3) { *w++ = *r++; continue; }
        char *sc = memchr(r + 1, ';', e - r - 1);
        if (!sc || sc - r > 10) { *w++ = *r++; continue; }
        char buf[16] = {0};
        memcpy(buf, r + 1, sc - r - 1);
        unsigned cp = 0;
        int ok = 0;
        if (*buf == '#') {
            char *d = buf + 1;
            if (*d == 'x' || *d == 'X') cp = (unsigned)strtoul(d + 1, 0, 16);
            else cp = (unsigned)strtoul(d, 0, 10);
            ok = cp != 0 && cp <= 0x10FFFF;
        } else {
            switch (pk(buf)) {
                case K3('a', 'm', 'p'): cp = '&'; ok = 1; break;
                case K2('l', 't'): cp = '<'; ok = 1; break;
                case K2('g', 't'): cp = '>'; ok = 1; break;
                case K4('q', 'u', 'o', 't'): cp = '"'; ok = 1; break;
                case K4('a', 'p', 'o', 's'): cp = '\''; ok = 1; break;
                case K4('n', 'b', 's', 'p'): cp = ' '; ok = 1; break;
                case K4('c', 'o', 'p', 'y'): cp = 0xA9; ok = 1; break;
                case K3('r', 'e', 'g'): cp = 0xAE; ok = 1; break;
                case K3('d', 'e', 'g'): cp = 0xB0; ok = 1; break;
                case K6('p', 'l', 'u', 's', 'm', 'n'): cp = 0xB1; ok = 1; break;
                case K5('t', 'i', 'm', 'e', 's'): cp = 0xD7; ok = 1; break;
                case K6('d', 'i', 'v', 'i', 'd', 'e'): cp = 0xF7; ok = 1; break;
                case K6('m', 'i', 'd', 'd', 'o', 't'): cp = 0xB7; ok = 1; break;
                case K4('s', 'e', 'c', 't'): cp = 0xA7; ok = 1; break;
                case K4('p', 'a', 'r', 'a'): cp = 0xB6; ok = 1; break;
                case K5('l', 'a', 'q', 'u', 'o'): cp = 0xAB; ok = 1; break;
                case K5('r', 'a', 'q', 'u', 'o'): cp = 0xBB; ok = 1; break;
                case K4('c', 'e', 'n', 't'): cp = 0xA2; ok = 1; break;
                case K5('p', 'o', 'u', 'n', 'd'): cp = 0xA3; ok = 1; break;
                case K3('y', 'e', 'n'): cp = 0xA5; ok = 1; break;
                case K4('e', 'u', 'r', 'o'): cp = 0x20AC; ok = 1; break;
                case K5('m', 'd', 'a', 's', 'h'): cp = 0x2014; ok = 1; break;
                case K5('n', 'd', 'a', 's', 'h'): cp = 0x2013; ok = 1; break;
                case K6('h', 'e', 'l', 'l', 'i', 'p'): cp = 0x2026; ok = 1; break;
                case K5('l', 's', 'q', 'u', 'o'): cp = 0x2018; ok = 1; break;
                case K5('r', 's', 'q', 'u', 'o'): cp = 0x2019; ok = 1; break;
                case K5('l', 'd', 'q', 'u', 'o'): cp = 0x201C; ok = 1; break;
                case K5('r', 'd', 'q', 'u', 'o'): cp = 0x201D; ok = 1; break;
                case K4('b', 'u', 'l', 'l'): cp = 0x2022; ok = 1; break;
                case K6('d', 'a', 'g', 'g', 'e', 'r'): cp = 0x2020; ok = 1; break;
                case K5('t', 'r', 'a', 'd', 'e'): cp = 0x2122; ok = 1; break;
                case K4('l', 'a', 'r', 'r'): cp = 0x2190; ok = 1; break;
                case K4('u', 'a', 'r', 'r'): cp = 0x2191; ok = 1; break;
                case K4('r', 'a', 'r', 'r'): cp = 0x2192; ok = 1; break;
                case K4('d', 'a', 'r', 'r'): cp = 0x2193; ok = 1; break;
            }
        }
        if (!ok) { *w++ = *r++; continue; }
        w += utf8_put(w, cp);
        r = sc + 1;
    }
    return (int)(w - s);
}

int rgb256(int r, int g, int b) {
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    if (r == g && g == b) return r < 8 ? 16 : r > 248 ? 231 : 232 + ((r - 8) * 205 >> 11);
    return 16 + 36 * ((r * 1287 + 32896) >> 16)
         + 6 * ((g * 1287 + 32896) >> 16) + ((b * 1287 + 32896) >> 16);
}

void hsl_rgb(int h, int s, int l, int *R, int *G, int *B) {
    h = (h % 360 + 360) % 360;
    s = s < 0 ? 0 : s > 100 ? 100 : s;
    l = l < 0 ? 0 : l > 100 ? 100 : l;
    int d = 2 * l - 100;
    if (d < 0) d = -d;
    int c = (100 - d) * s / 100, m2 = 2 * l - c;
    int t = h % 120;
    t = t < 60 ? 60 - t : t - 60;
    int x = c * (60 - t) / 60, r2, g2, b2;
    switch (h / 60) {
        case 0: r2 = 2 * c; g2 = 2 * x; b2 = 0; break;
        case 1: r2 = 2 * x; g2 = 2 * c; b2 = 0; break;
        case 2: r2 = 0; g2 = 2 * c; b2 = 2 * x; break;
        case 3: r2 = 0; g2 = 2 * x; b2 = 2 * c; break;
        case 4: r2 = 2 * x; g2 = 0; b2 = 2 * c; break;
        default: r2 = 2 * c; g2 = 0; b2 = 2 * x; break;
    }
    *R = ((r2 + m2) * 51 + 20) / 40;
    *G = ((g2 + m2) * 51 + 20) / 40;
    *B = ((b2 + m2) * 51 + 20) / 40;
}

static char *p3(char *s, int *p) {
    for (int i = 0; i < 3; i++) {
        while (*s && *s != '-' && (*s < '0' || *s > '9')) s++;
        if (!*s) return s;
        p[i] = (int)strtol(s, &s, 10);
    }
    return s;
}

int ansi_color(const char *v) {
    while (*v == ' ') v++;
    if (isdigit((unsigned char)*v)) { int n = atoi(v); return n < 256 ? n : -1; }
    if (!memcmp(v, "rgb", 3) || !memcmp(v, "hsl", 3)) {
        int p[3], r, g, b;
        float a = 1;
        char *lp = (char *)memchr(v, '(', strlen(v));
        if (!lp) return -1;
        char *e3 = p3(lp + 1, p);
        if (e3 == lp + 1) return -1;
        char *sl = strchr(e3, '/');
        if (sl) a = atof(sl + 1);
        else if (v[3] == 'a') {
            char *cm = lp;
            for (int i = 0; i < 3 && cm; i++) cm = strchr(cm + 1, ',');
            if (cm) a = atof(cm + 1);
        }
        if (a < 0) a = 0;
        if (a > 1) a = 1;
        if (*v == 'h') hsl_rgb(p[0], p[1], p[2], &r, &g, &b);
        else { r = p[0]; g = p[1]; b = p[2]; }
        return rgb256(r * a + .5f, g * a + .5f, b * a + .5f);
    }
    if (*v == '#') {
        size_t len = strlen(v);
        if (len != 4 && len != 7) return -1;
        unsigned long hx = strtoul(v + 1, 0, 16);
        unsigned r, g, b;
        if (len == 4) {
            r = hx >> 8 & 0xf; g = hx >> 4 & 0xf; b = hx & 0xf;
            r |= r << 4; g |= g << 4; b |= b << 4;
        } else {
            r = hx >> 16 & 0xff; g = hx >> 8 & 0xff; b = hx & 0xff;
        }
        return rgb256(r, g, b);
    }
    switch (pk(v)) {
        case K5('b', 'l', 'a', 'c', 'k'): return 0;
        case K3('r', 'e', 'd'): return 1;
        case K5('g', 'r', 'e', 'e', 'n'): return 2;
        case K6('y', 'e', 'l', 'l', 'o', 'w'): return 3;
        case K4('b', 'l', 'u', 'e'): return 4;
        case K7('m', 'a', 'g', 'e', 'n', 't', 'a'): return 5;
        case K4('c', 'y', 'a', 'n'): case K4('a', 'q', 'u', 'a'): return 6;
        case K5('w', 'h', 'i', 't', 'e'): return 7;
        case K4('l', 'i', 'm', 'e'): return 10;
        case K4('n', 'a', 'v', 'y'): return 18;
        case K4('t', 'e', 'a', 'l'): return 30;
        case K6('m', 'a', 'r', 'o', 'o', 'n'): return 88;
        case K6('p', 'u', 'r', 'p', 'l', 'e'): return 90;
        case K5('o', 'l', 'i', 'v', 'e'): return 100;
        case K7('f', 'u', 'c', 'h', 's', 'i', 'a'): return 201;
        case K4('g', 'r', 'a', 'y'): return 244;
        case K6('s', 'i', 'l', 'v', 'e', 'r'): return 250;
    }
    return -1;
}

char *sdup(const char *s, size_t n) {
    char *p = malloc(n + 1 < 16 ? 16 : n + 1);
    if (!p) oom();
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}
