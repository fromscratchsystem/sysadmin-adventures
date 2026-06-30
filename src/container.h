#ifndef CONTAINER_H
#define CONTAINER_H

#define CONTAINER_NAME      "sysadmin-game"
#define CONTAINER_IMAGE     "sysadmin-game:latest"
#define CONTAINER_NETWORK   "sysadmin-net"
#define CONTAINER_SSH_PORT  2222   /* host port of main container */
#define SSH_USER            "player"
#define SSH_PASSWORD        "datacenter2031"

/* Returns 1 if container is running, 0 otherwise. */
int  container_is_running(const char *name);

/*
 * Creates the shared Podman network if necessary.
 * All game containers join it → inter-container DNS.
 */
int  container_init_network(void);

/*
 * Verifies that the main container is running.
 * Builds / creates / starts it if necessary.
 * Blocking (may take ~30s on first launch for build).
 * Returns 0 if ready, -1 on failure.
 */
int  container_ensure_running(void);

/*
 * Deploys a new container on the game network.
 *   name       : Podman name + hostname (e.g.: "web01")
 *   image      : OCI image (e.g.: "sysadmin-game:latest")
 *   ssh_port   : host port exposed for SSH (127.0.0.1:ssh_port → :22)
 *   extra_nets : additional Podman networks to connect (may be NULL)
 *   nnets      : number of elements in extra_nets
 *
 * Container is persistent. Returns 0 if ready, -1 on failure.
 */
int  container_deploy(const char *name, const char *image, int ssh_port,
                      const char **extra_nets, int nnets);

/* Stops a container without removing it. */
void container_stop(const char *name);

/* Creates a Podman network. Idempotent. Returns 0 if ready, -1 on failure. */
int  container_network_create(const char *name);

/* Deletes a Podman network. Returns 0 if ok, -1 on failure. */
int  container_network_delete(const char *name);

/* Connects / disconnects sysadmin-net from a server container.
 * connect is idempotent (ignores "already connected"). */
int  container_mgmt_connect   (const char *name);
int  container_mgmt_disconnect(const char *name);

#endif /* CONTAINER_H */
