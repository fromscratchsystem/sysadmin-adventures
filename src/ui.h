#ifndef UI_H
#define UI_H

#include "defs.h"

/* ─── ncurses initialization + colors ──────────────────────── */
void init_ncurses(void);

/* ─── Panels ─────────────────────────────────────────────────── */
Panel make_panel(int lines, int cols, int y, int x,
                 int color_pair, const char *title);
void  destroy_panel(Panel *p);

/* ─── Complete layout ─────────────────────────────────────────── */
Layout create_layout(void);
void   destroy_layout(Layout *l);

/*
 * handle_resize — rebuilds layout after SIGWINCH.
 * Resizes all active SSH shells.
 */
Layout handle_resize(Layout *old, Shell *shells, int nshells);

/* ─── Tab bar ────────────────────────────────────────────── */
void draw_tabs(WINDOW *tab_bar, Shell *shells, int nshells,
               int active, int cols);

/* ─── Status bar ─────────────────────────────────────────────── */
void draw_status(WINDOW *status, int cols,
                 const char *date, float sla, int tickets);

/* ─── Input line ────────────────────────────────────────────── */
void draw_prompt(WINDOW *input_win);
void redraw_input(WINDOW *input_win, const char *buf, int pos);

/* ─── Display in panels ──────────────────────────────────────── */
void narrator_say    (Panel *p, const char *msg);
void narrator_printf (Panel *p, const char *fmt, ...); /* shortcut snprintf+say */
void narrator_scroll (Panel *p, int delta);   /* >0=scroll up, <0=scroll down */
void narrator_refresh(Panel *p);              /* re-render after resize */
void shell_print     (Panel *p, const char *line);

#endif /* UI_H */
