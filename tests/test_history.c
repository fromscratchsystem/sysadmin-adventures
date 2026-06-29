#include "framework.h"
#include "history.h"
#include <string.h>

/* ─── helpers ─────────────────────────────────────────────────── */
static History h;
static char buf[CMD_MAX];
static char saved[CMD_MAX];

static void reset(void) {
    memset(&h,     0, sizeof(h));
    memset(buf,    0, CMD_MAX);
    memset(saved,  0, CMD_MAX);
}

/* ═══════════════════════════════════════════════════════════════ */

TEST(push_single) {
    reset();
    history_push(&h, "ls");
    ASSERT_EQ(h.count, 1);
    ASSERT_EQ(h.cursor, 1);
}

TEST(push_dedup_consecutive) {
    reset();
    history_push(&h, "ls");
    history_push(&h, "ls");
    ASSERT_EQ(h.count, 1);   /* doublon consécutif ignoré */
}

TEST(push_dedup_nonconsecutive) {
    reset();
    history_push(&h, "ls");
    history_push(&h, "pwd");
    history_push(&h, "ls");   /* pas consécutif → stocké */
    ASSERT_EQ(h.count, 3);
}

TEST(push_wrap) {
    reset();
    for (int i = 0; i < HISTORY_MAX + 5; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        history_push(&h, cmd);
    }
    ASSERT_EQ(h.count, HISTORY_MAX + 5);
    /* Le curseur est bien en fin d'historique */
    ASSERT_EQ(h.cursor, h.count);
}

TEST(prev_basic) {
    reset();
    history_push(&h, "cmd1");
    history_push(&h, "cmd2");
    history_push(&h, "cmd3");

    ASSERT_EQ(history_prev(&h, buf, CMD_MAX, saved), 1);
    ASSERT_STR(buf, "cmd3");
    ASSERT_EQ(history_prev(&h, buf, CMD_MAX, saved), 1);
    ASSERT_STR(buf, "cmd2");
    ASSERT_EQ(history_prev(&h, buf, CMD_MAX, saved), 1);
    ASSERT_STR(buf, "cmd1");
}

TEST(prev_at_oldest) {
    reset();
    history_push(&h, "cmd1");
    history_prev(&h, buf, CMD_MAX, saved);   /* remonte au plus ancien */
    ASSERT_EQ(history_prev(&h, buf, CMD_MAX, saved), 0);
    ASSERT_STR(buf, "cmd1");                 /* buf inchangé */
}

TEST(prev_empty_history) {
    reset();
    ASSERT_EQ(history_prev(&h, buf, CMD_MAX, saved), 0);
}

TEST(next_back_to_end) {
    reset();
    history_push(&h, "cmd1");
    history_push(&h, "cmd2");
    history_prev(&h, buf, CMD_MAX, saved);   /* cmd2 */
    history_prev(&h, buf, CMD_MAX, saved);   /* cmd1 */
    ASSERT_EQ(history_next(&h, buf, CMD_MAX, saved), 1);
    ASSERT_STR(buf, "cmd2");
}

TEST(next_restores_saved_input) {
    reset();
    history_push(&h, "cmd1");
    strncpy(buf, "mon brouillon", CMD_MAX - 1);
    history_prev(&h, buf, CMD_MAX, saved);   /* sauvegarde "mon brouillon" */
    ASSERT_STR(saved, "mon brouillon");
    history_next(&h, buf, CMD_MAX, saved);   /* retour en fin → restaure */
    ASSERT_STR(buf, "mon brouillon");
}

TEST(next_at_end) {
    reset();
    history_push(&h, "cmd1");
    ASSERT_EQ(history_next(&h, buf, CMD_MAX, saved), 0);
}

/* ═══════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    puts("=== test_history ===");
    push_single();
    push_dedup_consecutive();
    push_dedup_nonconsecutive();
    push_wrap();
    prev_basic();
    prev_at_oldest();
    prev_empty_history();
    next_back_to_end();
    next_restores_saved_input();
    next_at_end();
    RESULTS();
}
