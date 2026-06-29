#ifndef CONTAINER_H
#define CONTAINER_H

#define CONTAINER_NAME      "sysadmin-game"
#define CONTAINER_IMAGE     "sysadmin-game:latest"
#define CONTAINER_NETWORK   "sysadmin-net"
#define CONTAINER_SSH_PORT  2222   /* port hôte du conteneur principal */
#define SSH_USER            "player"
#define SSH_PASSWORD        "datacenter2031"

/* Retourne 1 si le conteneur est en cours d'exécution, 0 sinon. */
int  container_is_running(const char *name);

/*
 * Crée le réseau Podman partagé si nécessaire.
 * Tous les conteneurs du jeu le rejoignent → DNS inter-conteneurs.
 */
int  container_init_network(void);

/*
 * Vérifie que le conteneur principal est en cours d'exécution.
 * Le construit / crée / démarre si nécessaire.
 * Bloquant (peut prendre ~30s au premier lancement pour le build).
 * Retourne 0 si prêt, -1 en cas d'échec.
 */
int  container_ensure_running(void);

/*
 * Déploie un nouveau conteneur sur le réseau du jeu.
 *   name       : nom Podman + hostname (ex: "web01")
 *   image      : image OCI (ex: "sysadmin-game:latest")
 *   ssh_port   : port hôte exposé pour SSH (127.0.0.1:ssh_port → :22)
 *   extra_nets : réseaux Podman additionnels à connecter (peut être NULL)
 *   nnets      : nombre d'éléments dans extra_nets
 *
 * Le conteneur est persistant. Retourne 0 si prêt, -1 en cas d'échec.
 */
int  container_deploy(const char *name, const char *image, int ssh_port,
                      const char **extra_nets, int nnets);

/* Arrête un conteneur sans le supprimer. */
void container_stop(const char *name);

/* Crée un réseau Podman. Idempotent. Retourne 0 si prêt, -1 en cas d'échec. */
int  container_network_create(const char *name);

/* Supprime un réseau Podman. Retourne 0 si ok, -1 en cas d'échec. */
int  container_network_delete(const char *name);

#endif /* CONTAINER_H */
