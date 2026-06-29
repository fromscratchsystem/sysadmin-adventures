#include "container.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int run_silent(const char *cmd)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s >/dev/null 2>&1", cmd);
    return system(buf);
}

static int image_exists(const char *image)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman image exists %s", image);
    return run_silent(cmd) == 0;
}

static int container_exists(const char *name)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman container exists %s", name);
    return run_silent(cmd) == 0;
}

int container_is_running(const char *name)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "podman inspect --format '{{.State.Running}}' %s 2>/dev/null | grep -q true",
        name);
    return system(cmd) == 0;
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

/* ── Réseau partagé ── */

int container_init_network(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman network exists %s", CONTAINER_NETWORK);
    if (run_silent(cmd) == 0) return 0;   /* déjà présent */

    fprintf(stderr, "[container] Création du réseau %s...\n", CONTAINER_NETWORK);
    snprintf(cmd, sizeof(cmd), "podman network create %s", CONTAINER_NETWORK);
    if (run_silent(cmd) != 0) {
        fprintf(stderr, "[container] Échec de la création du réseau.\n");
        return -1;
    }
    return 0;
}

/* ── Conteneur principal (joueur) ── */

int container_ensure_running(void)
{
    if (!image_exists(CONTAINER_IMAGE)) {
        fprintf(stderr, "[container] Construction de l'image %s (première fois, ~30s)...\n",
                CONTAINER_IMAGE);
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "podman build -t %s .", CONTAINER_IMAGE);
        if (system(cmd) != 0) {
            fprintf(stderr, "[container] Échec du build.\n");
            return -1;
        }
    }

    if (!container_exists(CONTAINER_NAME)) {
        fprintf(stderr, "[container] Création du conteneur %s...\n", CONTAINER_NAME);
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "podman run -d --name %s"
            " --network %s --hostname %s"
            " -p 127.0.0.1:%d:22 %s",
            CONTAINER_NAME,
            CONTAINER_NETWORK, CONTAINER_NAME,
            CONTAINER_SSH_PORT, CONTAINER_IMAGE);
        if (run_silent(cmd) != 0) {
            fprintf(stderr, "[container] Échec de la création.\n");
            return -1;
        }
    } else if (!container_is_running(CONTAINER_NAME)) {
        fprintf(stderr, "[container] Démarrage du conteneur %s...\n", CONTAINER_NAME);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "podman start %s", CONTAINER_NAME);
        if (run_silent(cmd) != 0) {
            fprintf(stderr, "[container] Échec du démarrage.\n");
            return -1;
        }
    }

    if (wait_for_ssh(CONTAINER_SSH_PORT, 15) < 0) {
        fprintf(stderr, "[container] SSH non disponible après 15s.\n");
        return -1;
    }
    return 0;
}

/* ── Conteneurs additionnels (scénarios) ── */

int container_deploy(const char *name, const char *image, int ssh_port,
                     const char **extra_nets, int nnets)
{
    if (!image_exists(image)) {
        fprintf(stderr, "[container] Image %s introuvable.\n", image);
        return -1;
    }

    if (!container_exists(name)) {
        fprintf(stderr, "[container] Déploiement du conteneur %s...\n", name);
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "podman run -d --name %s"
            " --network %s --hostname %s"
            " -p 127.0.0.1:%d:22 %s",
            name,
            CONTAINER_NETWORK, name,
            ssh_port, image);
        if (run_silent(cmd) != 0) {
            fprintf(stderr, "[container] Échec du déploiement de %s.\n", name);
            return -1;
        }
        /* Connexion aux réseaux additionnels */
        for (int i = 0; i < nnets; i++) {
            snprintf(cmd, sizeof(cmd),
                "podman network connect %s %s", extra_nets[i], name);
            if (run_silent(cmd) != 0)
                fprintf(stderr, "[container] Impossible de connecter %s au réseau %s.\n",
                        name, extra_nets[i]);
        }
    } else if (!container_is_running(name)) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "podman start %s", name);
        if (run_silent(cmd) != 0) return -1;
    }

    if (wait_for_ssh(ssh_port, 15) < 0) {
        fprintf(stderr, "[container] SSH de %s non disponible après 15s.\n", name);
        return -1;
    }
    return 0;
}

int container_network_create(const char *name)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman network exists %s", name);
    if (run_silent(cmd) == 0) return 0;   /* déjà présent */

    fprintf(stderr, "[container] Création du réseau %s...\n", name);
    snprintf(cmd, sizeof(cmd), "podman network create %s", name);
    return run_silent(cmd) == 0 ? 0 : -1;
}

int container_network_delete(const char *name)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman network rm %s", name);
    return run_silent(cmd) == 0 ? 0 : -1;
}

void container_stop(const char *name)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "podman stop %s", name);
    run_silent(cmd);
}
