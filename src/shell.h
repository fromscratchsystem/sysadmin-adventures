#ifndef SHELL_H
#define SHELL_H

#include "defs.h"
#include "ui.h"

/*
 * shell_spawn — opens an SSH session to a Podman container.
 *
 * name      : tab label (e.g.: "player@datacenter", "web01")
 * port      : host port on which the container exposes SSH
 */
Shell shell_spawn(int shell_rows, int shell_cols,
                  const char *name, int port);

/*
 * shell_send_raw — sends len raw bytes without adding '\n'.
 * Used to transmit control characters (Ctrl+C, Ctrl+D…).
 */
void shell_send_raw(Shell *sh, const char *buf, int len);

/*
 * shell_send_line — sends a command followed by '\n' via SSH channel.
 * No effect if sh->alive == 0.
 */
void shell_send_line(Shell *sh, const char *cmd);

/*
 * shell_read_output — reads everything available on the SSH channel
 * and feeds it to VTerm. If win != NULL, renders to the window.
 * Pass NULL for unfocused shells (VTerm updated in background).
 */
void shell_read_output(Shell *sh, WINDOW *win);

/*
 * shell_resize — notifies the remote pseudo-terminal of the new size.
 */
void shell_resize(Shell *sh, int rows, int cols);

/*
 * shell_close — closes the SSH channel and session cleanly.
 * The container remains running (persistent).
 */
void shell_close(Shell *sh);

#endif /* SHELL_H */
