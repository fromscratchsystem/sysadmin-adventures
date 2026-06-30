#ifndef UI_H
#define UI_H

#include "defs.h"

/* ─── Initialisation ncurses + couleurs ──────────────────────── */
void init_ncurses(void);

/* ─── Panels ─────────────────────────────────────────────────── */
Panel make_panel(int lines, int cols, int y, int x,
                 int color_pair, const char *title);
void  destroy_panel(Panel *p);

/* ─── Layout complet ─────────────────────────────────────────── */
Layout create_layout(void);
void   destroy_layout(Layout *l);

/*
 * handle_resize — reconstruit le layout après un SIGWINCH.
 * Redimensionne tous les shells SSH actifs.
 */
Layout handle_resize(Layout *old, Shell *shells, int nshells);

/* ─── Barre d'onglets ────────────────────────────────────────── */
void draw_tabs(WINDOW *tab_bar, Shell *shells, int nshells,
               int active, int cols);

/* ─── Status bar ─────────────────────────────────────────────── */
void draw_status(WINDOW *status, int cols,
                 const char *date, float sla, int tickets);

/* ─── Ligne de saisie ────────────────────────────────────────── */
void draw_prompt(WINDOW *input_win);
void redraw_input(WINDOW *input_win, const char *buf, int pos);

/* ─── Affichage dans les panels ──────────────────────────────── */
void narrator_say    (Panel *p, const char *msg);
void narrator_printf (Panel *p, const char *fmt, ...); /* raccourci snprintf+say */
void narrator_scroll (Panel *p, int delta);   /* >0=remonter, <0=descendre */
void narrator_refresh(Panel *p);              /* re-rend après resize */
void shell_print     (Panel *p, const char *line);

#endif /* UI_H */
