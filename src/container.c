#include "container.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CMD_BUF    512
#define SSH_WAIT_S  15

static int run_silent(const char *cmd)
{
    char buf[CMD_BUF + 32];
    snprintf(buf, sizeof(buf), "%s >/dev/null 2>&1", cmd);
    return system(buf);
}

static int image_exists(const char *image)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman image exists %s", image);
    return run_silent(cmd) == 0;
}

static int container_exists(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman container exists %s", name);
    return run_silent(cmd) == 0;
}

int container_is_running(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd),
        "podman inspect --format '{{.State.Running}}' %s 2>/dev/null | grep -q true",
        name);
    return system(cmd) == 0;
}

/* Starts a new container with standard sysadmin capabilities.
 * with_mgmt=1: joins sysadmin-net (main container only). */
static int container_start_new(const char *name, int port, const char *image,
                               int with_mgmt)
{
    char cmd[CMD_BUF];
    if (with_mgmt) {
        snprintf(cmd, sizeof(cmd),
            "podman run -d --name %s"
            " --network %s --hostname %s"
            " --cap-add NET_RAW --cap-add NET_ADMIN --cap-add SYS_PTRACE"
            " -p 127.0.0.1:%d:22 %s",
            name, CONTAINER_NETWORK, name, port, image);
    } else {
        snprintf(cmd, sizeof(cmd),
            "podman run -d --name %s"
            " --hostname %s"
            " --cap-add NET_RAW --cap-add NET_ADMIN --cap-add SYS_PTRACE"
            " -p 127.0.0.1:%d:22 %s",
            name, name, port, image);
    }
    return run_silent(cmd);
}

int container_mgmt_connect(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman network connect %s %s", CONTAINER_NETWORK, name);
    run_silent(cmd);   /* idempotent: ignores "already connected" */
    return 0;
}

int container_mgmt_disconnect(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman network disconnect %s %s", CONTAINER_NETWORK, name);
    return run_silent(cmd) == 0 ? 0 : -1;
}

static int tcp_probe(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int r = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    return r;
}

static int wait_for_ssh(int port, int max_s)
{
    for (int i = 0; i < max_s; i++) {
        if (tcp_probe(port) == 0) return 0;
        sleep(1);
    }
    return -1;
}

/* ── Shared network ── */

int container_init_network(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman network exists %s", CONTAINER_NETWORK);
    if (run_silent(cmd) == 0) return 0;   /* already present */

    fprintf(stderr, "[container] Creating network %s...\n", CONTAINER_NETWORK);
    snprintf(cmd, sizeof(cmd), "podman network create %s", CONTAINER_NETWORK);
    if (run_silent(cmd) != 0) {
        fprintf(stderr, "[container] Failed to create network.\n");
        return -1;
    }
    return 0;
}

/* ── Main container (player) ── */

int container_ensure_running(void)
{
    if (!image_exists(CONTAINER_IMAGE)) {
        fprintf(stderr, "[container] Building image %s (first time, ~30s)...\n",
                CONTAINER_IMAGE);
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "podman build -t %s .", CONTAINER_IMAGE);
        if (system(cmd) != 0) {
            fprintf(stderr, "[container] Build failed.\n");
            return -1;
        }
    }

    if (!container_exists(CONTAINER_NAME)) {
        fprintf(stderr, "[container] Creating container %s...\n", CONTAINER_NAME);
        if (container_start_new(CONTAINER_NAME, CONTAINER_SSH_PORT, CONTAINER_IMAGE, 1) != 0) {
            fprintf(stderr, "[container] Creation failed.\n");
            return -1;
        }
    } else if (!container_is_running(CONTAINER_NAME)) {
        fprintf(stderr, "[container] Starting container %s...\n", CONTAINER_NAME);
        char cmd[CMD_BUF];
        snprintf(cmd, sizeof(cmd), "podman start %s", CONTAINER_NAME);
        if (run_silent(cmd) != 0) {
            fprintf(stderr, "[container] Failed to start.\n");
            return -1;
        }
    }

    if (wait_for_ssh(CONTAINER_SSH_PORT, SSH_WAIT_S) < 0) {
        fprintf(stderr, "[container] SSH unavailable after %ds.\n", SSH_WAIT_S);
        return -1;
    }
    return 0;
}

/* ── Additional containers (scenarios) ── */

int container_deploy(const char *name, const char *image, int ssh_port,
                     const char **extra_nets, int nnets)
{
    if (!image_exists(image)) {
        fprintf(stderr, "[container] Image %s not found.\n", image);
        return -1;
    }

    if (!container_exists(name)) {
        fprintf(stderr, "[container] Deploying container %s...\n", name);
        if (container_start_new(name, ssh_port, image, 0) != 0) {
            fprintf(stderr, "[container] Failed to deploy %s.\n", name);
            return -1;
        }
        /* Connect to additional networks */
        char cmd[CMD_BUF];
        for (int i = 0; i < nnets; i++) {
            snprintf(cmd, sizeof(cmd),
                "podman network connect %s %s", extra_nets[i], name);
            if (run_silent(cmd) != 0)
                fprintf(stderr, "[container] Cannot connect %s to network %s.\n",
                        name, extra_nets[i]);
        }
    } else if (!container_is_running(name)) {
        char cmd[CMD_BUF];
        snprintf(cmd, sizeof(cmd), "podman start %s", name);
        if (run_silent(cmd) != 0) return -1;
    }

    if (wait_for_ssh(ssh_port, SSH_WAIT_S) < 0) {
        fprintf(stderr, "[container] SSH for %s unavailable after %ds.\n", name, SSH_WAIT_S);
        return -1;
    }
    return 0;
}

int container_network_create(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman network exists %s", name);
    if (run_silent(cmd) == 0) return 0;   /* already present */

    fprintf(stderr, "[container] Creating network %s...\n", name);
    snprintf(cmd, sizeof(cmd), "podman network create %s", name);
    return run_silent(cmd) == 0 ? 0 : -1;
}

int container_network_delete(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman network rm %s", name);
    return run_silent(cmd) == 0 ? 0 : -1;
}

void container_stop(const char *name)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "podman stop %s", name);
    run_silent(cmd);
}
