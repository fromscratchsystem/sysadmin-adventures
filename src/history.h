#ifndef HISTORY_H
#define HISTORY_H

#include "defs.h"

/*
 * history_push — adds a command to history.
 * Consecutive duplicates are ignored (default bash behavior).
 * Cursor is reset to end of history after each addition.
 */
void history_push(History *h, const char *cmd);

/*
 * history_prev — goes back in history (up arrow key).
 * Copies command into buf (size maxlen).
 * Returns 1 if an entry was loaded, 0 if already at oldest.
 *
 * saved: buffer to save current input before first
 *        scroll up (restored by history_next when returning to end).
 */
int history_prev(History *h, char *buf, int maxlen, char *saved);

/*
 * history_next — goes forward in history (down arrow key).
 * Returns 1 if an entry was loaded, 0 if already at end.
 */
int history_next(History *h, char *buf, int maxlen, const char *saved);

#endif /* HISTORY_H */
