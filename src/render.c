#include "peek.h"
#include "box.h"

static Box *ROOT;

Node *FOC;
Node **BTNS;
int NBTN, FOCI;

Box *peek_root(void) { return ROOT; }

void peek_layout(void) {
    box_free(ROOT);
    ROOT = box_build(DOM);
    if (ROOT) lay_layout(ROOT);
}

void peek_paint(void) {
    Canvas *cv = cv_new(vx_cols(), vx_rows());
    cv_clear(cv, -1);
    TermCtx tc;
    tc.cv = cv;
    tc.rev = 0;
    if (ROOT) paint_tree(ROOT, term_backend(), &tc);
    term_frame(cv);
    cv_free(cv);
}

void peek_dump(void) {
    peek_layout();
    box_dump(ROOT);
}

void render(Node *n) {
    if (n) DOM = n;
    peek_layout();
    peek_paint();
}
