#ifndef DEFS_H
#define DEFS_H

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

/* ─── Dimensions layout ──────────────────────────────────────── */
#define STATUS_HEIGHT   1
#define NARRATOR_RATIO  0.35
#define INPUT_HEIGHT    1

/* ─── Historique ─────────────────────────────────────────────── */
#define HISTORY_MAX     64
#define CMD_MAX         256

/* ─── Multi-conteneurs ───────────────────────────────────────── */
#define MAX_SHELLS      8
#define MAX_NETS        8   /* réseaux additionnels par conteneur */

/* ─── Paires de couleurs ncurses ─────────────────────────────── */
#define COL_NARRATOR_BORDER 1
#define COL_NARRATOR_TEXT   2
#define COL_SHELL_BORDER    3
#define COL_SHELL_TEXT      4
#define COL_STATUS          5
#define COL_PROMPT          6
#define COL_INPUT           7

/* ─── Panel : une zone ncurses avec bordure + contenu ────────── */
typedef struct {
    WINDOW     *border;
    WINDOW     *inner;
    int         lines, cols, y, x;
    int         color_pair;
    const char *title;
} Panel;

/* ─── Layout : l'ensemble des fenêtres ───────────────────────── */
typedef struct {
    Panel   narrator;
    WINDOW *tab_bar;   /* barre d'onglets entre narrateur et shell */
    Panel   shell;
    WINDOW *status;
    WINDOW *input_win;
    int     term_lines;
    int     term_cols;
} Layout;

/* ─── Historique des commandes ───────────────────────────────── */
typedef struct {
    char entries[HISTORY_MAX][CMD_MAX];
    int  count;
    int  cursor;
} History;

/* ─── Shell : connexion SSH vers un conteneur Podman ─────────── */
typedef struct {
    int            sock;          /* socket TCP pour select()        */
    int            alive;
    int            has_activity;  /* données reçues hors focus       */
    int            port;          /* port hôte SSH (pour persistance) */
    char           name[32];      /* label de l'onglet               */
    char           extra_nets[MAX_NETS][32]; /* réseaux additionnels */
    int            nnets;
    struct VTerm  *vterm;         /* émulateur VT100                 */
    void          *ssh_session;   /* LIBSSH2_SESSION*, opaque ici    */
    void          *ssh_channel;   /* LIBSSH2_CHANNEL*, opaque ici    */
} Shell;

#endif /* DEFS_H */
