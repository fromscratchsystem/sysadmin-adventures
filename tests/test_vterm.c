#include "framework.h"
#include "vterm.h"   /* picks up tests/ncurses.h via -Itests/ first */
#include <string.h>

/* ─── helpers ─────────────────────────────────────────────────── */

/* Caractère à la position (row, col) dans le buffer courant */
static int cell_ch(VTerm *vt, int row, int col) {
    return vt->cur[row * vt->cols + col].ch;
}

/* Attributs à la position (row, col) */
static attr_t cell_attrs(VTerm *vt, int row, int col) {
    return vt->cur[row * vt->cols + col].attrs;
}

/* Fg color à la position donnée (champ cur_fg du vterm après SGR) */
/* On inspecte l'état courant du vterm, pas les cellules. */

/* Ligne i du scrollback (0 = plus ancienne) — premier char de la ligne */
static int sb_ch(VTerm *vt, int i, int col) {
    int idx = (vt->sb_head - vt->sb_count + i + vt->sb_capacity) % vt->sb_capacity;
    return vt->scrollback[idx * vt->cols + col].ch;
}

/* Envoie une séquence de bytes au vterm */
static void feed(VTerm *vt, const char *s) {
    vterm_process(vt, s, (int)strlen(s));
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : cycle de vie
 * ═══════════════════════════════════════════════════════════════ */

TEST(new_and_free) {
    VTerm *vt = vterm_new(24, 80, 8);
    ASSERT_NN(vt);
    ASSERT_EQ(vt->rows, 24);
    ASSERT_EQ(vt->cols, 80);
    ASSERT_EQ(vt->crow,  0);
    ASSERT_EQ(vt->ccol,  0);
    ASSERT_EQ(vt->in_altscreen, 0);
    ASSERT_EQ(vt->sb_count, 0);
    ASSERT_EQ(vt->sb_offset, 0);
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : affichage de caractères
 * ═══════════════════════════════════════════════════════════════ */

TEST(write_char) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "A");
    ASSERT_EQ(cell_ch(vt, 0, 0), 'A');
    ASSERT_EQ(vt->crow, 0);
    ASSERT_EQ(vt->ccol, 1);
    vterm_free(vt);
}

TEST(write_line) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "Hello");
    ASSERT_EQ(cell_ch(vt, 0, 0), 'H');
    ASSERT_EQ(cell_ch(vt, 0, 4), 'o');
    ASSERT_EQ(vt->ccol, 5);
    vterm_free(vt);
}

TEST(carriage_return) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "ABC\r");
    ASSERT_EQ(vt->ccol, 0);  /* retour en début de ligne */
    ASSERT_EQ(vt->crow, 0);
    vterm_free(vt);
}

TEST(newline_moves_cursor_down) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "A\n");
    ASSERT_EQ(vt->crow, 1);
    ASSERT_EQ(vt->ccol, 1);  /* LF = line feed seul, pas de CR : col inchangé */
    vterm_free(vt);
}

TEST(crlf_moves_cursor) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "AB\r\n");
    ASSERT_EQ(vt->crow, 1);
    ASSERT_EQ(vt->ccol, 0);
    vterm_free(vt);
}

TEST(backspace) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "AB\b");
    ASSERT_EQ(vt->ccol, 1);  /* recule d'une position */
    vterm_free(vt);
}

TEST(tab_stop) {
    VTerm *vt = vterm_new(5, 20, 8);
    feed(vt, "A\t");
    ASSERT_EQ(vt->ccol, 8);  /* premier tabstop à 8 */
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : mouvements curseur CSI
 * ═══════════════════════════════════════════════════════════════ */

TEST(csi_cursor_up) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\n\n\n");             /* crow = 3 */
    feed(vt, "\x1b[2A");           /* CUU 2 → crow = 1 */
    ASSERT_EQ(vt->crow, 1);
    vterm_free(vt);
}

TEST(csi_cursor_position) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[3;5H");  /* CUP row=3 col=5 → crow=2 ccol=4 (0-based) */
    ASSERT_EQ(vt->crow, 2);
    ASSERT_EQ(vt->ccol, 4);
    vterm_free(vt);
}

TEST(csi_erase_line) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "ABCDE");
    feed(vt, "\r\x1b[K");   /* retour début de ligne + EL0 (efface à droite) */
    ASSERT_EQ(cell_ch(vt, 0, 0), ' ');
    ASSERT_EQ(cell_ch(vt, 0, 4), ' ');
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : attributs SGR
 * ═══════════════════════════════════════════════════════════════ */

TEST(sgr_bold) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[1m");   /* SGR bold */
    ASSERT_NE(vt->cur_attrs & A_BOLD, 0);
    vterm_free(vt);
}

TEST(sgr_color_fg) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[31m");  /* SGR fg rouge */
    ASSERT_EQ(vt->cur_fg, 2);   /* 31-29 = 2 */
    vterm_free(vt);
}

TEST(sgr_reset) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[1m\x1b[31m");  /* bold + rouge */
    feed(vt, "\x1b[0m");          /* reset */
    ASSERT_EQ(vt->cur_attrs & A_BOLD, 0);
    ASSERT_EQ(vt->cur_fg, 0);
    ASSERT_EQ(vt->cur_bg, 0);
    vterm_free(vt);
}

TEST(sgr_bold_written_to_cell) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[1mX");   /* bold puis 'X' */
    ASSERT_NE(cell_attrs(vt, 0, 0) & A_BOLD, 0);
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : scrollback
 * ═══════════════════════════════════════════════════════════════ */

TEST(scroll_pushes_to_scrollback) {
    VTerm *vt = vterm_new(3, 10, 8);
    /* Remplir les 3 lignes puis provoquer un scroll */
    feed(vt, "AAA\r\nBBB\r\nCCC\r\n");  /* scroll : ligne 0 (AAA) quitte l'écran */
    ASSERT_GT(vt->sb_count, 0);
    ASSERT_EQ(sb_ch(vt, 0, 0), 'A');
    vterm_free(vt);
}

TEST(altscreen_no_scrollback) {
    VTerm *vt = vterm_new(3, 10, 8);
    feed(vt, "\x1b[?1049h");          /* entrer altscreen */
    feed(vt, "AAA\r\nBBB\r\nCCC\r\n"); /* scroll en altscreen */
    ASSERT_EQ(vt->sb_count, 0);       /* scrollback inchangé */
    vterm_free(vt);
}

TEST(vterm_scroll_offset) {
    VTerm *vt = vterm_new(3, 10, 8);
    feed(vt, "AAA\r\nBBB\r\nCCC\r\nDDD\r\n");  /* sb_count >= 2 */
    int before = vt->sb_count;
    ASSERT_GT(before, 0);
    vterm_scroll(vt, 1);
    ASSERT_EQ(vt->sb_offset, 1);
    vterm_free(vt);
}

TEST(vterm_scroll_clamp_max) {
    VTerm *vt = vterm_new(3, 10, 8);
    feed(vt, "AAA\r\nBBB\r\nCCC\r\n");
    vterm_scroll(vt, 9999);
    ASSERT_EQ(vt->sb_offset, vt->sb_count);  /* clamped au max */
    vterm_free(vt);
}

TEST(vterm_scroll_clamp_min) {
    VTerm *vt = vterm_new(3, 10, 8);
    vterm_scroll(vt, -5);
    ASSERT_EQ(vt->sb_offset, 0);  /* ne descend pas sous 0 */
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : mouvements curseur CSI supplémentaires
 * ═══════════════════════════════════════════════════════════════ */

TEST(csi_cursor_down) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[3B");   /* CUD 3 → crow = 3 */
    ASSERT_EQ(vt->crow, 3);
    vterm_free(vt);
}

TEST(csi_cursor_forward) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[5C");   /* CUF 5 → ccol = 5 */
    ASSERT_EQ(vt->ccol, 5);
    vterm_free(vt);
}

TEST(csi_cursor_back) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[10C");  /* CUF 10 */
    feed(vt, "\x1b[3D");   /* CUB 3 → ccol = 7 */
    ASSERT_EQ(vt->ccol, 7);
    vterm_free(vt);
}

TEST(csi_cursor_position_default) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[5;8H"); /* aller en 5;8 */
    feed(vt, "\x1b[H");    /* CUP sans params → 1;1 → crow=0, ccol=0 */
    ASSERT_EQ(vt->crow, 0);
    ASSERT_EQ(vt->ccol, 0);
    vterm_free(vt);
}

TEST(cursor_clamp_cup) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[99;99H"); /* CUP hors limites */
    ASSERT_EQ(vt->crow, 4);  /* clamped à rows-1 */
    ASSERT_EQ(vt->ccol, 9);  /* clamped à cols-1 */
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : effacement CSI supplémentaire
 * ═══════════════════════════════════════════════════════════════ */

TEST(csi_erase_display_all) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "ABCDE");
    feed(vt, "\x1b[2J");   /* ED2 : efface tout l'écran */
    ASSERT_EQ(cell_ch(vt, 0, 0), ' ');
    ASSERT_EQ(cell_ch(vt, 0, 4), ' ');
    vterm_free(vt);
}

TEST(csi_erase_line_left) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "ABCDE");
    /* Retour en col 3, puis EL1 (efface du début jusqu'à la position courante) */
    feed(vt, "\r\x1b[3C\x1b[1K");
    ASSERT_EQ(cell_ch(vt, 0, 0), ' ');
    ASSERT_EQ(cell_ch(vt, 0, 3), ' ');
    /* 'E' (col 4) doit rester intact */
    ASSERT_EQ(cell_ch(vt, 0, 4), 'E');
    vterm_free(vt);
}

TEST(csi_erase_line_all) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "ABCDE");
    feed(vt, "\r\x1b[2K"); /* EL2 : efface toute la ligne */
    ASSERT_EQ(cell_ch(vt, 0, 0), ' ');
    ASSERT_EQ(cell_ch(vt, 0, 4), ' ');
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : séquences ESC
 * ═══════════════════════════════════════════════════════════════ */

TEST(esc_save_restore_cursor) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[5;8H"); /* crow=4, ccol=7 */
    feed(vt, "\x1b" "7"); /* DECSC : sauvegarde */
    feed(vt, "\x1b[1;1H"); /* déplace ailleurs */
    ASSERT_EQ(vt->crow, 0);
    feed(vt, "\x1b" "8"); /* DECRC : restaure */
    ASSERT_EQ(vt->crow, 4);
    ASSERT_EQ(vt->ccol, 7);
    vterm_free(vt);
}

TEST(esc_reverse_index) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\n\n");       /* crow = 2 */
    feed(vt, "\x1b" "M");  /* RI : remonte d'une ligne → crow = 1 */
    ASSERT_EQ(vt->crow, 1);
    vterm_free(vt);
}

TEST(csi_scroll_region) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "\x1b[3;7r"); /* DECSTBM : région lignes 3–7 (base 1) → 2–6 (base 0) */
    ASSERT_EQ(vt->scroll_top,    2);
    ASSERT_EQ(vt->scroll_bottom, 6);
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : attributs SGR supplémentaires
 * ═══════════════════════════════════════════════════════════════ */

TEST(sgr_underline) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[4m");    /* underline on */
    ASSERT_NE(vt->cur_attrs & A_UNDERLINE, 0);
    feed(vt, "\x1b[24m");   /* underline off */
    ASSERT_EQ(vt->cur_attrs & A_UNDERLINE, 0);
    vterm_free(vt);
}

TEST(sgr_reverse) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[7m");    /* reverse on */
    ASSERT_NE(vt->cur_attrs & A_REVERSE, 0);
    feed(vt, "\x1b[27m");   /* reverse off */
    ASSERT_EQ(vt->cur_attrs & A_REVERSE, 0);
    vterm_free(vt);
}

TEST(sgr_color_bg) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[42m");   /* bg vert : 42-39 = 3 */
    ASSERT_EQ(vt->cur_bg, 3);
    vterm_free(vt);
}

TEST(sgr_bright_fg) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "\x1b[91m");   /* bright red : 91-89=2, + A_BOLD */
    ASSERT_EQ(vt->cur_fg, 2);
    ASSERT_NE(vt->cur_attrs & A_BOLD, 0);
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : wrap de ligne
 * ═══════════════════════════════════════════════════════════════ */

TEST(line_wrap) {
    VTerm *vt = vterm_new(5, 5, 8);  /* 5 colonnes */
    feed(vt, "ABCDE");  /* remplit la ligne 0 → wrap_pending=1 */
    ASSERT_EQ(vt->crow, 0);
    ASSERT_EQ(vt->wrap_pending, 1);
    feed(vt, "X");      /* X force le passage à la ligne suivante */
    ASSERT_EQ(vt->crow, 1);
    ASSERT_EQ(cell_ch(vt, 1, 0), 'X');
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : scrollback supplémentaire
 * ═══════════════════════════════════════════════════════════════ */

TEST(scrollback_multiple) {
    VTerm *vt = vterm_new(3, 10, 8);
    /* Dépasse 3 lignes : 2 lignes doivent aller dans le scrollback */
    feed(vt, "AAA\r\nBBB\r\nCCC\r\nDDD\r\n");
    ASSERT_EQ(vt->sb_count, 2);
    ASSERT_EQ(sb_ch(vt, 0, 0), 'A');
    ASSERT_EQ(sb_ch(vt, 1, 0), 'B');
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : vterm_resize
 * ═══════════════════════════════════════════════════════════════ */

TEST(vterm_resize_basic) {
    VTerm *vt = vterm_new(10, 20, 8);
    feed(vt, "AB");
    vterm_resize(vt, 5, 40);
    ASSERT_EQ(vt->rows, 5);
    ASSERT_EQ(vt->cols, 40);
    ASSERT_EQ(vt->sb_count, 0);   /* scrollback réinitialisé */
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : écran alternatif
 * ═══════════════════════════════════════════════════════════════ */

TEST(altscreen_switch) {
    VTerm *vt = vterm_new(5, 10, 8);
    feed(vt, "hello");
    ASSERT_EQ(vt->in_altscreen, 0);
    feed(vt, "\x1b[?1049h");          /* entrer altscreen */
    ASSERT_EQ(vt->in_altscreen, 1);
    /* L'altscreen est vierge */
    ASSERT_EQ(cell_ch(vt, 0, 0), ' ');
    feed(vt, "\x1b[?1049l");          /* quitter altscreen */
    ASSERT_EQ(vt->in_altscreen, 0);
    /* Le contenu du screen principal est restauré */
    ASSERT_EQ(cell_ch(vt, 0, 0), 'h');
    vterm_free(vt);
}

/* ═══════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    puts("=== test_vterm ===");

    puts("-- cycle de vie --");
    new_and_free();

    puts("-- affichage --");
    write_char();
    write_line();
    carriage_return();
    newline_moves_cursor_down();
    crlf_moves_cursor();
    backspace();
    tab_stop();

    puts("-- CSI --");
    csi_cursor_up();
    csi_cursor_down();
    csi_cursor_forward();
    csi_cursor_back();
    csi_cursor_position();
    csi_cursor_position_default();
    cursor_clamp_cup();
    csi_erase_line();
    csi_erase_display_all();
    csi_erase_line_left();
    csi_erase_line_all();
    csi_scroll_region();

    puts("-- ESC --");
    esc_save_restore_cursor();
    esc_reverse_index();

    puts("-- SGR --");
    sgr_bold();
    sgr_color_fg();
    sgr_reset();
    sgr_bold_written_to_cell();
    sgr_underline();
    sgr_reverse();
    sgr_color_bg();
    sgr_bright_fg();

    puts("-- wrap --");
    line_wrap();

    puts("-- scrollback --");
    scroll_pushes_to_scrollback();
    altscreen_no_scrollback();
    scrollback_multiple();
    vterm_scroll_offset();
    vterm_scroll_clamp_max();
    vterm_scroll_clamp_min();

    puts("-- resize --");
    vterm_resize_basic();

    puts("-- altscreen --");
    altscreen_switch();

    RESULTS();
}
