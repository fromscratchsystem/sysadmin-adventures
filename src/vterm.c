#include "vterm.h"
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Parser states
 * ═══════════════════════════════════════════════════════════════════════════ */
enum {
    ST_NORMAL,
    ST_ESC,
    ST_CSI,
    ST_OSC,
    ST_OSC_ESC,
    ST_ESC_IGNORE   /* consume one byte after ESC( ESC) etc. */
};

/* ANSI color 0-7 to ncurses constant mapping (same order) */
static const short ansi_nc[8] = {
    COLOR_BLACK, COLOR_RED,   COLOR_GREEN, COLOR_YELLOW,
    COLOR_BLUE,  COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Color pair management — global cache shared among all VTerms
 *
 * ncurses pairs are global to the process: if each VTerm allocated
 * its own pairs from the same index, they would overwrite each other.
 * One cache is enough: a (fg, bg) combination always yields the same pair.
 * ═══════════════════════════════════════════════════════════════════════════ */

static short g_pair_cache[VTERM_NCOLORS][VTERM_NCOLORS];
static int   g_next_pair = -1;   /* -1 = not initialized */

static void init_global_pairs(int first_pair) {
    if (g_next_pair >= 0) return;
    g_next_pair = first_pair - 1;
    memset(g_pair_cache, -1, sizeof(g_pair_cache));
}

static short get_pair(short fg, short bg) {
    if (fg == 0 && bg == 0) return 0;
    if (g_pair_cache[fg][bg] >= 0) return g_pair_cache[fg][bg];

    short nc_fg = fg ? ansi_nc[fg - 1] : -1;
    short nc_bg = bg ? ansi_nc[bg - 1] : -1;
    short id = (short)(++g_next_pair);
    init_pair(id, nc_fg, nc_bg);
    g_pair_cache[fg][bg] = id;
    return id;
}

static void update_pair(VTerm *vt) {
    vt->cur_pair = get_pair(vt->cur_fg, vt->cur_bg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

static VCell make_blank(VTerm *vt) {
    return (VCell){ .ch = ' ', .color_pair = vt->cur_pair, .attrs = 0 };
}

static void fill_row(VTerm *vt, int row, int c0, int c1) {
    VCell blank = make_blank(vt);
    VCell *p = vt->cur + row * vt->cols;
    for (int c = c0; c <= c1; c++) p[c] = blank;
}

static void fill_screen(VTerm *vt) {
    VCell blank = make_blank(vt);
    int n = vt->rows * vt->cols;
    for (int i = 0; i < n; i++) vt->cur[i] = blank;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Scrollback buffer — helpers internes
 * ═══════════════════════════════════════════════════════════════════════════ */

static void sb_push(VTerm *vt, const VCell *row) {
    if (!vt->scrollback || vt->sb_capacity == 0) return;
    memcpy(vt->scrollback + vt->sb_head * vt->cols,
           row, (size_t)vt->cols * sizeof(VCell));
    vt->sb_head = (vt->sb_head + 1) % vt->sb_capacity;
    if (vt->sb_count < vt->sb_capacity) vt->sb_count++;
}

/* i=0 = oldest line, i=sb_count-1 = most recent */
static VCell *sb_row(VTerm *vt, int i) {
    int idx = (vt->sb_head - vt->sb_count + i + vt->sb_capacity) % vt->sb_capacity;
    return vt->scrollback + idx * vt->cols;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Scroll in active region
 * ═══════════════════════════════════════════════════════════════════════════ */

static void scroll_up_n(VTerm *vt, int n) {
    int top = vt->scroll_top, bot = vt->scroll_bottom;
    int h = bot - top + 1;
    if (n <= 0) return;
    /* Save to scrollback only for main screen
     * and when scroll region starts at top (top == 0).
     * This avoids polluting scrollback with vim/less/htop. */
    if (n >= h) {
        if (top == 0 && !vt->in_altscreen)
            for (int r = top; r <= bot; r++)
                sb_push(vt, vt->cur + r * vt->cols);
        for (int r = top; r <= bot; r++) fill_row(vt, r, 0, vt->cols - 1);
        return;
    }
    if (top == 0 && !vt->in_altscreen)
        for (int r = 0; r < n; r++)
            sb_push(vt, vt->cur + r * vt->cols);
    memmove(vt->cur + top * vt->cols,
            vt->cur + (top + n) * vt->cols,
            (size_t)(h - n) * (size_t)vt->cols * sizeof(VCell));
    for (int r = bot - n + 1; r <= bot; r++) fill_row(vt, r, 0, vt->cols - 1);
}

static void scroll_down_n(VTerm *vt, int n) {
    int top = vt->scroll_top, bot = vt->scroll_bottom;
    int h = bot - top + 1;
    if (n <= 0) return;
    if (n >= h) {
        for (int r = top; r <= bot; r++) fill_row(vt, r, 0, vt->cols - 1);
        return;
    }
    memmove(vt->cur + (top + n) * vt->cols,
            vt->cur + top * vt->cols,
            (size_t)(h - n) * (size_t)vt->cols * sizeof(VCell));
    for (int r = top; r < top + n; r++) fill_row(vt, r, 0, vt->cols - 1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Character display
 * ═══════════════════════════════════════════════════════════════════════════ */

static void put_char(VTerm *vt, unsigned char ch) {
    /* Resolve deferred wrap before writing */
    if (vt->wrap_pending) {
        vt->ccol = 0;
        vt->crow++;
        vt->wrap_pending = 0;
        if (vt->crow > vt->scroll_bottom) {
            vt->crow = vt->scroll_bottom;
            scroll_up_n(vt, 1);
        }
    }
    if (vt->crow < 0 || vt->crow >= vt->rows) return;
    if (vt->ccol < 0 || vt->ccol >= vt->cols) return;

    vt->cur[vt->crow * vt->cols + vt->ccol] =
        (VCell){ .ch = ch, .color_pair = vt->cur_pair, .attrs = vt->cur_attrs };

    vt->ccol++;
    if (vt->ccol >= vt->cols) {
        vt->ccol = vt->cols - 1;
        vt->wrap_pending = 1;
    }
}

static void clamp_cursor(VTerm *vt) {
    if (vt->crow < 0)           vt->crow = 0;
    if (vt->crow >= vt->rows)   vt->crow = vt->rows - 1;
    if (vt->ccol < 0)           vt->ccol = 0;
    if (vt->ccol >= vt->cols)   vt->ccol = vt->cols - 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SGR — attributs graphiques
 * ═══════════════════════════════════════════════════════════════════════════ */

static void do_sgr(VTerm *vt) {
    int i = 0;
    if (vt->nparams == 0) { vt->params[0] = 0; vt->nparams = 1; }

    while (i < vt->nparams) {
        int p = vt->params[i];
        switch (p) {
        case 0:
            vt->cur_fg = 0; vt->cur_bg = 0; vt->cur_attrs = 0;
            break;
        case 1:  vt->cur_attrs |= A_BOLD;       break;
        case 2:  vt->cur_attrs &= ~A_BOLD;       break;
        case 4:  vt->cur_attrs |= A_UNDERLINE;   break;
        case 7:  vt->cur_attrs |= A_REVERSE;     break;
        case 22: vt->cur_attrs &= ~A_BOLD;        break;
        case 24: vt->cur_attrs &= ~A_UNDERLINE;   break;
        case 27: vt->cur_attrs &= ~A_REVERSE;     break;
        /* Couleurs standard fg 30-37 */
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            vt->cur_fg = (short)(p - 29); break;
        /* 256-color fg : 38;5;n */
        case 38:
            if (i + 2 < vt->nparams && vt->params[i + 1] == 5) {
                int n = vt->params[i + 2];
                vt->cur_fg = (n < 8) ? (short)(n + 1) :
                             (n < 16) ? (short)(n - 7) : 0;
                i += 2;
            }
            break;
        case 39: vt->cur_fg = 0; break;
        /* Couleurs standard bg 40-47 */
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            vt->cur_bg = (short)(p - 39); break;
        /* 256-color bg : 48;5;n */
        case 48:
            if (i + 2 < vt->nparams && vt->params[i + 1] == 5) {
                int n = vt->params[i + 2];
                vt->cur_bg = (n < 8) ? (short)(n + 1) :
                             (n < 16) ? (short)(n - 7) : 0;
                i += 2;
            }
            break;
        case 49: vt->cur_bg = 0; break;
        /* Couleurs vives fg 90-97 */
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            vt->cur_fg = (short)(p - 89);
            vt->cur_attrs |= A_BOLD;
            break;
        /* Couleurs vives bg 100-107 */
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            vt->cur_bg = (short)(p - 99); break;
        default: break;
        }
        i++;
    }
    update_pair(vt);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CSI — control sequences
 * ═══════════════════════════════════════════════════════════════════════════ */

/* P(n, default): param n or default if missing / is 0 */
#define P(n, d) (((n) < vt->nparams && vt->params[(n)] > 0) \
                 ? vt->params[(n)] : (d))

static void do_csi(VTerm *vt, char cmd) {
    int n;

    if (vt->private_flag) {
        /* ── Private DEC modes ── */
        for (int i = 0; i < vt->nparams; i++) {
            int mode = vt->params[i];
            if (cmd == 'h') {          /* set */
                if (mode == 1049) {    /* alternate screen + save cursor */
                    vt->saved_crow = vt->crow;
                    vt->saved_ccol = vt->ccol;
                    vt->in_altscreen = 1;
                    vt->cur = vt->altscreen;
                    vt->crow = 0; vt->ccol = 0;
                    fill_screen(vt);
                } else if (mode == 47) { /* simple alternate screen */
                    vt->in_altscreen = 1;
                    vt->cur = vt->altscreen;
                }
            } else if (cmd == 'l') {   /* reset */
                if (mode == 1049) {
                    vt->in_altscreen = 0;
                    vt->cur = vt->screen;
                    vt->crow = vt->saved_crow;
                    vt->ccol = vt->saved_ccol;
                    clamp_cursor(vt);
                } else if (mode == 47) {
                    vt->in_altscreen = 0;
                    vt->cur = vt->screen;
                }
            }
        }
        return;
    }

    switch (cmd) {
    /* ── Cursor movement ── */
    case 'A': /* CUU */
        vt->crow -= P(0, 1);
        if (vt->crow < 0) vt->crow = 0;
        vt->wrap_pending = 0; break;
    case 'B': /* CUD */
        vt->crow += P(0, 1);
        if (vt->crow >= vt->rows) vt->crow = vt->rows - 1;
        vt->wrap_pending = 0; break;
    case 'C': /* CUF */
        vt->ccol += P(0, 1);
        if (vt->ccol >= vt->cols) vt->ccol = vt->cols - 1;
        vt->wrap_pending = 0; break;
    case 'D': /* CUB */
        vt->ccol -= P(0, 1);
        if (vt->ccol < 0) vt->ccol = 0;
        vt->wrap_pending = 0; break;
    case 'E': /* CNL */
        vt->crow += P(0, 1);
        if (vt->crow >= vt->rows) vt->crow = vt->rows - 1;
        vt->ccol = 0; vt->wrap_pending = 0; break;
    case 'F': /* CPL */
        vt->crow -= P(0, 1);
        if (vt->crow < 0) vt->crow = 0;
        vt->ccol = 0; vt->wrap_pending = 0; break;
    case 'G': /* CHA */
        vt->ccol = P(0, 1) - 1;
        clamp_cursor(vt); vt->wrap_pending = 0; break;
    case 'd': /* VPA */
        vt->crow = P(0, 1) - 1;
        clamp_cursor(vt); vt->wrap_pending = 0; break;
    case 'H': /* CUP */
    case 'f': /* HVP */
        vt->crow = P(0, 1) - 1;
        vt->ccol = P(1, 1) - 1;
        clamp_cursor(vt); vt->wrap_pending = 0; break;

    /* ── Effacement ── */
    case 'J': /* ED */
        n = P(0, 0);
        if (n == 0) {
            fill_row(vt, vt->crow, vt->ccol, vt->cols - 1);
            for (int r = vt->crow + 1; r < vt->rows; r++)
                fill_row(vt, r, 0, vt->cols - 1);
        } else if (n == 1) {
            for (int r = 0; r < vt->crow; r++)
                fill_row(vt, r, 0, vt->cols - 1);
            fill_row(vt, vt->crow, 0, vt->ccol);
        } else {
            fill_screen(vt);
        }
        break;
    case 'K': /* EL */
        n = P(0, 0);
        if (n == 0)      fill_row(vt, vt->crow, vt->ccol, vt->cols - 1);
        else if (n == 1) fill_row(vt, vt->crow, 0, vt->ccol);
        else             fill_row(vt, vt->crow, 0, vt->cols - 1);
        break;

    /* ── Insertion / suppression de lignes ── */
    case 'L': { /* IL */
        int old_top = vt->scroll_top;
        vt->scroll_top = vt->crow;
        scroll_down_n(vt, P(0, 1));
        vt->scroll_top = old_top;
        vt->ccol = 0; break;
    }
    case 'M': { /* DL */
        int old_top = vt->scroll_top;
        vt->scroll_top = vt->crow;
        scroll_up_n(vt, P(0, 1));
        vt->scroll_top = old_top;
        vt->ccol = 0; break;
    }

    /* ── Character insertion / deletion ── */
    case '@': { /* ICH */
        n = P(0, 1);
        VCell blank = make_blank(vt);
        VCell *row = vt->cur + vt->crow * vt->cols;
        int avail = vt->cols - vt->ccol;
        if (n > avail) n = avail;
        memmove(row + vt->ccol + n, row + vt->ccol,
                (size_t)(avail - n) * sizeof(VCell));
        for (int c = vt->ccol; c < vt->ccol + n; c++) row[c] = blank;
        break;
    }
    case 'P': { /* DCH */
        n = P(0, 1);
        VCell blank = make_blank(vt);
        VCell *row = vt->cur + vt->crow * vt->cols;
        int avail = vt->cols - vt->ccol;
        if (n > avail) n = avail;
        memmove(row + vt->ccol, row + vt->ccol + n,
                (size_t)(avail - n) * sizeof(VCell));
        for (int c = vt->cols - n; c < vt->cols; c++) row[c] = blank;
        break;
    }

    /* ── Scroll ── */
    case 'S': scroll_up_n  (vt, P(0, 1)); break; /* SU */
    case 'T': scroll_down_n(vt, P(0, 1)); break; /* SD */

    /* ── Scroll region ── */
    case 'r': /* DECSTBM */
        vt->scroll_top    = P(0, 1)         - 1;
        vt->scroll_bottom = P(1, vt->rows)  - 1;
        if (vt->scroll_top < 0)              vt->scroll_top = 0;
        if (vt->scroll_bottom >= vt->rows)   vt->scroll_bottom = vt->rows - 1;
        if (vt->scroll_top >= vt->scroll_bottom) {
            vt->scroll_top = 0; vt->scroll_bottom = vt->rows - 1;
        }
        vt->crow = 0; vt->ccol = 0; vt->wrap_pending = 0;
        break;

    case 'm': do_sgr(vt); break;

    default: break;
    }
}

#undef P

/* ═══════════════════════════════════════════════════════════════════════════
 * Traitement de la sortie PTY
 * ═══════════════════════════════════════════════════════════════════════════ */

void vterm_process(VTerm *vt, const char *buf, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];

        switch (vt->state) {

        case ST_NORMAL:
            if      (c == 0x1b)                    { vt->state = ST_ESC; }
            else if (c == '\r')                    { vt->ccol = 0; vt->wrap_pending = 0; }
            else if (c == '\n' || c == '\v' || c == '\f') {
                vt->wrap_pending = 0;
                if (vt->crow == vt->scroll_bottom) scroll_up_n(vt, 1);
                else { vt->crow++; if (vt->crow >= vt->rows) vt->crow = vt->rows - 1; }
            }
            else if (c == '\b') {
                if (vt->ccol > 0) { vt->ccol--; vt->wrap_pending = 0; }
            }
            else if (c == '\t') {
                vt->ccol = (vt->ccol + 8) & ~7;
                if (vt->ccol >= vt->cols) vt->ccol = vt->cols - 1;
                vt->wrap_pending = 0;
            }
            else if (c == 0x07) { /* BEL : ignore */ }
            else if (c >= 0x20) { put_char(vt, c); }
            break;

        case ST_ESC:
            switch (c) {
            case '[':  /* CSI */
                vt->state = ST_CSI;
                vt->nparams = 0;
                vt->private_flag = 0;
                memset(vt->params, 0, sizeof(vt->params));
                break;
            case ']':  vt->state = ST_OSC; break;  /* OSC */
            case '(':  /* character set G0/G1 : consomme octet suivant */
            case ')':  vt->state = ST_ESC_IGNORE; break;
            case '7':  /* DECSC : sauvegarde curseur */
                vt->saved_crow = vt->crow; vt->saved_ccol = vt->ccol;
                vt->state = ST_NORMAL; break;
            case '8':  /* DECRC : restaure curseur */
                vt->crow = vt->saved_crow; vt->ccol = vt->saved_ccol;
                clamp_cursor(vt); vt->state = ST_NORMAL; break;
            case 'D':  /* IND : saut de ligne */
                if (vt->crow == vt->scroll_bottom) scroll_up_n(vt, 1);
                else { vt->crow++; if (vt->crow >= vt->rows) vt->crow = vt->rows - 1; }
                vt->state = ST_NORMAL; break;
            case 'M':  /* RI : index inverse */
                if (vt->crow == vt->scroll_top) scroll_down_n(vt, 1);
                else { vt->crow--; if (vt->crow < 0) vt->crow = 0; }
                vt->state = ST_NORMAL; break;
            case 'E':  /* NEL */
                if (vt->crow == vt->scroll_bottom) scroll_up_n(vt, 1);
                else { vt->crow++; if (vt->crow >= vt->rows) vt->crow = vt->rows - 1; }
                vt->ccol = 0; vt->state = ST_NORMAL; break;
            default:   vt->state = ST_NORMAL; break;
            }
            break;

        case ST_CSI:
            if (c == '?') {
                vt->private_flag = 1;
            } else if (c >= '0' && c <= '9') {
                if (vt->nparams == 0) vt->nparams = 1;
                if (vt->nparams <= 32)
                    vt->params[vt->nparams - 1] =
                        vt->params[vt->nparams - 1] * 10 + (c - '0');
            } else if (c == ';') {
                if (vt->nparams < 32) vt->nparams++;
            } else if (c >= '@' && c <= '~') {
                do_csi(vt, (char)c);
                vt->state = ST_NORMAL;
            }
            /* intermediate bytes 0x20-0x2F: ignored */
            break;

        case ST_OSC:
            if      (c == 0x07)  vt->state = ST_NORMAL;   /* BEL : fin OSC */
            else if (c == 0x1b)  vt->state = ST_OSC_ESC;
            break;

        case ST_OSC_ESC:
            vt->state = (c == '\\') ? ST_NORMAL : ST_OSC;
            break;

        case ST_ESC_IGNORE:
            vt->state = ST_NORMAL;
            break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Rendering in an ncurses window
 * ═══════════════════════════════════════════════════════════════════════════ */

void vterm_render(VTerm *vt, WINDOW *win) {
    for (int r = 0; r < vt->rows; r++) {
        VCell *row;
        if (vt->sb_offset > 0) {
            /* Scrollback view: view starts sb_offset lines before screen */
            int combined = vt->sb_count - vt->sb_offset + r;
            if (combined < 0) {
                wmove(win, r, 0);
                for (int c = 0; c < vt->cols; c++) waddch(win, ' ');
                continue;
            } else if (combined < vt->sb_count) {
                row = sb_row(vt, combined);
            } else {
                row = vt->cur + (combined - vt->sb_count) * vt->cols;
            }
        } else {
            row = vt->cur + r * vt->cols;
        }
        wmove(win, r, 0);
        for (int c = 0; c < vt->cols; c++) {
            VCell *cell = &row[c];
            waddch(win, (chtype)(unsigned char)cell->ch
                        | COLOR_PAIR(cell->color_pair)
                        | cell->attrs);
        }
    }
    /* Cursor visible only in real-time view */
    if (vt->sb_offset == 0)
        wmove(win, vt->crow, vt->ccol);
    wrefresh(win);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * API publique
 * ═══════════════════════════════════════════════════════════════════════════ */

VTerm *vterm_new(int rows, int cols, int first_pair) {
    VTerm *vt = calloc(1, sizeof(VTerm));
    if (!vt) return NULL;

    vt->rows = rows;
    vt->cols = cols;
    vt->screen    = calloc((size_t)rows * (size_t)cols, sizeof(VCell));
    vt->altscreen = calloc((size_t)rows * (size_t)cols, sizeof(VCell));
    vt->sb_capacity = SCROLLBACK_LINES;
    vt->scrollback  = calloc((size_t)SCROLLBACK_LINES * (size_t)cols, sizeof(VCell));
    if (!vt->screen || !vt->altscreen || !vt->scrollback) {
        vterm_free(vt); return NULL;
    }

    vt->cur           = vt->screen;
    vt->scroll_top    = 0;
    vt->scroll_bottom = rows - 1;

    init_global_pairs(first_pair);

    /* Initialise tous les buffers avec des espaces / paire 0 */
    VCell blank = { ' ', 0, 0 };
    for (int i = 0; i < rows * cols; i++) {
        vt->screen[i]    = blank;
        vt->altscreen[i] = blank;
    }
    return vt;
}

void vterm_free(VTerm *vt) {
    if (!vt) return;
    free(vt->screen);
    free(vt->altscreen);
    free(vt->scrollback);
    free(vt);
}

void vterm_resize(VTerm *vt, int rows, int cols) {
    free(vt->screen);
    free(vt->altscreen);
    free(vt->scrollback);

    vt->rows = rows;
    vt->cols = cols;
    vt->screen    = calloc((size_t)rows * (size_t)cols, sizeof(VCell));
    vt->altscreen = calloc((size_t)rows * (size_t)cols, sizeof(VCell));
    vt->scrollback = calloc((size_t)SCROLLBACK_LINES * (size_t)cols, sizeof(VCell));
    if (!vt->screen || !vt->altscreen || !vt->scrollback) return;

    vt->sb_count  = 0;
    vt->sb_head   = 0;
    vt->sb_offset = 0;

    vt->cur = vt->in_altscreen ? vt->altscreen : vt->screen;

    VCell blank = { ' ', 0, 0 };
    for (int i = 0; i < rows * cols; i++) {
        vt->screen[i]    = blank;
        vt->altscreen[i] = blank;
    }

    vt->scroll_top    = 0;
    vt->scroll_bottom = rows - 1;
    if (vt->crow >= rows) vt->crow = rows - 1;
    if (vt->ccol >= cols) vt->ccol = cols - 1;
    vt->wrap_pending = 0;
}

void vterm_scroll(VTerm *vt, int delta) {
    vt->sb_offset += delta;
    if (vt->sb_offset < 0)            vt->sb_offset = 0;
    if (vt->sb_offset > vt->sb_count) vt->sb_offset = vt->sb_count;
}
