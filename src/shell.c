#include "shell.h"
#include "container.h"
#include "vterm.h"
#include <libssh2.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ═══════════════════════════════════════════════════════════════
 * shell_spawn
 *
 * Opens SSH connection to Podman container:
 *  1. TCP connect → 127.0.0.1:CONTAINER_SSH_PORT
 *  2. libssh2 handshake + password auth
 *  3. Open SSH channel with PTY (xterm-256color)
 *  4. Switch to non-blocking mode for select() integration
 * ═══════════════════════════════════════════════════════════════ */
Shell shell_spawn(int shell_rows, int shell_cols,
                  const char *name, int port)
{
    Shell sh;
    memset(&sh, 0, sizeof(sh));
    sh.alive = 0;
    sh.sock  = -1;
    sh.port  = port;
    strncpy(sh.name, name ? name : "shell", sizeof(sh.name) - 1);

    sh.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sh.sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sh.sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect SSH");
        goto fail_sock;
    }

    if (libssh2_init(0) != 0) {
        fprintf(stderr, "libssh2_init failed\n");
        exit(1);
    }

    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) { fprintf(stderr, "libssh2_session_init failed\n"); goto fail_exit; }

    libssh2_session_set_blocking(session, 1);

    if (libssh2_session_handshake(session, sh.sock) != 0) {
        fprintf(stderr, "SSH handshake failed\n");
        libssh2_session_free(session);
        goto fail_exit;
    }

    if (libssh2_userauth_password(session, SSH_USER, SSH_PASSWORD) != 0) {
        fprintf(stderr, "SSH auth failed\n");
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        goto fail_exit;
    }

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session);
    if (!channel) {
        fprintf(stderr, "SSH channel open failed\n");
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        goto fail_exit;
    }

    if (libssh2_channel_request_pty_ex(channel,
            "xterm-256color", 15, NULL, 0,
            shell_cols, shell_rows, 0, 0) != 0
        || libssh2_channel_shell(channel) != 0) {
        fprintf(stderr, "SSH PTY/shell request failed\n");
        libssh2_channel_free(channel);
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        goto fail_exit;
    }

    libssh2_session_set_blocking(session, 0);
    fcntl(sh.sock, F_SETFL, O_NONBLOCK);

    sh.ssh_session = session;
    sh.ssh_channel = channel;
    sh.vterm = vterm_new(shell_rows, shell_cols, 8);
    sh.alive = 1;
    return sh;

fail_exit:
    libssh2_exit();
fail_sock:
    close(sh.sock);
    sh.sock = -1;
    return sh;
}

/* ═══════════════════════════════════════════════════════════════
 * shell_send_raw
 * ═══════════════════════════════════════════════════════════════ */
void shell_send_raw(Shell *sh, const char *buf, int len)
{
    if (!sh->alive) return;
    LIBSSH2_CHANNEL *ch = (LIBSSH2_CHANNEL *)sh->ssh_channel;
    const char *p = buf;
    while (len > 0) {
        ssize_t n = libssh2_channel_write(ch, p, (size_t)len);
        if (n > 0) { p += n; len -= (int)n; }
        else if (n == LIBSSH2_ERROR_EAGAIN) { usleep(1000); }
        else { sh->alive = 0; return; }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * shell_send_line
 * ═══════════════════════════════════════════════════════════════ */
void shell_send_line(Shell *sh, const char *cmd)
{
    if (!sh->alive) return;

    LIBSSH2_CHANNEL *ch = (LIBSSH2_CHANNEL *)sh->ssh_channel;
    const char      *p  = cmd;
    size_t           rem = strlen(cmd);

    while (rem > 0) {
        ssize_t n = libssh2_channel_write(ch, p, rem);
        if (n > 0) {
            p   += n;
            rem -= (size_t)n;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            usleep(1000);
        } else {
            sh->alive = 0;
            return;
        }
    }

    /* send newline */
    while (libssh2_channel_write(ch, "\n", 1) == LIBSSH2_ERROR_EAGAIN)
        usleep(1000);
}

/* ═══════════════════════════════════════════════════════════════
 * shell_read_output
 *
 * Reads available data from SSH channel, passes to VTerm,
 * then renders screen in win.
 * ═══════════════════════════════════════════════════════════════ */
void shell_read_output(Shell *sh, WINDOW *win)
{
    LIBSSH2_CHANNEL *ch = (LIBSSH2_CHANNEL *)sh->ssh_channel;
    char             buf[4096];
    ssize_t          n;
    int              got_data = 0;

    for (;;) {
        n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0) {
            vterm_process(sh->vterm, buf, (int)n);
            got_data = 1;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            break;
        } else {
            /* n == 0 (EOF) or error */
            sh->alive = 0;
            break;
        }
    }

    if (!sh->alive && libssh2_channel_eof(ch) == 0)
        sh->alive = 1;   /* false positive: channel still open */

    if (libssh2_channel_eof(ch))
        sh->alive = 0;

    if (got_data && win)
        vterm_render(sh->vterm, win);
}

/* ═══════════════════════════════════════════════════════════════
 * shell_resize
 * ═══════════════════════════════════════════════════════════════ */
void shell_resize(Shell *sh, int rows, int cols)
{
    if (!sh->alive || !sh->ssh_channel) return;

    LIBSSH2_CHANNEL *ch = (LIBSSH2_CHANNEL *)sh->ssh_channel;

    /* libssh2 may return EAGAIN in non-blocking mode */
    int rc;
    do {
        rc = libssh2_channel_request_pty_size(ch, cols, rows);
    } while (rc == LIBSSH2_ERROR_EAGAIN);

    if (sh->vterm)
        vterm_resize(sh->vterm, rows, cols);
}

/* ═══════════════════════════════════════════════════════════════
 * shell_close
 * ═══════════════════════════════════════════════════════════════ */
void shell_close(Shell *sh)
{
    sh->alive = 0;
    if (sh->ssh_channel) {
        LIBSSH2_CHANNEL *ch = (LIBSSH2_CHANNEL *)sh->ssh_channel;
        libssh2_channel_close(ch);
        libssh2_channel_free(ch);
        sh->ssh_channel = NULL;
    }
    if (sh->ssh_session) {
        LIBSSH2_SESSION *sess = (LIBSSH2_SESSION *)sh->ssh_session;
        libssh2_session_disconnect(sess, "Bye");
        libssh2_session_free(sess);
        sh->ssh_session = NULL;
    }
    if (sh->sock >= 0) {
        close(sh->sock);
        sh->sock = -1;
    }
    if (sh->vterm) {
        vterm_free(sh->vterm);
        sh->vterm = NULL;
    }
    libssh2_exit();
}
