#include "infra.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
 * Recherche
 * ═══════════════════════════════════════════════════════════════ */

Rack *infra_find_rack(Infra *inf, const char *name) {
    for (int i = 0; i < inf->nracks; i++)
        if (strcmp(inf->racks[i].name, name) == 0) return &inf->racks[i];
    return NULL;
}

PhysServer *infra_find_server(Infra *inf, const char *name) {
    for (int i = 0; i < inf->nservers; i++)
        if (strcmp(inf->servers[i].name, name) == 0) return &inf->servers[i];
    return NULL;
}

PhysSwitch *infra_find_switch(Infra *inf, const char *name) {
    for (int i = 0; i < inf->nswitches; i++)
        if (strcmp(inf->switches[i].name, name) == 0) return &inf->switches[i];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Vérification de disponibilité de slot
 * ═══════════════════════════════════════════════════════════════ */

static int slot_free(const Infra *inf, const char *rack, int slot, int size_u) {
    int end = slot + size_u - 1;
    for (int i = 0; i < inf->nservers; i++) {
        if (strcmp(inf->servers[i].rack, rack) != 0) continue;
        int s = inf->servers[i].slot;
        int e = s + inf->servers[i].size_u - 1;
        if (slot <= e && end >= s) return 0;
    }
    for (int i = 0; i < inf->nswitches; i++) {
        if (strcmp(inf->switches[i].rack, rack) != 0) continue;
        int s = inf->switches[i].slot;
        int e = s + inf->switches[i].size_u - 1;
        if (slot <= e && end >= s) return 0;
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 * Construction
 * ═══════════════════════════════════════════════════════════════ */

int infra_rack_create(Infra *inf, const char *name, int units) {
    if (inf->nracks >= MAX_RACKS)        return -1;
    if (infra_find_rack(inf, name))      return -3;
    Rack *r = &inf->racks[inf->nracks++];
    strncpy(r->name, name, 31);
    r->units = units > 0 ? units : RACK_DEFAULT_U;
    return 0;
}

int infra_server_add(Infra *inf, const char *name, const char *rack,
                     int slot, int size_u, int cpu, int ram_mb, int disk_gb) {
    if (inf->nservers >= MAX_SERVERS)       return -1;
    if (!infra_find_rack(inf, rack))        return -2;
    if (infra_find_server(inf, name))       return -3;
    if (size_u < 1) size_u = 1;
    if (slot < 1)                           return -4;
    if (!slot_free(inf, rack, slot, size_u)) return -5;

    int port = PHYS_SSH_BASE;
    for (int i = 0; i < inf->nservers; i++)
        if (inf->servers[i].port >= port) port = inf->servers[i].port + 1;

    PhysServer *s = &inf->servers[inf->nservers];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, 31);
    strncpy(s->rack, rack, 31);
    s->slot    = slot;
    s->size_u  = size_u;
    s->cpu     = cpu     > 0 ? cpu     : 1;
    s->ram_mb  = ram_mb  > 0 ? ram_mb  : 2048;
    s->disk_gb = disk_gb > 0 ? disk_gb : 100;
    s->powered = 0;
    s->port    = port;
    s->has_ipmi       = 1;
    s->max_ram_slots  = HW_RAM_SLOTS;
    s->max_disk_slots = HW_DISK_SLOTS;
    inf->nservers++;
    return 0;
}

int infra_switch_add(Infra *inf, const char *name, const char *rack,
                     int slot, int size_u, int ports) {
    if (inf->nswitches >= MAX_SWITCHES)      return -1;
    if (!infra_find_rack(inf, rack))         return -2;
    if (infra_find_switch(inf, name))        return -3;
    if (size_u < 1) size_u = 1;
    if (slot < 1)                            return -4;
    if (!slot_free(inf, rack, slot, size_u)) return -5;

    PhysSwitch *sw = &inf->switches[inf->nswitches++];
    strncpy(sw->name, name, 31);
    strncpy(sw->rack, rack, 31);
    sw->slot   = slot;
    sw->size_u = size_u;
    sw->ports  = ports > 0 ? ports : 24;
    sw->powered = 0;
    return 0;
}

int infra_cable_connect(Infra *inf, const char *server, const char *nic,
                        const char *sw, int port) {
    if (inf->ncables >= MAX_CABLES)      return -1;
    if (!infra_find_server(inf, server)) return -2;
    PhysSwitch *psw = infra_find_switch(inf, sw);
    if (!psw)                            return -2;
    if (port < 1 || port > psw->ports)  return -4;
    for (int i = 0; i < inf->ncables; i++) {
        if (strcmp(inf->cables[i].server, server) == 0 &&
            strcmp(inf->cables[i].nic,    nic)    == 0) return -3;
        if (strcmp(inf->cables[i].sw,     sw)     == 0 &&
            inf->cables[i].port == port)               return -5;
    }

    Cable *c = &inf->cables[inf->ncables++];
    strncpy(c->server, server, 31);
    strncpy(c->nic,    nic,     7);
    strncpy(c->sw,     sw,     31);
    c->port = port;
    return 0;
}

int infra_server_add_model(Infra *inf, const char *name, const char *rack,
                           int slot, int size_u, int has_ipmi,
                           int max_ram, int max_disk, const char *model_id) {
    int rc = infra_server_add(inf, name, rack, slot, size_u, 1, 2048, 100);
    if (rc != 0) return rc;
    PhysServer *s = infra_find_server(inf, name);
    if (s) {
        s->has_ipmi       = has_ipmi;
        s->max_ram_slots  = max_ram  > 0 && max_ram  < HW_RAM_SLOTS  ? max_ram  : HW_RAM_SLOTS;
        s->max_disk_slots = max_disk > 0 && max_disk < HW_DISK_SLOTS ? max_disk : HW_DISK_SLOTS;
        if (model_id && model_id[0]) strncpy(s->model_id, model_id, 31);
    }
    return 0;
}

/* Trouve le subslot libre (0 ou 1) pour un mini PC dans un slot 1U.
 * Retourne -1 si le slot est plein ou bloqué par un serveur régulier/switch. */
static int minipc_free_subslot(const Infra *inf, const char *rack, int slot) {
    for (int i = 0; i < inf->nswitches; i++) {
        if (strcmp(inf->switches[i].rack, rack) != 0) continue;
        int s = inf->switches[i].slot;
        int e = s + inf->switches[i].size_u - 1;
        if (slot >= s && slot <= e) return -1;
    }
    for (int i = 0; i < inf->nservers; i++) {
        if (strcmp(inf->servers[i].rack, rack) != 0) continue;
        if (!inf->servers[i].is_minipc) {
            int s = inf->servers[i].slot;
            int e = s + inf->servers[i].size_u - 1;
            if (slot >= s && slot <= e) return -1;
        }
    }
    int used[2] = {0, 0};
    for (int i = 0; i < inf->nservers; i++) {
        if (strcmp(inf->servers[i].rack, rack) != 0) continue;
        if (inf->servers[i].slot != slot || !inf->servers[i].is_minipc) continue;
        int ss = inf->servers[i].subslot;
        if (ss == 0 || ss == 1) used[ss] = 1;
    }
    if (!used[0]) return 0;
    if (!used[1]) return 1;
    return -1;
}

int infra_minipc_add(Infra *inf, const char *name, const char *rack,
                     int slot, int max_ram, int max_disk, const char *model_id) {
    if (inf->nservers >= MAX_SERVERS) return -1;
    if (!infra_find_rack(inf, rack))  return -2;
    if (infra_find_server(inf, name)) return -3;
    if (slot < 1)                     return -4;

    int subslot = minipc_free_subslot(inf, rack, slot);
    if (subslot < 0) return -5;

    PhysServer *s = &inf->servers[inf->nservers];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, 31);
    strncpy(s->rack, rack, 31);
    s->slot    = slot;
    s->size_u  = 1;
    s->cpu     = 1;
    s->ram_mb  = 2048;
    s->disk_gb = 100;
    int mport = PHYS_SSH_BASE;
    for (int i = 0; i < inf->nservers; i++)
        if (inf->servers[i].port >= mport) mport = inf->servers[i].port + 1;

    s->powered = 0;
    s->port    = mport;
    s->is_minipc      = 1;
    s->subslot        = subslot;
    s->has_ipmi       = 0;
    s->max_ram_slots  = max_ram  > 0 && max_ram  < HW_RAM_SLOTS  ? max_ram  : 2;
    s->max_disk_slots = max_disk > 0 && max_disk < HW_DISK_SLOTS ? max_disk : 1;
    if (model_id && model_id[0]) strncpy(s->model_id, model_id, 31);
    inf->nservers++;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * Réseaux associés à un serveur via ses câbles
 * ═══════════════════════════════════════════════════════════════ */

int infra_server_nets(const Infra *inf, const char *server,
                      const char *nets_out[], int max_nets) {
    int n = 0;
    for (int i = 0; i < inf->ncables && n < max_nets; i++)
        if (strcmp(inf->cables[i].server, server) == 0)
            nets_out[n++] = inf->cables[i].sw;
    return n;
}

/* ═══════════════════════════════════════════════════════════════
 * Rendu texte (sans ncurses)
 * ═══════════════════════════════════════════════════════════════ */

static void addl(char lines[][128], int *n, int max, const char *fmt, ...) {
    if (*n >= max) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lines[(*n)++], 128, fmt, ap);
    va_end(ap);
}

void infra_rack_render(const Infra *inf, const char *rack_name,
                       char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    const Rack *rack = NULL;
    for (int i = 0; i < inf->nracks; i++)
        if (strcmp(inf->racks[i].name, rack_name) == 0) { rack = &inf->racks[i]; break; }
    if (!rack) {
        addl(lines, nlines, max_lines, "Baie '%s' introuvable.", rack_name);
        return;
    }

    addl(lines, nlines, max_lines,
         "+-[ %-12s ]%s %2dU -+",
         rack->name, "-------------------------------", rack->units);

    int u = 1;
    while (u <= rack->units) {
        /* Cherche un serveur régulier au slot u */
        const PhysServer *srv = NULL;
        for (int i = 0; i < inf->nservers; i++)
            if (strcmp(inf->servers[i].rack, rack_name) == 0 &&
                inf->servers[i].slot == u &&
                !inf->servers[i].is_minipc) { srv = &inf->servers[i]; break; }

        const PhysSwitch *sw = NULL;
        if (!srv)
            for (int i = 0; i < inf->nswitches; i++)
                if (strcmp(inf->switches[i].rack, rack_name) == 0 &&
                    inf->switches[i].slot == u) { sw = &inf->switches[i]; break; }

        /* Cherche des mini PCs au slot u */
        const PhysServer *mini[2] = {NULL, NULL};
        if (!srv && !sw) {
            for (int i = 0; i < inf->nservers; i++) {
                if (strcmp(inf->servers[i].rack, rack_name) != 0) continue;
                if (inf->servers[i].slot != u || !inf->servers[i].is_minipc) continue;
                int ss = inf->servers[i].subslot;
                if (ss == 0 || ss == 1) mini[ss] = &inf->servers[i];
            }
        }

        if (srv) {
            addl(lines, nlines, max_lines,
                 "| %2dU [SRV] %-10s %2dc %5dMB %4dGB [%s] |",
                 u, srv->name, srv->cpu, srv->ram_mb, srv->disk_gb,
                 srv->powered ? "ON " : "OFF");
            u += srv->size_u;
        } else if (sw) {
            addl(lines, nlines, max_lines,
                 "| %2dU [SW ] %-10s %3dp                [%s] |",
                 u, sw->name, sw->ports,
                 sw->powered ? "ON " : "OFF");
            u += sw->size_u;
        } else if (mini[0] || mini[1]) {
            const char *n0 = mini[0] ? mini[0]->name : "(libre)";
            const char *n1 = mini[1] ? mini[1]->name : "(libre)";
            const char *s0 = mini[0] ? (mini[0]->powered ? "ON " : "OFF") : "---";
            const char *s1 = mini[1] ? (mini[1]->powered ? "ON " : "OFF") : "---";
            addl(lines, nlines, max_lines,
                 "| %2dU [MNI] %-9s[%s]  %-9s[%s] |",
                 u, n0, s0, n1, s1);
            u++;
        } else {
            addl(lines, nlines, max_lines,
                 "| %2dU  %-38s |", u, "");
            u++;
        }
    }
    addl(lines, nlines, max_lines,
         "+------------------------------------------+");
}

void infra_list_racks(const Infra *inf,
                      char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    for (int i = 0; i < inf->nracks; i++)
        addl(lines, nlines, max_lines,
             "  %-12s  %dU", inf->racks[i].name, inf->racks[i].units);
    if (*nlines == 0)
        addl(lines, nlines, max_lines, "  (aucune baie)");
}

void infra_list_servers(const Infra *inf, const char *rack,
                        char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    for (int i = 0; i < inf->nservers; i++) {
        if (rack && strcmp(inf->servers[i].rack, rack) != 0) continue;
        addl(lines, nlines, max_lines,
             "  %-12s  rack=%-8s slot=%2d %dU  %dc %dMB %dGB  [%s]",
             inf->servers[i].name, inf->servers[i].rack,
             inf->servers[i].slot, inf->servers[i].size_u,
             inf->servers[i].cpu, inf->servers[i].ram_mb, inf->servers[i].disk_gb,
             inf->servers[i].powered ? "ON" : "OFF");
    }
    if (*nlines == 0)
        addl(lines, nlines, max_lines, "  (aucun serveur)");
}

void infra_list_switches(const Infra *inf, const char *rack,
                         char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    for (int i = 0; i < inf->nswitches; i++) {
        if (rack && strcmp(inf->switches[i].rack, rack) != 0) continue;
        addl(lines, nlines, max_lines,
             "  %-12s  rack=%-8s slot=%2d %dU  %dp  [%s]",
             inf->switches[i].name, inf->switches[i].rack,
             inf->switches[i].slot, inf->switches[i].size_u,
             inf->switches[i].ports,
             inf->switches[i].powered ? "ON" : "OFF");
    }
    if (*nlines == 0)
        addl(lines, nlines, max_lines, "  (aucun switch)");
}

void infra_server_show(const Infra *inf, const char *name,
                       char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    const PhysServer *srv = NULL;
    for (int i = 0; i < inf->nservers; i++)
        if (strcmp(inf->servers[i].name, name) == 0) { srv = &inf->servers[i]; break; }
    if (!srv) {
        addl(lines, nlines, max_lines, "Serveur '%s' introuvable.", name);
        return;
    }

    addl(lines, nlines, max_lines, "Serveur : %s%s",
         srv->name, srv->has_ipmi ? "" : "  [pas d'IPMI]");
    if (srv->model_id[0])
        addl(lines, nlines, max_lines, "  Modele : %s", srv->model_id);
    if (srv->is_minipc)
        addl(lines, nlines, max_lines, "  Baie   : %-12s slot %d  (mini PC subslot %d)",
             srv->rack, srv->slot, srv->subslot);
    else
        addl(lines, nlines, max_lines, "  Baie   : %-12s slot %d  (%dU)",
             srv->rack, srv->slot, srv->size_u);
    addl(lines, nlines, max_lines, "  CPU    : %d coeur%s",
         srv->cpu, srv->cpu > 1 ? "s" : "");
    addl(lines, nlines, max_lines, "  RAM    : %d MB", srv->ram_mb);
    addl(lines, nlines, max_lines, "  Disque : %d GB", srv->disk_gb);

    if (srv->powered)
        addl(lines, nlines, max_lines, "  Etat   : ON  (SSH :%-4d)", srv->port);
    else
        addl(lines, nlines, max_lines, "  Etat   : OFF");

    int ncables = 0;
    for (int i = 0; i < inf->ncables; i++)
        if (strcmp(inf->cables[i].server, name) == 0) ncables++;

    if (ncables == 0) {
        addl(lines, nlines, max_lines, "  Cables : (aucun)");
    } else {
        addl(lines, nlines, max_lines, "  Cables :");
        for (int i = 0; i < inf->ncables; i++)
            if (strcmp(inf->cables[i].server, name) == 0)
                addl(lines, nlines, max_lines, "    %-6s ->  %s:port%d",
                     inf->cables[i].nic, inf->cables[i].sw, inf->cables[i].port);
    }
}

void infra_switch_show(const Infra *inf, const char *name,
                       char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    const PhysSwitch *sw = NULL;
    for (int i = 0; i < inf->nswitches; i++)
        if (strcmp(inf->switches[i].name, name) == 0) { sw = &inf->switches[i]; break; }
    if (!sw) {
        addl(lines, nlines, max_lines, "Switch '%s' introuvable.", name);
        return;
    }

    addl(lines, nlines, max_lines, "Switch : %s", sw->name);
    addl(lines, nlines, max_lines, "  Baie   : %-12s slot %d  (%dU)",
         sw->rack, sw->slot, sw->size_u);
    addl(lines, nlines, max_lines, "  Ports  : %d", sw->ports);
    addl(lines, nlines, max_lines, "  Etat   : %s", sw->powered ? "ON" : "OFF");

    int ncables = 0;
    for (int i = 0; i < inf->ncables; i++)
        if (strcmp(inf->cables[i].sw, name) == 0) ncables++;

    if (ncables == 0) {
        addl(lines, nlines, max_lines, "  Connexions : (aucune)");
    } else {
        addl(lines, nlines, max_lines, "  Connexions :");
        for (int i = 0; i < inf->ncables; i++)
            if (strcmp(inf->cables[i].sw, name) == 0)
                addl(lines, nlines, max_lines, "    port%-3d  <-  %s:%s",
                     inf->cables[i].port, inf->cables[i].server, inf->cables[i].nic);
    }
}

void infra_list_cables(const Infra *inf,
                       char lines[][128], int *nlines, int max_lines) {
    *nlines = 0;
    for (int i = 0; i < inf->ncables; i++)
        addl(lines, nlines, max_lines,
             "  %s:%s  ->  %s:port%d",
             inf->cables[i].server, inf->cables[i].nic,
             inf->cables[i].sw,     inf->cables[i].port);
    if (*nlines == 0)
        addl(lines, nlines, max_lines, "  (aucun cable)");
}

/* ═══════════════════════════════════════════════════════════════
 * Suppression
 * ═══════════════════════════════════════════════════════════════ */

int infra_rack_delete(Infra *inf, const char *name) {
    /* Interdit si des équipements occupent encore la baie */
    for (int i = 0; i < inf->nservers; i++)
        if (strcmp(inf->servers[i].rack, name) == 0) return -2;
    for (int i = 0; i < inf->nswitches; i++)
        if (strcmp(inf->switches[i].rack, name) == 0) return -2;

    int idx = -1;
    for (int i = 0; i < inf->nracks; i++)
        if (strcmp(inf->racks[i].name, name) == 0) { idx = i; break; }
    if (idx < 0) return -1;

    memmove(&inf->racks[idx], &inf->racks[idx + 1],
            (size_t)(inf->nracks - idx - 1) * sizeof(Rack));
    inf->nracks--;
    return 0;
}

int infra_server_delete(Infra *inf, const char *name) {
    int idx = -1;
    for (int i = 0; i < inf->nservers; i++)
        if (strcmp(inf->servers[i].name, name) == 0) { idx = i; break; }
    if (idx < 0)                    return -1;
    if (inf->servers[idx].powered)  return -2;   /* doit être éteint */

    /* Retire tous les câbles partant de ce serveur */
    for (int i = 0; i < inf->ncables; ) {
        if (strcmp(inf->cables[i].server, name) == 0) {
            memmove(&inf->cables[i], &inf->cables[i + 1],
                    (size_t)(inf->ncables - i - 1) * sizeof(Cable));
            inf->ncables--;
        } else i++;
    }

    memmove(&inf->servers[idx], &inf->servers[idx + 1],
            (size_t)(inf->nservers - idx - 1) * sizeof(PhysServer));
    inf->nservers--;
    return 0;
}

int infra_switch_delete(Infra *inf, const char *name) {
    int idx = -1;
    for (int i = 0; i < inf->nswitches; i++)
        if (strcmp(inf->switches[i].name, name) == 0) { idx = i; break; }
    if (idx < 0)                     return -1;
    if (inf->switches[idx].powered)  return -2;   /* doit être éteint */

    /* Retire tous les câbles vers ce switch */
    for (int i = 0; i < inf->ncables; ) {
        if (strcmp(inf->cables[i].sw, name) == 0) {
            memmove(&inf->cables[i], &inf->cables[i + 1],
                    (size_t)(inf->ncables - i - 1) * sizeof(Cable));
            inf->ncables--;
        } else i++;
    }

    memmove(&inf->switches[idx], &inf->switches[idx + 1],
            (size_t)(inf->nswitches - idx - 1) * sizeof(PhysSwitch));
    inf->nswitches--;
    return 0;
}

int infra_cable_disconnect(Infra *inf, const char *server, const char *nic) {
    int idx = -1;
    for (int i = 0; i < inf->ncables; i++)
        if (strcmp(inf->cables[i].server, server) == 0 &&
            strcmp(inf->cables[i].nic,    nic)    == 0) { idx = i; break; }
    if (idx < 0) return -1;

    memmove(&inf->cables[idx], &inf->cables[idx + 1],
            (size_t)(inf->ncables - idx - 1) * sizeof(Cable));
    inf->ncables--;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * Persistance
 * ═══════════════════════════════════════════════════════════════ */

void infra_save(const Infra *inf, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < inf->nracks; i++)
        fprintf(f, "RACK:%s:%d\n",
                inf->racks[i].name, inf->racks[i].units);
    for (int i = 0; i < inf->nservers; i++) {
        const PhysServer *s = &inf->servers[i];
        fprintf(f, "SRV:%s:%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%s\n",
                s->name, s->rack, s->slot, s->size_u,
                s->cpu, s->ram_mb, s->disk_gb, s->powered, s->port,
                s->has_ipmi, s->is_minipc, s->subslot,
                s->max_ram_slots, s->max_disk_slots,
                s->model_id[0] ? s->model_id : "-");
    }
    for (int i = 0; i < inf->nswitches; i++)
        fprintf(f, "SW:%s:%s:%d:%d:%d:%d\n",
                inf->switches[i].name, inf->switches[i].rack,
                inf->switches[i].slot,  inf->switches[i].size_u,
                inf->switches[i].ports, inf->switches[i].powered);
    for (int i = 0; i < inf->ncables; i++)
        fprintf(f, "CBL:%s:%s:%s:%d\n",
                inf->cables[i].server, inf->cables[i].nic,
                inf->cables[i].sw,     inf->cables[i].port);
    /* Composants hardware */
    for (int i = 0; i < inf->nservers; i++) {
        const PhysServer *s = &inf->servers[i];
        if (s->hw_cpu[0])
            fprintf(f, "HW:%s:cpu:%s\n", s->name, s->hw_cpu);
        for (int j = 0; j < HW_RAM_SLOTS; j++)
            if (s->hw_ram[j][0])
                fprintf(f, "HW:%s:ram:%d:%s\n", s->name, j, s->hw_ram[j]);
        for (int j = 0; j < HW_DISK_SLOTS; j++)
            if (s->hw_disk[j][0])
                fprintf(f, "HW:%s:disk:%d:%s\n", s->name, j, s->hw_disk[j]);
    }
    fclose(f);
}

void infra_load(Infra *inf, const char *path) {
    memset(inf, 0, sizeof(*inf));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "RACK:", 5) == 0) {
            char name[32]; int units;
            if (sscanf(line + 5, "%31[^:]:%d", name, &units) == 2)
                infra_rack_create(inf, name, units);

        } else if (strncmp(line, "SRV:", 4) == 0) {
            char name[32], rack[32], model_id[32] = "";
            int slot, size_u, cpu, ram_mb, disk_gb, powered, port;
            int has_ipmi = 1, is_minipc = 0, subslot = 0;
            int max_ram = HW_RAM_SLOTS, max_disk = HW_DISK_SLOTS;
            int n = sscanf(line + 4,
                "%31[^:]:%31[^:]:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%31[^\n]",
                name, rack, &slot, &size_u, &cpu, &ram_mb, &disk_gb,
                &powered, &port,
                &has_ipmi, &is_minipc, &subslot, &max_ram, &max_disk, model_id);
            if (n >= 9) {
                if (is_minipc) {
                    infra_minipc_add(inf, name, rack, slot, max_ram, max_disk,
                                     (n >= 15 && model_id[0] && model_id[0] != '-') ? model_id : "");
                    PhysServer *s = infra_find_server(inf, name);
                    if (s) {
                        s->powered = powered; s->port = port; s->subslot = subslot;
                        s->cpu = cpu; s->ram_mb = ram_mb; s->disk_gb = disk_gb;
                    }
                } else {
                    infra_server_add(inf, name, rack, slot, size_u, cpu, ram_mb, disk_gb);
                    PhysServer *s = infra_find_server(inf, name);
                    if (s) {
                        s->powered = powered; s->port = port;
                        if (n >= 10) s->has_ipmi  = has_ipmi;
                        if (n >= 13) s->max_ram_slots  = max_ram;
                        if (n >= 14) s->max_disk_slots = max_disk;
                        if (n >= 15 && model_id[0] && model_id[0] != '-')
                            strncpy(s->model_id, model_id, 31);
                    }
                }
            }

        } else if (strncmp(line, "SW:", 3) == 0) {
            char name[32], rack[32];
            int slot, size_u, ports, powered;
            if (sscanf(line + 3, "%31[^:]:%31[^:]:%d:%d:%d:%d",
                    name, rack, &slot, &size_u, &ports, &powered) == 6) {
                infra_switch_add(inf, name, rack, slot, size_u, ports);
                PhysSwitch *sw = infra_find_switch(inf, name);
                if (sw) sw->powered = powered;
            }

        } else if (strncmp(line, "CBL:", 4) == 0) {
            char server[32], nic[8], sw[32]; int port;
            if (sscanf(line + 4, "%31[^:]:%7[^:]:%31[^:]:%d",
                    server, nic, sw, &port) == 4)
                infra_cable_connect(inf, server, nic, sw, port);

        } else if (strncmp(line, "HW:", 3) == 0) {
            char server[32], kind[8], extra[64] = "";
            if (sscanf(line + 3, "%31[^:]:%7[^:]:%63[^\n]",
                       server, kind, extra) < 2) continue;
            PhysServer *s = infra_find_server(inf, server);
            if (!s) continue;
            if (strcmp(kind, "cpu") == 0) {
                strncpy(s->hw_cpu, extra, 31);
            } else if (strcmp(kind, "ram") == 0) {
                int slot = -1; char comp[32] = "";
                if (sscanf(extra, "%d:%31[^\n]", &slot, comp) == 2 &&
                    slot >= 0 && slot < HW_RAM_SLOTS)
                    strncpy(s->hw_ram[slot], comp, 31);
            } else if (strcmp(kind, "disk") == 0) {
                int slot = -1; char comp[32] = "";
                if (sscanf(extra, "%d:%31[^\n]", &slot, comp) == 2 &&
                    slot >= 0 && slot < HW_DISK_SLOTS)
                    strncpy(s->hw_disk[slot], comp, 31);
            }
        }
    }
    fclose(f);
}
