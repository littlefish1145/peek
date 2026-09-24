#include "peek.h"

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) {
        perror(p);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    if (fl < 0) {
        fclose(f);
        perror(p);
        return 0;
    }
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)fl + 9);
    if (!b) oom();
    size_t got = fread(b, 1, (size_t)fl, f);
    if (ferror(f)) {
        fclose(f);
        free(b);
        perror(p);
        return 0;
    }
    fclose(f);
    b[got] = 0;
    *n = got;
    return b;
}

static void pstr(const char *s, int n) {
    putchar('"');
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c == '\n') fputs("\\n", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c < 32) printf("\\x%02x", c);
        else putchar(c);
    }
    putchar('"');
}

static void indent(int d) {
    for (int i = 0; i < d; i++) fputs("  ", stdout);
}

static void line_sel(Node *n, int d) {
    int nr;
    const Rule *rr = css_rules(&nr);
    char line[4096];
    line[0] = 0;
    int w = 0;
    int any = 0;
    for (int i = 0; i < nr; i++) {
        if (rr[i].pe) continue;
        if (match_selector(rr + i, n) < 0) continue;
        char sel[512];
        rule_sel(rr + i, sel, sizeof sel);
        int r = snprintf(line + w, sizeof line > (size_t)w ? sizeof line - (size_t)w : 0,
                         "%s%s", any ? " | " : "", sel);
        if (r > 0) w += r;
        any = 1;
    }
    if (!any) return;
    indent(d);
    printf("@ %s\n", line);
}

static void line_css(Node *n, int d) {
    if (!n->nst) return;
    indent(d);
    fputs("= ", stdout);
    for (int i = 0; i < n->nst; i++)
        printf("%s%s=%s#%d", i ? " " : "", n->st[i].p->name,
               n->st[i].v ? n->st[i].v : "", n->st[i].spec);
    putchar('\n');
}

static void walk(Node *n, int d) {
    if (n->def->f & T_TEXTN) {
        indent(d);
        printf("%s ", n->gen ? "+" : "-");
        pstr(n->text, n->tlen);
        putchar('\n');
        return;
    }
    if (n->tag[0] == '#') {
        for (int i = 0; i < n->nchild; i++) walk(n->child[i], d);
        return;
    }
    indent(d);
    printf("<%s", n->tag);
    for (int i = 0; i < n->nattr; i++) {
        if (!strcmp(n->attrs[i].k, "style")) continue;
        putchar(' ');
        fputs(n->attrs[i].k, stdout);
        putchar('=');
        pstr(n->attrs[i].v, (int)strlen(n->attrs[i].v));
    }
    if (n->gen) printf(" :gen");
    printf(">\n");
    line_sel(n, d + 1);
    line_css(n, d + 1);
    if ((n->taglen == 5 && n->tagpk == K5('s', 't', 'y', 'l', 'e')) ||
        (n->taglen == 6 && n->tagpk == K6('s', 'c', 'r', 'i', 'p', 't')))
        return;
    for (int i = 0; i < n->nchild; i++) walk(n->child[i], d + 1);
}

int main(int argc, char **argv) {
    const char *arg = 0;
    int vw = 1280, vh = 800;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--w") && i + 1 < argc) vw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) vh = atoi(argv[++i]);
        else arg = argv[i];
    }
    if (!arg) return fprintf(stderr, "usage: %s [--w px] [--h px] <file.html|url>\n", argv[0]), 1;
    size_t len = 0;
    char *body = url_is(arg) ? http_get((char *)arg, &len) : slurp(arg, &len);
    if (!body) return 1;
    css_viewport(vw, vh);
    Node *dom = parse_html(body);
    parse_css(css_collect(dom, arg));
    apply_styles(dom);
    walk(dom, 0);
    return 0;
}
