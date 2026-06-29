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
    if (!slot_free(inf, rack, slot, size_u)) return -4;

    PhysServer *s = &inf->servers[inf->nservers];
    strncpy(s->name, name, 31);
    strncpy(s->rack, rack, 31);
    s->slot    = slot;
    s->size_u  = size_u;
    s->cpu     = cpu     > 0 ? cpu     : 1;
    s->ram_mb  = ram_mb  > 0 ? ram_mb  : 2048;
    s->disk_gb = disk_gb > 0 ? disk_gb : 100;
    s->powered = 0;
    s->port    = PHYS_SSH_BASE + inf->nservers;
    inf->nservers++;
    return 0;
}

int infra_switch_add(Infra *inf, const char *name, const char *rack,
                     int slot, int size_u, int ports) {
    if (inf->nswitches >= MAX_SWITCHES)      return -1;
    if (!infra_find_rack(inf, rack))         return -2;
    if (infra_find_switch(inf, name))        return -3;
    if (size_u < 1) size_u = 1;
    if (!slot_free(inf, rack, slot, size_u)) return -4;

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
    if (!infra_find_switch(inf, sw))     return -2;
    for (int i = 0; i < inf->ncables; i++)
        if (strcmp(inf->cables[i].server, server) == 0 &&
            strcmp(inf->cables[i].nic,    nic)    == 0) return -3;

    Cable *c = &inf->cables[inf->ncables++];
    strncpy(c->server, server, 31);
    strncpy(c->nic,    nic,     7);
    strncpy(c->sw,     sw,     31);
    c->port = port;
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
        const PhysServer *srv = NULL;
        for (int i = 0; i < inf->nservers; i++)
            if (strcmp(inf->servers[i].rack, rack_name) == 0 &&
                inf->servers[i].slot == u) { srv = &inf->servers[i]; break; }

        const PhysSwitch *sw = NULL;
        if (!srv)
            for (int i = 0; i < inf->nswitches; i++)
                if (strcmp(inf->switches[i].rack, rack_name) == 0 &&
                    inf->switches[i].slot == u) { sw = &inf->switches[i]; break; }

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
 * Persistance
 * ═══════════════════════════════════════════════════════════════ */

void infra_save(const Infra *inf, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < inf->nracks; i++)
        fprintf(f, "RACK:%s:%d\n",
                inf->racks[i].name, inf->racks[i].units);
    for (int i = 0; i < inf->nservers; i++)
        fprintf(f, "SRV:%s:%s:%d:%d:%d:%d:%d:%d:%d\n",
                inf->servers[i].name, inf->servers[i].rack,
                inf->servers[i].slot,  inf->servers[i].size_u,
                inf->servers[i].cpu,   inf->servers[i].ram_mb,
                inf->servers[i].disk_gb, inf->servers[i].powered,
                inf->servers[i].port);
    for (int i = 0; i < inf->nswitches; i++)
        fprintf(f, "SW:%s:%s:%d:%d:%d:%d\n",
                inf->switches[i].name, inf->switches[i].rack,
                inf->switches[i].slot,  inf->switches[i].size_u,
                inf->switches[i].ports, inf->switches[i].powered);
    for (int i = 0; i < inf->ncables; i++)
        fprintf(f, "CBL:%s:%s:%s:%d\n",
                inf->cables[i].server, inf->cables[i].nic,
                inf->cables[i].sw,     inf->cables[i].port);
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
            char name[32], rack[32];
            int slot, size_u, cpu, ram_mb, disk_gb, powered, port;
            if (sscanf(line + 4, "%31[^:]:%31[^:]:%d:%d:%d:%d:%d:%d:%d",
                    name, rack, &slot, &size_u, &cpu,
                    &ram_mb, &disk_gb, &powered, &port) == 9) {
                infra_server_add(inf, name, rack, slot, size_u, cpu, ram_mb, disk_gb);
                PhysServer *s = infra_find_server(inf, name);
                if (s) { s->powered = powered; s->port = port; }
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
        }
    }
    fclose(f);
}
