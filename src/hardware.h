#ifndef HARDWARE_H
#define HARDWARE_H

#include "infra.h"

/* ─── Propriété générique d'un composant ────────────────────── */
#define MAX_HW_PROPS   8

typedef struct {
    char key[16];
    char val[32];
} HWProp;

/* ─── Composant hardware ─────────────────────────────────────── */
/* slot_type détermine dans quel slot ce composant s'installe.   */
/* Exemples : "cpu", "dimm", "pcie_x16", "pcie_x8", "pcie_x4",  */
/*            "u2", "m2", "sata", "gpu", ...                      */
typedef struct {
    char   id[32];
    char   label[64];
    char   slot_type[16];
    HWProp props[MAX_HW_PROPS];
    int    nprops;
} HWComp;

extern HWComp hw_catalog[];
extern int    hw_catalog_size;

int            hw_catalog_load(const char *path);
const HWComp  *hw_find        (const char *id);
const HWProp  *hw_prop        (const HWComp *c, const char *key);
int            hw_prop_int    (const HWComp *c, const char *key, int def);

/* ─── Modèle de serveur (châssis) ───────────────────────────── */
/* Chaque modèle déclare ses slots : type + nombre.              */
/* Exemple : cpu=1,dimm=4,pcie_x16=2,u2=4                        */
#define MAX_SLOT_DEFS  16

typedef struct {
    char type[16];
    int  count;
} SlotDef;

typedef struct {
    char    id[32];
    char    label[64];
    int     size_u;          /* 0 = mini PC (2 par 1U) */
    int     has_ipmi;        /* 0 = pas d'IPMI */
    char    cpu_socket[16];  /* socket CPU requis, "" = tout socket */
    SlotDef slots[MAX_SLOT_DEFS];
    int     nslot_defs;
} ServerModel;

extern ServerModel server_catalog[];
extern int         server_catalog_size;

int                server_catalog_load(const char *path);
const ServerModel *srv_model_find     (const char *id);

/* ─── Initialisation des slots d'un serveur ─────────────────── */
/* À appeler depuis main.c après infra_server_add_model /        */
/* infra_minipc_add pour configurer hw_slots depuis le modèle.   */
/* Si model_id est inconnu, applique un jeu de slots par défaut. */
void hw_server_init_slots(PhysServer *srv, const char *model_id);

/* ─── Install / Retrait ──────────────────────────────────────── */
/*
 * Retourne  0 : succès
 *          -1 : composant inconnu / non installé (pour remove)
 *          -2 : serveur inconnu
 *          -3 : aucun slot libre de ce type sur ce serveur
 *          -4 : serveur allumé
 *          -5 : incompatibilité génération mémoire (DDR3/4/5)
 *          -6 : incompatibilité socket CPU (ex : SP3 vs NUC)
 */
int hw_install(Infra *inf, const char *server, const char *comp_id);
int hw_remove (Infra *inf, const char *server, const char *comp_id);

/* ─── Recalcul des métriques agrégées ───────────────────────── */
void hw_recompute    (PhysServer *srv);
void hw_recompute_all(Infra *inf);

/* ─── Affichage ──────────────────────────────────────────────── */
/* slot_type_filter : NULL = tout afficher, sinon filtre sur le type */
void hw_list_catalog      (const char *slot_type_filter,
                           char lines[][128], int *nlines, int max);
void hw_list_server_models(char lines[][128], int *nlines, int max);
void hw_show_server       (const PhysServer *srv,
                           char lines[][128], int *nlines, int max);

#endif /* HARDWARE_H */
