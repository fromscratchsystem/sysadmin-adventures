#ifndef INFRA_H
#define INFRA_H

#include <stdio.h>

/* ─── Limites ────────────────────────────────────────────────── */
#define MAX_RACKS      8
#define MAX_SERVERS   32
#define MAX_SWITCHES  16
#define MAX_CABLES    64
#define RACK_DEFAULT_U   42
#define PHYS_SSH_BASE  2300   /* ports 2300-2331 réservés aux serveurs physiques */

/* ─── Baie de brassage ───────────────────────────────────────── */
typedef struct {
    char name[32];
    int  units;
} Rack;

/* ─── Serveur physique ───────────────────────────────────────── */
typedef struct {
    char name[32];
    char rack[32];
    int  slot;      /* position dans la baie (base 1) */
    int  size_u;    /* hauteur en U */
    int  cpu;       /* nombre de cœurs */
    int  ram_mb;
    int  disk_gb;
    int  powered;   /* 1 = allumé */
    int  port;      /* port SSH hôte alloué */
} PhysServer;

/* ─── Switch physique ────────────────────────────────────────── */
/* Chaque switch est backed par un réseau Podman du même nom.    */
typedef struct {
    char name[32];
    char rack[32];
    int  slot;
    int  size_u;
    int  ports;     /* nombre de ports */
    int  powered;
} PhysSwitch;

/* ─── Câble entre une NIC de serveur et un port de switch ───── */
typedef struct {
    char server[32];
    char nic[8];    /* ex: "eth0" */
    char sw[32];
    int  port;
} Cable;

/* ─── Infra complète ─────────────────────────────────────────── */
typedef struct {
    Rack       racks   [MAX_RACKS];    int nracks;
    PhysServer servers [MAX_SERVERS];  int nservers;
    PhysSwitch switches[MAX_SWITCHES]; int nswitches;
    Cable      cables  [MAX_CABLES];   int ncables;
} Infra;

/* ── Recherche ────────────────────────────────────────────────── */
Rack       *infra_find_rack  (Infra *inf, const char *name);
PhysServer *infra_find_server(Infra *inf, const char *name);
PhysSwitch *infra_find_switch(Infra *inf, const char *name);

/* ── Construction ─────────────────────────────────────────────── */
/*
 * Retourne  0 : succès
 *          -1 : limite atteinte
 *          -2 : référence inconnue (baie, etc.)
 *          -3 : nom déjà utilisé
 *          -4 : slot occupé
 */
int infra_rack_create  (Infra *inf, const char *name, int units);
int infra_server_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int size_u, int cpu, int ram_mb, int disk_gb);
int infra_switch_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int size_u, int ports);
int infra_cable_connect(Infra *inf, const char *server, const char *nic,
                        const char *sw, int port);

/* ── Requêtes ─────────────────────────────────────────────────── */
/* Retourne les noms de réseaux Podman associés à un serveur via ses câbles. */
int infra_server_nets(const Infra *inf, const char *server,
                      const char *nets_out[], int max_nets);

/* ── Rendu texte (sans dépendance ncurses) ────────────────────── */
void infra_rack_render   (const Infra *inf, const char *rack_name,
                          char lines[][128], int *nlines, int max_lines);
void infra_list_racks    (const Infra *inf,
                          char lines[][128], int *nlines, int max_lines);
void infra_list_servers  (const Infra *inf, const char *rack,
                          char lines[][128], int *nlines, int max_lines);
void infra_list_switches (const Infra *inf, const char *rack,
                          char lines[][128], int *nlines, int max_lines);
void infra_list_cables   (const Infra *inf,
                          char lines[][128], int *nlines, int max_lines);

/* ── Persistance ──────────────────────────────────────────────── */
void infra_save(const Infra *inf, const char *path);
void infra_load(Infra *inf, const char *path);

#endif /* INFRA_H */
