#include "peek.h"
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

static struct termios SAVED;

static void raw_on(void) {
    struct termios t;
    if (tcgetattr(0, &SAVED)) return;
    t = SAVED;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

static void raw_off(void) { tcsetattr(0, TCSANOW, &SAVED); }

static int getkey(int tmo) {
    unsigned char c;
    if (tmo >= 0) {
        fd_set rs;
        struct timeval tv;
        tv.tv_sec = tmo / 1000;
        tv.tv_usec = (tmo % 1000) * 1000;
        FD_ZERO(&rs);
        FD_SET(0, &rs);
        if (select(1, &rs, 0, 0, &tv) <= 0) return -2;
    }
    if (read(0, &c, 1) != 1) return 'q';
    if (c != 27) return c;
    unsigned char b[2];
    if (read(0, b, 1) != 1) return 'q';
    if (read(0, b + 1, 1) != 1) return 'q';
    if (b[0] == '[' && b[1] == 'A') return 1;
    if (b[0] == '[' && b[1] == 'B') return 2;
    return 0;
}

static void frame(void) {
    paint_set_focus(FOC);
    peek_layout();
    peek_paint();
    if (NLOG) putchar('\n');
    for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
}

static int ASEEN;

static void show_alerts(void) {
    for (; ASEEN < NAL; ASEEN++) {
        putchar('\n');
        draw_dialog(ALERTS[ASEEN]);
        fputs("\x1b[90mpress any key to dismiss\x1b[0m\n", stdout);
        fflush(stdout);
        unsigned char c;
        if (read(0, &c, 1) != 1) return;
    }
}

static void hint(void) {
    if (NBTN)
        fputs("\x1b[90m[\xe2\x86\x91\xe2\x86\x93/Tab] \xe9\x80\x89\xe6\x8b\xa9  [Enter] \xe6\x89\x93\xe5\xbc\x80/\xe7\x82\xb9\xe5\x87\xbb  [b] \xe5\x90\x8e\xe9\x80\x80  [q] \xe9\x80\x80\xe5\x87\xba\x1b[0m\n", stdout);
    else
        fputs("\x1b[90m[q] \xe9\x80\x80\xe5\x87\xba\x1b[0m\n", stdout);
    fflush(stdout);
}

static char *CURURL;
static char *HIST[32];
static int NH, JSUP;

static char *rf(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); return 0; }
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    if (fl < 0) { fclose(f); perror(p); return 0; }
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)fl + 9);
    if (!b) { fclose(f); oom(); }
    size_t got = fread(b, 1, (size_t)fl, f);
    if (ferror(f)) { fclose(f); free(b); perror(p); return 0; }
    fclose(f);
    b[got] = 0;
    *n = got;
    return b;
}

static void base_local(const char *p) {
    const char *s = strrchr(p, '/');
    if (!s) { js_set_base("."); return; }
    if (s == p) { js_set_base("/"); return; }
    char *d = sdup(p, (size_t)(s - p));
    js_set_base(d);
    free(d);
}

static int load_page(const char *u) {
    char ub[2048];
    snprintf(ub, sizeof ub, "%s", u);
    int url = url_is(ub);
    size_t len = 0;
    char *body = url ? http_get(ub, &len) : rf(ub, &len);
    if (!body) return 0;
    free(BTNS);
    BTNS = 0;
    NBTN = FOCI = 0;
    FOC = 0;
    for (int i = 0; i < NLOG; i++) free(LOGS[i]);
    for (int i = 0; i < NAL; i++) free(ALERTS[i]);
    NLOG = NAL = 0;
    ASEEN = 0;
    if (url) js_set_base(ub);
    else base_local(ub);
    if (JSUP) { js_done(); js_init(); }
    else { js_init(); JSUP = 1; }
    css_reset();
    DOM = parse_html(body);
    run_scripts(DOM);
    js_pump();
    parse_css(css_collect(DOM, ub));
    apply_styles(DOM);
    qquery("button,a[href]", sizeof "button,a[href]" - 1);
    NBTN = NQL;
    if (NQL) {
        BTNS = malloc((size_t)NQL * sizeof *BTNS);
        if (!BTNS) oom();
        memcpy(BTNS, QL, (size_t)NQL * sizeof *BTNS);
        FOC = BTNS[0];
    }
    js_win_event("domcontentloaded");
    js_win_event("load");
    free(CURURL);
    CURURL = sdup(ub, strlen(ub));
    return 1;
}

static void nav(const char *href) {
    if (!*href || *href == '#') return;
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

static void back(void) {
    if (!NH) return;
    char *u = HIST[--NH];
    load_page(u);
    free(u);
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: %s [--dump] <file.html|url>\n", argv[0]), 1;
    vx_init();
    js_viewport(vx_width(), vx_height());
    int dump = 0, runms = 0;
    const char *arg = argv[1];
    if (!strcmp(arg, "--dump")) {
        dump = 1;
        if (argc < 3) return fprintf(stderr, "usage: %s --dump <file.html>\n", argv[0]), 1;
        arg = argv[2];
    } else if (!strcmp(arg, "--run")) {
        runms = argc > 3 ? atoi(argv[2]) : 0;
        if (runms <= 0 || argc < 4)
            return fprintf(stderr, "usage: %s --run <ms> <file.html|url>\n", argv[0]), 1;
        arg = argv[3];
    }
    if (!load_page(arg)) return 1;
    if (dump) {
        peek_dump();
        js_done();
        return 0;
    }
    if (runms) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (;;) {
            js_pump();
            double w = js_next_wait();
            if (w < 0) break;
            if (w > 50) w = 50;
            usleep((useconds_t)(w * 1000) + 1000);
            clock_gettime(CLOCK_MONOTONIC, &b);
            if ((b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000 > runms) break;
        }
        for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
        js_done();
        return 0;
    }
    if (!isatty(0) || !isatty(1)) {
        FOC = 0;
        js_pump();
        apply_styles(DOM);
        paint_set_focus(0);
        peek_layout();
        peek_paint();
        if (NLOG) putchar('\n');
        for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
        if (NAL) putchar('\n');
        for (int i = 0; i < NAL; i++) draw_dialog(ALERTS[i]);
        js_done();
        return 0;
    }
    raw_on();
    fputs("\x1b[?25l", stdout);
    frame();
    show_alerts();
    hint();
    for (;;) {
        double w = js_next_wait();
        int tmo = w < 0 ? -1 : w < 1 ? 1 : (int)(w * 1000.0 + 0.5);
        int acted = 0;
        int k = getkey(tmo);
        if (k == 'q' || k == 3) break;
        if (k == -2) {
            acted = 1;
        } else if (k == 1 || k == 2 || k == '\t') {
            if (NBTN) {
                FOCI = k == 1 ? (FOCI + NBTN - 1) % NBTN : (FOCI + 1) % NBTN;
                FOC = BTNS[FOCI];
            }
        } else if (k == '\r' || k == '\n' || k == ' ') {
            if (NBTN) {
                Node *f = BTNS[FOCI];
                char *href = f->taglen == 1 && f->tagpk == K_A ? attr_get(f, "href") : 0;
                if (href) { nav(href); acted = 1; }
                else { js_click(oc_find(BTNS[FOCI])); acted = 1; }
            }
        } else if (k == 'b' || k == 127 || k == 8) {
            if (NH) { back(); acted = 1; }
        }
        if (js_pump()) acted = 1;
        if (acted) apply_styles(DOM);
        frame();
        show_alerts();
        hint();
    }
    js_done();
    raw_off();
    fputs("\x1b[?25h", stdout);
    putchar('\n');
    return 0;
}
