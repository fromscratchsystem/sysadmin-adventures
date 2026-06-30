#include "history.h"

void history_push(History *h, const char *cmd) {
    /* Ignore consecutive duplicates */
    if (h->count > 0 &&
        strcmp(h->entries[(h->count - 1) % HISTORY_MAX], cmd) == 0)
        return;

    strncpy(h->entries[h->count % HISTORY_MAX], cmd, CMD_MAX - 1);
    h->entries[h->count % HISTORY_MAX][CMD_MAX - 1] = '\0';
    h->count++;
    h->cursor = h->count;
}

int history_prev(History *h, char *buf, int maxlen, char *saved) {
    /* Save current input before first scroll up */
    if (h->cursor == h->count)
        strncpy(saved, buf, CMD_MAX - 1);

    int oldest = (h->count >= HISTORY_MAX) ? h->count - HISTORY_MAX : 0;
    if (h->cursor <= oldest)
        return 0;

    h->cursor--;
    strncpy(buf, h->entries[h->cursor % HISTORY_MAX], maxlen - 1);
    buf[maxlen - 1] = '\0';
    return 1;
}

int history_next(History *h, char *buf, int maxlen, const char *saved) {
    if (h->cursor >= h->count)
        return 0;

    h->cursor++;
    if (h->cursor == h->count)
        strncpy(buf, saved, maxlen - 1);
    else
        strncpy(buf, h->entries[h->cursor % HISTORY_MAX], maxlen - 1);

    buf[maxlen - 1] = '\0';
    return 1;
}
