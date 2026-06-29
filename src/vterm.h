#ifndef VTERM_H
#define VTERM_H

#include <ncurses.h>

/* ─── Une cellule du buffer d'écran ──────────────────────────────────────── */
typedef struct {
    int    ch;          /* caractère (unsigned char stocké en int) */
    short  color_pair;  /* paire ncurses */
    attr_t attrs;       /* A_BOLD, A_UNDERLINE, A_REVERSE… */
} VCell;

/* ─── État de l'émulateur ────────────────────────────────────────────────── */
#define VTERM_NCOLORS 9  /* 0=défaut, 1-8 = ANSI black…white */

typedef struct VTerm {
    VCell *screen;       /* buffer principal */
    VCell *altscreen;    /* buffer alternatif (less, vim, htop…) */
    VCell *cur;          /* pointe vers screen ou altscreen */
    int    rows, cols;

    int    crow, ccol;             /* position curseur */
    int    saved_crow, saved_ccol; /* sauvegarde ESC 7 */

    int    scroll_top, scroll_bottom;  /* région de scroll (inclusif) */

    short  cur_fg, cur_bg; /* couleur courante (0=défaut, 1-8=ANSI) */
    short  cur_pair;       /* paire ncurses dérivée */
    attr_t cur_attrs;      /* attributs courants */
    int    wrap_pending;   /* wrap différé après la dernière colonne */

    int    in_altscreen;   /* 1 si altscreen actif */

    /* Machine à états du parseur */
    int    state;
    int    params[32];
    int    nparams;
    int    private_flag;

} VTerm;

/*
 * vterm_new  — alloue et initialise un VTerm.
 * first_pair : premier numéro de paire ncurses libre (les paires 1..first_pair-1
 *              sont déjà utilisées par l'UI du jeu).
 */
VTerm *vterm_new    (int rows, int cols, int first_pair);
void   vterm_free   (VTerm *vt);
void   vterm_resize (VTerm *vt, int rows, int cols);
void   vterm_process(VTerm *vt, const char *buf, int n);
void   vterm_render (VTerm *vt, WINDOW *win);

#endif /* VTERM_H */
