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

/* ─── Layout dimensions ──────────────────────────────────────── */
#define STATUS_HEIGHT   1
#define NARRATOR_RATIO  0.35
#define INPUT_HEIGHT    1

/* ─── History ─────────────────────────────────────────────── */
#define HISTORY_MAX     64
#define CMD_MAX         256

/* ─── Multi-container ───────────────────────────────────────── */
#define MAX_SHELLS     16   /* main container + infra servers + deploys */
#define MAX_NETS        8   /* additional networks per container */

/* ─── ncurses color pairs ─────────────────────────────────── */
#define COL_NARRATOR_BORDER 1
#define COL_NARRATOR_TEXT   2
#define COL_SHELL_BORDER    3
#define COL_SHELL_TEXT      4
#define COL_STATUS          5
#define COL_PROMPT          6
#define COL_INPUT           7

/* ─── Panel: ncurses area with border + content ────────── */
typedef struct {
    WINDOW     *border;
    WINDOW     *inner;
    int         lines, cols, y, x;
    int         color_pair;
    const char *title;
} Panel;

/* ─── Layout: complete set of windows ───────────────────────── */
typedef struct {
    Panel   narrator;
    WINDOW *tab_bar;   /* tab bar between narrator and shell */
    Panel   shell;
    WINDOW *status;
    WINDOW *input_win;
    int     term_lines;
    int     term_cols;
} Layout;

/* ─── Command history ───────────────────────────────────── */
typedef struct {
    char entries[HISTORY_MAX][CMD_MAX];
    int  count;
    int  cursor;
} History;

/* ─── Shell: SSH connection to Podman container ─────────── */
typedef struct {
    int            sock;          /* TCP socket for select()        */
    int            alive;
    int            has_activity;  /* received data while unfocused  */
    int            port;          /* SSH host port (for persistence) */
    char           name[32];           /* tab label               */
    char           container_name[32]; /* real Podman name ("" = main) */
    char           extra_nets[MAX_NETS][32]; /* additional networks */
    int            nnets;
    struct VTerm  *vterm;         /* VT100 emulator                 */
    void          *ssh_session;   /* LIBSSH2_SESSION*, opaque here  */
    void          *ssh_channel;   /* LIBSSH2_CHANNEL*, opaque here  */
} Shell;

#endif /* DEFS_H */
