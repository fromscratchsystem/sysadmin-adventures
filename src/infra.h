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

/* ─── Slots hardware par serveur ─────────────────────────────── */
#define HW_RAM_SLOTS   4
#define HW_DISK_SLOTS  4

/* ─── Baie de brassage ───────────────────────────────────────── */
typedef struct {
    char name[32];
    int  units;
} Rack;

/* ─── Serveur physique ───────────────────────────────────────── */
typedef struct {
    char name[32];
    char rack[32];
    int  slot;           /* position dans la baie (base 1) */
    int  size_u;         /* hauteur en U (1 pour les mini PCs aussi) */
    int  cpu;            /* cœurs — recalculé depuis hw_cpu si installé */
    int  ram_mb;         /* RAM  — recalculée depuis hw_ram[] */
    int  disk_gb;        /* disk — recalculé depuis hw_disk[] */
    int  powered;
    int  port;
    /* Forme et gestion */
    int  is_minipc;      /* 1 = mini PC (2 par 1U) */
    int  subslot;        /* 0 ou 1 dans le même U (mini PC seulement) */
    int  has_ipmi;       /* 0 = pas d'IPMI, intervention physique requise */
    int  max_ram_slots;  /* limite du modèle (0 = HW_RAM_SLOTS) */
    int  max_disk_slots; /* limite du modèle (0 = HW_DISK_SLOTS) */
    char model_id[32];   /* id du modèle de châssis, "" si aucun */
    /* Composants matériels installés (IDs du catalogue, "" = vide) */
    char hw_cpu [32];
    char hw_ram [HW_RAM_SLOTS ][32];
    char hw_disk[HW_DISK_SLOTS][32];
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
 *          -4 : slot invalide (< 1)
 *          -5 : slot occupé
 */
int infra_rack_create  (Infra *inf, const char *name, int units);
int infra_server_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int size_u, int cpu, int ram_mb, int disk_gb);
/*
 * Ajout avec modèle de châssis (size_u, has_ipmi, max_slots depuis le modèle).
 * Retourne les mêmes codes qu'infra_server_add, plus :
 *          -7 : modèle inconnu
 */
int infra_server_add_model(Infra *inf, const char *name, const char *rack,
                           int slot, int size_u, int has_ipmi,
                           int max_ram, int max_disk, const char *model_id);
/*
 * Ajout d'un mini PC (2 par 1U, subslot auto-assigné).
 * Retourne les mêmes codes qu'infra_server_add.
 */
int infra_minipc_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int max_ram, int max_disk, const char *model_id);
int infra_switch_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int size_u, int ports);
int infra_cable_connect(Infra *inf, const char *server, const char *nic,
                        const char *sw, int port);

/* ── Suppression ──────────────────────────────────────────────── */
/*
 * Retourne  0 : succès
 *          -1 : introuvable
 *          -2 : dépendances (baie non vide, serveur/switch allumé)
 */
int infra_rack_delete     (Infra *inf, const char *name);
int infra_server_delete   (Infra *inf, const char *name);
int infra_switch_delete   (Infra *inf, const char *name);
int infra_cable_disconnect(Infra *inf, const char *server, const char *nic);

/* ── Requêtes ─────────────────────────────────────────────────── */
/* Retourne les noms de réseaux Podman associés à un serveur via ses câbles. */
int infra_server_nets(const Infra *inf, const char *server,
                      const char *nets_out[], int max_nets);

/* ── Rendu texte (sans dépendance ncurses) ────────────────────── */
void infra_rack_render   (const Infra *inf, const char *rack_name,
                          char lines[][128], int *nlines, int max_lines);
void infra_server_show   (const Infra *inf, const char *name,
                          char lines[][128], int *nlines, int max_lines);
void infra_switch_show   (const Infra *inf, const char *name,
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
