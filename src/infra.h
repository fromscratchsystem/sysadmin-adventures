#ifndef INFRA_H
#define INFRA_H

#include <stdio.h>

/* ─── Limits ────────────────────────────────────────────────── */
#define MAX_RACKS      8
#define MAX_SERVERS   32
#define MAX_SWITCHES  16
#define MAX_CABLES    64
#define RACK_DEFAULT_U   42
#define PHYS_SSH_BASE  2300   /* ports 2300-2331 reserved for physical servers */

/* ─── Generic hardware slots ──────────────────────────────── */
#define MAX_HW_SLOTS  32

typedef struct {
    char type[16];      /* "cpu", "dimm", "pcie_x16", "u2", "m2", "sata", ... */
    char comp_id[32];   /* id of installed component, "" = empty */
} HWSlot;

/* ─── Rack ───────────────────────────────────────────────– */
typedef struct {
    char name[32];
    int  units;
} Rack;

/* ─── Physical server ───────────────────────────────────────── */
typedef struct {
    char name[32];
    char rack[32];
    int  slot;           /* position in rack (1-indexed) */
    int  size_u;         /* height in U (1 for mini PCs too) */
    int  cpu;            /* cores — recalculated from hw_slots */
    int  ram_mb;         /* RAM   — recalculated from hw_slots */
    int  disk_gb;        /* disk  — recalculated from hw_slots */
    int  powered;
    int  port;
    /* Form factor and management */
    int  is_minipc;      /* 1 = mini PC (2 per 1U) */
    int  subslot;        /* 0 or 1 in same U (mini PC only) */
    int  has_ipmi;       /* 0 = no IPMI, physical access required */
    char model_id[32];   /* id of chassis model, "" if none */
    /* Generic hardware slots (types and installed components) */
    HWSlot hw_slots[MAX_HW_SLOTS];
    int    nhw_slots;
} PhysServer;

/* ─── Physical switch ────────────────────────────────────────── */
/* Each switch is backed by a Podman network of the same name.    */
typedef struct {
    char name[32];
    char rack[32];
    int  slot;
    int  size_u;
    int  ports;     /* number of ports */
    int  powered;
} PhysSwitch;

/* ─── Cable connecting server NIC to switch port ───── */
typedef struct {
    char server[32];
    char nic[8];    /* e.g.: "eth0" */
    char sw[32];
    int  port;
} Cable;

/* ─── Complete infrastructure ─────────────────────────────────────────── */
typedef struct {
    Rack       racks   [MAX_RACKS];    int nracks;
    PhysServer servers [MAX_SERVERS];  int nservers;
    PhysSwitch switches[MAX_SWITCHES]; int nswitches;
    Cable      cables  [MAX_CABLES];   int ncables;
} Infra;

/* ── Search ────────────────────────────────────────────────── */
Rack       *infra_find_rack  (Infra *inf, const char *name);
PhysServer *infra_find_server(Infra *inf, const char *name);
PhysSwitch *infra_find_switch(Infra *inf, const char *name);

/* ── Construction ─────────────────────────────────────────────── */
/*
 * Returns  0: success
 *         -1: limit reached
 *         -2: unknown reference (rack, etc.)
 *         -3: name already in use
 *         -4: invalid slot (< 1)
 *         -5: slot occupied
 */
int infra_rack_create  (Infra *inf, const char *name, int units);
int infra_server_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int size_u, int cpu, int ram_mb, int disk_gb);
/*
 * Add with chassis model (size_u, has_ipmi from model).
 * Hardware slots are configured separately by hw_server_init_slots().
 * Returns same codes as infra_server_add.
 */
int infra_server_add_model(Infra *inf, const char *name, const char *rack,
                           int slot, int size_u, int has_ipmi,
                           const char *model_id);
/*
 * Add a mini PC (2 per 1U, subslot auto-assigned).
 * Returns same codes as infra_server_add.
 */
int infra_minipc_add   (Infra *inf, const char *name, const char *rack,
                        int slot, const char *model_id);
int infra_switch_add   (Infra *inf, const char *name, const char *rack,
                        int slot, int size_u, int ports);
/*
 * Returns  0: success
 *         -1: cable limit reached
 *         -2: unknown server or switch
 *         -3: this NIC is already cabled
 *         -4: port out of range (< 1 or > switch.ports)
 *         -5: this switch port is already occupied
 */
int infra_cable_connect(Infra *inf, const char *server, const char *nic,
                        const char *sw, int port);

/* ── Deletion ──────────────────────────────────────────────── */
/*
 * Returns  0: success
 *         -1: not found
 *         -2: dependencies (non-empty rack, powered server/switch)
 */
int infra_rack_delete     (Infra *inf, const char *name);
int infra_server_delete   (Infra *inf, const char *name);
int infra_switch_delete   (Infra *inf, const char *name);
int infra_cable_disconnect(Infra *inf, const char *server, const char *nic);

/* ── Queries ─────────────────────────────────────────────────── */
/* Returns names of Podman networks associated with a server via its cables. */
int infra_server_nets(const Infra *inf, const char *server,
                      const char *nets_out[], int max_nets);

/* ── Text rendering (no ncurses dependency) ────────────────────── */
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

/* ── Persistence ──────────────────────────────────────────────── */
void infra_save(const Infra *inf, const char *path);
void infra_load(Infra *inf, const char *path);

#endif /* INFRA_H */
