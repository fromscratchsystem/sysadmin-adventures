#ifndef NCURSES_H
#define NCURSES_H
/*
 * Stub minimaliste de <ncurses.h> pour les tests unitaires.
 * Fournit les types et les macros utilisés par vterm.c / defs.h
 * sans dépendre de la bibliothèque ncurses réelle.
 */
#include <stdarg.h>

/* Types ──────────────────────────────────────────────────────── */
typedef struct { int _dummy; } WINDOW;
typedef unsigned long attr_t;
typedef unsigned long chtype;

/* Attributs texte */
#define A_BOLD       (1UL << 0)
#define A_UNDERLINE  (1UL << 1)
#define A_REVERSE    (1UL << 2)

/* Couleurs ANSI */
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

/* Macros */
#define COLOR_PAIR(n) ((chtype)0)
#define KEY_F(n)      (0x108 + (n))

/* Fonctions stubées ──────────────────────────────────────────── */
static inline int init_pair(short p, short f, short b)
    { (void)p;(void)f;(void)b; return 0; }
static inline int wmove(WINDOW *w, int y, int x)
    { (void)w;(void)y;(void)x; return 0; }
static inline int waddch(WINDOW *w, chtype c)
    { (void)w;(void)c; return 0; }
static inline int wrefresh(WINDOW *w)
    { (void)w; return 0; }
static inline int werase(WINDOW *w)
    { (void)w; return 0; }
static inline int wattron(WINDOW *w, int a)
    { (void)w;(void)a; return 0; }
static inline int wattroff(WINDOW *w, int a)
    { (void)w;(void)a; return 0; }
static inline int mvwprintw(WINDOW *w, int y, int x, const char *fmt, ...) {
    va_list ap;
    (void)w;(void)y;(void)x;(void)fmt;
    va_start(ap, fmt); va_end(ap);
    return 0;
}

#endif /* NCURSES_H */
