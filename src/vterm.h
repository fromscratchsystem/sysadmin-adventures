#ifndef VTERM_H
#define VTERM_H

#include <ncurses.h>

/* ─── A screen buffer cell ──────────────────────────────────────── */
typedef struct {
    int    ch;          /* character (unsigned char stored in int) */
    short  color_pair;  /* ncurses color pair */
    attr_t attrs;       /* A_BOLD, A_UNDERLINE, A_REVERSE… */
} VCell;

/* ─── Emulator state ────────────────────────────────────────────────── */
#define VTERM_NCOLORS    9    /* 0=default, 1-8 = ANSI black…white */
#define SCROLLBACK_LINES 500  /* lines preserved above screen */

typedef struct VTerm {
    VCell *screen;       /* main buffer */
    VCell *altscreen;    /* alternate buffer (less, vim, htop…) */
    VCell *cur;          /* points to screen or altscreen */
    int    rows, cols;

    int    crow, ccol;             /* cursor position */
    int    saved_crow, saved_ccol; /* ESC 7 save */

    int    scroll_top, scroll_bottom;  /* scroll region (inclusive) */

    short  cur_fg, cur_bg; /* current color (0=default, 1-8=ANSI) */
    short  cur_pair;       /* derived ncurses color pair */
    attr_t cur_attrs;      /* current attributes */
    int    wrap_pending;   /* deferred wrap after last column */

    int    in_altscreen;   /* 1 if altscreen active */

    /* Scrollback buffer (circular, main screen only) */
    VCell *scrollback;     /* sb_capacity * cols cells */
    int    sb_capacity;    /* = SCROLLBACK_LINES */
    int    sb_count;       /* lines actually stored */
    int    sb_head;        /* next write index */
    int    sb_offset;      /* 0=current view, N=scrolled up N lines */

    /* Parser state machine */
    int    state;
    int    params[32];
    int    nparams;
    int    private_flag;

} VTerm;

/*
 * vterm_new  — allocates and initializes a VTerm.
 * first_pair : first free ncurses color pair number (pairs 1..first_pair-1
 *              are already used by game UI).
 */
VTerm *vterm_new    (int rows, int cols, int first_pair);
void   vterm_free   (VTerm *vt);
void   vterm_resize (VTerm *vt, int rows, int cols);
void   vterm_process(VTerm *vt, const char *buf, int n);
void   vterm_render (VTerm *vt, WINDOW *win);
void   vterm_scroll (VTerm *vt, int delta); /* >0 = scroll up, <0 = scroll down */

#endif /* VTERM_H */
