#ifndef HARDWARE_H
#define HARDWARE_H

#include "infra.h"

/* ─── Type de composant ──────────────────────────────────────── */
typedef enum { HW_CPU, HW_RAM, HW_DISK } HWType;

/* ─── Composant hardware ─────────────────────────────────────── */
typedef struct {
    char   id[32];
    char   label[64];
    HWType type;
    /* CPU */
    int    cores;
    int    ghz10;       /* GHz × 10, e.g. 26 = 2.6 GHz */
    char   mem_gen[8];  /* génération mémoire supportée/requise : "DDR3","DDR4","DDR5" */
    /* RAM */
    int    size_mb;
    /* Disque */
    int    size_gb;
    char   disk_type[8]; /* "HDD", "SSD", "NVMe" */
    int    iops;
} HWComp;

extern HWComp hw_catalog[];
extern int    hw_catalog_size;

int hw_catalog_load(const char *path);   /* charge depuis fichier, fallback défauts */
const HWComp *hw_find(const char *id);

/* ─── Modèle de serveur (châssis) ───────────────────────────── */
typedef struct {
    char  id[32];
    char  label[64];
    int   size_u;         /* 0 = mini PC (2 par 1U) */
    int   has_ipmi;       /* 0 = pas d'IPMI (mini PCs) */
    int   max_ram_slots;
    int   max_disk_slots;
} ServerModel;

extern ServerModel server_catalog[];
extern int         server_catalog_size;

int server_catalog_load(const char *path); /* charge depuis fichier, fallback défauts */
const ServerModel *srv_model_find(const char *id);

/* ─── Install / Retrait ──────────────────────────────────────── */
/*
 * Retourne  0 : succès
 *          -1 : composant inconnu / non installé (pour remove)
 *          -2 : serveur inconnu
 *          -3 : slots pleins ou CPU déjà installé
 *          -4 : serveur allumé
 *          -5 : incompatibilité mémoire (gen CPU ≠ gen RAM)
 */
int hw_install(Infra *inf, const char *server, const char *comp_id);
int hw_remove (Infra *inf, const char *server, const char *comp_id);

/* ─── Recalcul ───────────────────────────────────────────────── */
void hw_recompute    (PhysServer *srv);
void hw_recompute_all(Infra *inf);

/* ─── Affichage ──────────────────────────────────────────────── */
void hw_list_catalog      (HWType type, char lines[][128], int *nlines, int max);
void hw_list_server_models(char lines[][128], int *nlines, int max);
void hw_show_server       (const PhysServer *srv, char lines[][128], int *nlines, int max);

#endif /* HARDWARE_H */
