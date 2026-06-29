#include "ui.h"
#include "shell.h"
#include <locale.h>

/* ═══════════════════════════════════════════════════════════════
 * INITIALISATION
 * ═══════════════════════════════════════════════════════════════ */

void init_ncurses(void) {
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Ce terminal ne supporte pas les couleurs.\n");
        exit(1);
    }
    start_color();
    use_default_colors();

    init_pair(COL_NARRATOR_BORDER, COLOR_GREEN, -1);
    init_pair(COL_NARRATOR_TEXT,   COLOR_GREEN, -1);
    init_pair(COL_SHELL_BORDER,    COLOR_CYAN,  -1);
    init_pair(COL_SHELL_TEXT,      COLOR_WHITE, -1);
    init_pair(COL_STATUS,          COLOR_BLACK,  COLOR_GREEN);
    init_pair(COL_PROMPT,          COLOR_CYAN,  -1);
    init_pair(COL_INPUT,           COLOR_WHITE, -1);
}

/* ═══════════════════════════════════════════════════════════════
 * PANELS
 * ═══════════════════════════════════════════════════════════════ */

Panel make_panel(int lines, int cols, int y, int x,
                 int color_pair, const char *title) {
    Panel p;
    p.lines      = lines;
    p.cols       = cols;
    p.y          = y;
    p.x          = x;
    p.color_pair = color_pair;
    p.title      = title;

    p.border = newwin(lines, cols, y, x);
    wattron(p.border, COLOR_PAIR(color_pair));
    box(p.border, 0, 0);
    if (title) {
        wattron(p.border, A_BOLD);
        mvwprintw(p.border, 0, 2, " %s ", title);
        wattroff(p.border, A_BOLD);
    }
    wattroff(p.border, COLOR_PAIR(color_pair));
    wrefresh(p.border);

    p.inner = newwin(lines - 2, cols - 2, y + 1, x + 1);
    scrollok(p.inner, TRUE);
    idlok(p.inner, TRUE);
    return p;
}

void destroy_panel(Panel *p) {
    delwin(p->inner);
    delwin(p->border);
}

/* ═══════════════════════════════════════════════════════════════
 * LAYOUT
 * ═══════════════════════════════════════════════════════════════ */

Layout create_layout(void) {
    Layout l;
    getmaxyx(stdscr, l.term_lines, l.term_cols);

    int narrator_h = (int)(l.term_lines * NARRATOR_RATIO);
    if (narrator_h < 6) narrator_h = 6;

    /* -1 pour la barre d'onglets */
    int shell_h = l.term_lines - narrator_h - STATUS_HEIGHT - INPUT_HEIGHT - 2;
    if (shell_h < 6) shell_h = 6;

    int shell_y  = narrator_h + 1;   /* +1 pour la tab_bar */
    int input_y  = shell_y + shell_h;
    int status_y = input_y + INPUT_HEIGHT;

    l.narrator = make_panel(narrator_h, l.term_cols, 0, 0,
                            COL_NARRATOR_BORDER, "NARRATEUR");
    wattron(l.narrator.inner, COLOR_PAIR(COL_NARRATOR_TEXT));

    /* Barre d'onglets : 1 ligne entre narrateur et terminal */
    l.tab_bar = newwin(1, l.term_cols, narrator_h, 0);
    wbkgd(l.tab_bar, COLOR_PAIR(COL_SHELL_BORDER));

    l.shell = make_panel(shell_h, l.term_cols, shell_y, 0,
                         COL_SHELL_BORDER, "TERMINAL");
    /* Le VTerm gère son propre rendu : ncurses ne doit pas scroller */
    scrollok(l.shell.inner, FALSE);

    l.input_win = newwin(INPUT_HEIGHT, l.term_cols, input_y, 0);
    keypad(l.input_win, TRUE);

    l.status = newwin(STATUS_HEIGHT, l.term_cols, status_y, 0);
    wbkgd(l.status, COLOR_PAIR(COL_STATUS));

    return l;
}

void destroy_layout(Layout *l) {
    destroy_panel(&l->narrator);
    delwin(l->tab_bar);
    destroy_panel(&l->shell);
    delwin(l->input_win);
    delwin(l->status);
}

Layout handle_resize(Layout *old, Shell *shells, int nshells) {
    destroy_layout(old);
    endwin();
    refresh();
    clear();

    Layout l = create_layout();

    for (int i = 0; i < nshells; i++) {
        if (shells[i].alive)
            shell_resize(&shells[i], l.shell.lines - 2, l.term_cols - 2);
    }
    return l;
}

/* ═══════════════════════════════════════════════════════════════
 * STATUS BAR
 * ═══════════════════════════════════════════════════════════════ */

void draw_status(WINDOW *status, int cols,
                 const char *date, float sla, int tickets) {
    werase(status);
    wbkgd(status, COLOR_PAIR(COL_STATUS));
    wattron(status, COLOR_PAIR(COL_STATUS) | A_BOLD);

    mvwprintw(status, 0, 1, " %s", date);

    char sla_str[32];
    snprintf(sla_str, sizeof(sla_str), "SLA: %.1f%%", sla);
    int sla_x = cols / 2 - (int)strlen(sla_str) / 2;
    mvwprintw(status, 0, sla_x, "%s", sla_str);

    char tick_str[32];
    snprintf(tick_str, sizeof(tick_str), "Tickets: %d ", tickets);
    mvwprintw(status, 0, cols - (int)strlen(tick_str) - 1, "%s", tick_str);

    wattroff(status, COLOR_PAIR(COL_STATUS) | A_BOLD);
    wrefresh(status);
}

/* ═══════════════════════════════════════════════════════════════
 * LIGNE DE SAISIE
 * ═══════════════════════════════════════════════════════════════ */

void draw_prompt(WINDOW *input_win) {
    werase(input_win);
    wattron(input_win, COLOR_PAIR(COL_PROMPT) | A_BOLD);
    mvwprintw(input_win, 0, 0, "$ ");
    wattroff(input_win, COLOR_PAIR(COL_PROMPT) | A_BOLD);
    wattron(input_win, COLOR_PAIR(COL_INPUT));
    wrefresh(input_win);
}

void redraw_input(WINDOW *input_win, const char *buf, int pos) {
    draw_prompt(input_win);
    wattron(input_win, COLOR_PAIR(COL_INPUT));
    mvwprintw(input_win, 0, 2, "%-*s", getmaxx(input_win) - 2, buf);
    wmove(input_win, 0, 2 + pos);
    wattroff(input_win, COLOR_PAIR(COL_INPUT));
    wrefresh(input_win);
}

/* ═══════════════════════════════════════════════════════════════
 * ÉCRITURE DANS LES PANELS
 * ═══════════════════════════════════════════════════════════════ */

void narrator_say(Panel *p, const char *msg) {
    wattron(p->inner, COLOR_PAIR(COL_NARRATOR_TEXT));
    wprintw(p->inner, "\n%s\n", msg);
    wattroff(p->inner, COLOR_PAIR(COL_NARRATOR_TEXT));
    wrefresh(p->inner);
}

void shell_print(Panel *p, const char *line) {
    wattron(p->inner, COLOR_PAIR(COL_SHELL_TEXT));
    wprintw(p->inner, "%s\n", line);
    wattroff(p->inner, COLOR_PAIR(COL_SHELL_TEXT));
    wrefresh(p->inner);
}

/* ═══════════════════════════════════════════════════════════════
 * BARRE D'ONGLETS
 * Format : [F1 name] [F2 name*] …   (* = activité hors focus)
 * ═══════════════════════════════════════════════════════════════ */
void draw_tabs(WINDOW *tab_bar, Shell *shells, int nshells,
               int active, int cols)
{
    werase(tab_bar);
    wbkgd(tab_bar, COLOR_PAIR(COL_SHELL_BORDER));

    int x = 0;
    for (int i = 0; i < nshells && x < cols - 4; i++) {
        char label[48];
        if (shells[i].has_activity)
            snprintf(label, sizeof(label), "[F%d %s!]", i + 1, shells[i].name);
        else
            snprintf(label, sizeof(label), "[F%d %s]",  i + 1, shells[i].name);

        if (i == active) {
            wattron(tab_bar, COLOR_PAIR(COL_PROMPT) | A_BOLD);
            mvwprintw(tab_bar, 0, x, "%s", label);
            wattroff(tab_bar, COLOR_PAIR(COL_PROMPT) | A_BOLD);
        } else {
            wattron(tab_bar,
                shells[i].has_activity
                    ? (COLOR_PAIR(COL_NARRATOR_TEXT) | A_BOLD)
                    : COLOR_PAIR(COL_SHELL_BORDER));
            mvwprintw(tab_bar, 0, x, "%s", label);
            wattroff(tab_bar, A_BOLD | COLOR_PAIR(COL_NARRATOR_TEXT)
                              | COLOR_PAIR(COL_SHELL_BORDER));
        }
        x += (int)strlen(label) + 1;
    }
    wrefresh(tab_bar);
}
