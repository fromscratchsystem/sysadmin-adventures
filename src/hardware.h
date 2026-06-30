#ifndef HARDWARE_H
#define HARDWARE_H

#include "infra.h"

/* ─── Generic component property ────────────────────── */
#define MAX_HW_PROPS   8

typedef struct {
    char key[16];
    char val[32];
} HWProp;

/* ─── Hardware component ─────────────────────────────────────── */
/* slot_type determines which slot this component installs in.   */
/* Examples: "cpu", "dimm", "pcie_x16", "pcie_x8", "pcie_x4",  */
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

/* ─── Server model (chassis) ───────────────────────── */
/* Each model declares its slots: type + count.              */
/* Example: cpu=1,dimm=4,pcie_x16=2,u2=4                        */
#define MAX_SLOT_DEFS  16

typedef struct {
    char type[16];
    int  count;
} SlotDef;

typedef struct {
    char    id[32];
    char    label[64];
    int     size_u;          /* 0 = mini PC (2 per 1U) */
    int     has_ipmi;        /* 0 = no IPMI */
    char    cpu_socket[16];  /* required CPU socket, "" = any socket */
    int     psu_w;           /* PSU power in W, 0 = unlimited */
    SlotDef slots[MAX_SLOT_DEFS];
    int     nslot_defs;
} ServerModel;

extern ServerModel server_catalog[];
extern int         server_catalog_size;

int                server_catalog_load(const char *path);
const ServerModel *srv_model_find     (const char *id);

/* ─── Server slot initialization ─────────────────── */
/* To be called from main.c after infra_server_add_model /        */
/* infra_minipc_add to configure hw_slots from the model.   */
/* If model_id is unknown, applies a default set of slots. */
void hw_server_init_slots(PhysServer *srv, const char *model_id);

/* ─── Install / Removal ──────────────────────────────────────── */
/*
 * Returns  0: success
 *         -1: unknown component / not installed (for remove)
 *         -2: unknown server
 *         -3: no free slot of this type on this server
 *         -4: server powered on
 *         -5: memory generation incompatibility (DDR3/4/5)
 *         -6: CPU socket incompatibility (e.g.: SP3 vs NUC)
 *         -7: PSU capacity exceeded
 */
int hw_install(Infra *inf, const char *server, const char *comp_id);
int hw_remove (Infra *inf, const char *server, const char *comp_id);

/* ─── Recalculate aggregated metrics ───────────────────────── */
void hw_recompute    (PhysServer *srv);
void hw_recompute_all(Infra *inf);

/* ─── Display ──────────────────────────────────────────────── */
/* slot_type_filter: NULL = display all, otherwise filter by type */
void hw_list_catalog      (const char *slot_type_filter,
                           char lines[][128], int *nlines, int max);
void hw_list_server_models(char lines[][128], int *nlines, int max);
void hw_show_server       (const PhysServer *srv,
                           char lines[][128], int *nlines, int max);

#endif /* HARDWARE_H */
