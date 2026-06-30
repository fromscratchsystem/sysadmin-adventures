#include "hardware.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
 * Tableaux dynamiques des catalogues
 * ═══════════════════════════════════════════════════════════════ */

#define MAX_HW_CATALOG   128
#define MAX_SRV_CATALOG   64

HWComp     hw_catalog[MAX_HW_CATALOG];
int        hw_catalog_size = 0;

ServerModel server_catalog[MAX_SRV_CATALOG];
int         server_catalog_size = 0;

/* ═══════════════════════════════════════════════════════════════
 * Valeurs par défaut (utilisées si les fichiers sont absents)
 * ═══════════════════════════════════════════════════════════════ */

static const HWComp hw_defaults[] = {
/* id               label                         type    cores ghz10 mem_gen   mb      gb   dtype   iops */
{"atom-d510",    "Intel Atom D510",            HW_CPU,    2,  16, "DDR3",    0,    0,  "",       0},
{"xeon-e3-1220", "Intel Xeon E3-1220 v3",     HW_CPU,    4,  31, "DDR3",    0,    0,  "",       0},
{"xeon-e5-2670", "Intel Xeon E5-2670",         HW_CPU,    8,  26, "DDR3",    0,    0,  "",       0},
{"epyc-7302",    "AMD EPYC 7302",              HW_CPU,   16,  30, "DDR4",    0,    0,  "",       0},
{"epyc-7543",    "AMD EPYC 7543",              HW_CPU,   32,  28, "DDR4",    0,    0,  "",       0},
{"xeon-w3-2435", "Intel Xeon W3-2435",         HW_CPU,    8,  31, "DDR5",    0,    0,  "",       0},
{"ddr3-4gb",     "4 GB DDR3 ECC",              HW_RAM,    0,   0, "DDR3", 4096,    0,  "",       0},
{"ddr3-16gb",    "16 GB DDR3 ECC",             HW_RAM,    0,   0, "DDR3",16384,    0,  "",       0},
{"ddr4-16gb",    "16 GB DDR4 ECC",             HW_RAM,    0,   0, "DDR4",16384,    0,  "",       0},
{"ddr4-32gb",    "32 GB DDR4 ECC",             HW_RAM,    0,   0, "DDR4",32768,    0,  "",       0},
{"ddr5-32gb",    "32 GB DDR5 ECC",             HW_RAM,    0,   0, "DDR5",32768,    0,  "",       0},
{"ddr5-64gb",    "64 GB DDR5 ECC",             HW_RAM,    0,   0, "DDR5",65536,    0,  "",       0},
{"hdd-1tb",      "1 TB SATA HDD",              HW_DISK,   0,   0, "",        0, 1000,  "HDD",   150},
{"hdd-4tb",      "4 TB SATA HDD",              HW_DISK,   0,   0, "",        0, 4000,  "HDD",   150},
{"ssd-500gb",    "500 GB SATA SSD",            HW_DISK,   0,   0, "",        0,  500,  "SSD",  5000},
{"ssd-2tb",      "2 TB SATA SSD",              HW_DISK,   0,   0, "",        0, 2000,  "SSD",  8000},
{"nvme-512gb",   "512 GB NVMe M.2",            HW_DISK,   0,   0, "",        0,  512,  "NVMe",20000},
{"nvme-2tb",     "2 TB NVMe U.2",              HW_DISK,   0,   0, "",        0, 2000,  "NVMe",60000},
};
static const int hw_defaults_size =
    (int)(sizeof(hw_defaults) / sizeof(hw_defaults[0]));

static const ServerModel srv_defaults[] = {
{"generic-1u",   "Serveur rack 1U generique",      1,  1,  4,  4},
{"generic-2u",   "Serveur rack 2U generique",      2,  1,  4,  4},
{"dell-r240",    "Dell PowerEdge R240 (1U)",        1,  1,  4,  2},
{"hp-dl360",     "HP ProLiant DL360 Gen9 (1U)",    1,  1,  4,  4},
{"dell-r740",    "Dell PowerEdge R740 (2U)",        2,  1,  4,  4},
{"hp-dl380",     "HP ProLiant DL380 Gen10 (2U)",   2,  1,  4,  4},
{"nuc-i5",       "Intel NUC i5 (occasion)",         0,  0,  2,  1},
{"hp-prodesk",   "HP ProDesk Mini G5 (occasion)",  0,  0,  2,  2},
};
static const int srv_defaults_size =
    (int)(sizeof(srv_defaults) / sizeof(srv_defaults[0]));

/* ═══════════════════════════════════════════════════════════════
 * Recherche
 * ═══════════════════════════════════════════════════════════════ */

const HWComp *hw_find(const char *id) {
    for (int i = 0; i < hw_catalog_size; i++)
        if (strcmp(hw_catalog[i].id, id) == 0) return &hw_catalog[i];
    return NULL;
}

const ServerModel *srv_model_find(const char *id) {
    for (int i = 0; i < server_catalog_size; i++)
        if (strcmp(server_catalog[i].id, id) == 0) return &server_catalog[i];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Chargement depuis fichier
 * ═══════════════════════════════════════════════════════════════ */

/* Découpe 'line' sur '|', supprime les espaces autour de chaque champ.
 * Retourne le nombre de champs trouvés. */
static int split_pipe(const char *line, char fields[][64], int max_f) {
    int n = 0;
    const char *p = line;
    while (n < max_f) {
        while (*p == ' ' || *p == '\t') p++;
        const char *end = p;
        while (*end && *end != '|' && *end != '\n' && *end != '\r') end++;
        const char *rend = end;
        while (rend > p && (*(rend-1) == ' ' || *(rend-1) == '\t')) rend--;
        int len = (int)(rend - p);
        if (len > 63) len = 63;
        strncpy(fields[n], p, (size_t)len);
        fields[n][len] = '\0';
        n++;
        if (!*end || *end == '\n' || *end == '\r') break;
        p = end + 1;
    }
    return n;
}

int hw_catalog_load(const char *path) {
    FILE *f = NULL;
    if (path) f = fopen(path, "r");

    if (!f) {
        if (path)
            fprintf(stderr, "[hw] '%s' introuvable, catalogue par defaut.\n", path);
        memcpy(hw_catalog, hw_defaults,
               (size_t)hw_defaults_size * sizeof(HWComp));
        hw_catalog_size = hw_defaults_size;
        return 0;
    }

    hw_catalog_size = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && hw_catalog_size < MAX_HW_CATALOG) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        char fields[10][64];
        int nf = split_pipe(p, fields, 10);
        if (nf < 3) continue;

        HWType type;
        if      (strcmp(fields[0], "cpu")  == 0) type = HW_CPU;
        else if (strcmp(fields[0], "ram")  == 0) type = HW_RAM;
        else if (strcmp(fields[0], "disk") == 0) type = HW_DISK;
        else continue;

        HWComp *c = &hw_catalog[hw_catalog_size++];
        memset(c, 0, sizeof(*c));
        c->type = type;
        strncpy(c->id,    fields[1], 31);
        strncpy(c->label, fields[2], 63);
        if (nf > 3) c->cores  = atoi(fields[3]);
        if (nf > 4) c->ghz10  = atoi(fields[4]);
        if (nf > 5) strncpy(c->mem_gen,   fields[5], 7);
        if (nf > 6) c->size_mb = atoi(fields[6]);
        if (nf > 7) c->size_gb = atoi(fields[7]);
        if (nf > 8) strncpy(c->disk_type, fields[8], 7);
        if (nf > 9) c->iops   = atoi(fields[9]);
    }
    fclose(f);
    return hw_catalog_size;
}

int server_catalog_load(const char *path) {
    FILE *f = NULL;
    if (path) f = fopen(path, "r");

    if (!f) {
        if (path)
            fprintf(stderr, "[hw] '%s' introuvable, modeles par defaut.\n", path);
        memcpy(server_catalog, (void *)srv_defaults,
               (size_t)srv_defaults_size * sizeof(ServerModel));
        server_catalog_size = srv_defaults_size;
        return 0;
    }

    server_catalog_size = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && server_catalog_size < MAX_SRV_CATALOG) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        char fields[6][64];
        int nf = split_pipe(p, fields, 6);
        if (nf < 6) continue;

        ServerModel *m = &server_catalog[server_catalog_size++];
        memset(m, 0, sizeof(*m));
        strncpy(m->id,    fields[0], 31);
        strncpy(m->label, fields[1], 63);
        m->size_u         = atoi(fields[2]);
        m->has_ipmi       = atoi(fields[3]);
        m->max_ram_slots  = atoi(fields[4]);
        m->max_disk_slots = atoi(fields[5]);
    }
    fclose(f);
    return server_catalog_size;
}

/* ═══════════════════════════════════════════════════════════════
 * Recalcul des specs depuis les composants installés
 * ═══════════════════════════════════════════════════════════════ */

void hw_recompute(PhysServer *srv) {
    if (srv->hw_cpu[0]) {
        const HWComp *c = hw_find(srv->hw_cpu);
        if (c) srv->cpu = c->cores;
    }
    int total_ram = 0;
    for (int i = 0; i < HW_RAM_SLOTS; i++) {
        if (!srv->hw_ram[i][0]) continue;
        const HWComp *c = hw_find(srv->hw_ram[i]);
        if (c) total_ram += c->size_mb;
    }
    if (total_ram > 0) srv->ram_mb = total_ram;

    int total_disk = 0;
    for (int i = 0; i < HW_DISK_SLOTS; i++) {
        if (!srv->hw_disk[i][0]) continue;
        const HWComp *c = hw_find(srv->hw_disk[i]);
        if (c) total_disk += c->size_gb;
    }
    if (total_disk > 0) srv->disk_gb = total_disk;
}

void hw_recompute_all(Infra *inf) {
    for (int i = 0; i < inf->nservers; i++)
        hw_recompute(&inf->servers[i]);
}

/* ═══════════════════════════════════════════════════════════════
 * Install / Retrait
 * ═══════════════════════════════════════════════════════════════ */

int hw_install(Infra *inf, const char *server, const char *comp_id) {
    const HWComp *comp = hw_find(comp_id);
    if (!comp) return -1;

    PhysServer *srv = infra_find_server(inf, server);
    if (!srv) return -2;
    if (srv->powered) return -4;

    int max_r = (srv->max_ram_slots  > 0 && srv->max_ram_slots  < HW_RAM_SLOTS)
                ? srv->max_ram_slots  : HW_RAM_SLOTS;
    int max_d = (srv->max_disk_slots > 0 && srv->max_disk_slots < HW_DISK_SLOTS)
                ? srv->max_disk_slots : HW_DISK_SLOTS;

    if (comp->type == HW_CPU) {
        if (srv->hw_cpu[0]) return -3;
        for (int i = 0; i < HW_RAM_SLOTS; i++) {
            if (!srv->hw_ram[i][0]) continue;
            const HWComp *ram = hw_find(srv->hw_ram[i]);
            if (ram && comp->mem_gen[0] && ram->mem_gen[0] &&
                strcmp(comp->mem_gen, ram->mem_gen) != 0)
                return -5;
        }
        strncpy(srv->hw_cpu, comp_id, 31);

    } else if (comp->type == HW_RAM) {
        if (srv->hw_cpu[0]) {
            const HWComp *cpu = hw_find(srv->hw_cpu);
            if (cpu && cpu->mem_gen[0] && comp->mem_gen[0] &&
                strcmp(cpu->mem_gen, comp->mem_gen) != 0)
                return -5;
        }
        int slot = -1;
        for (int i = 0; i < max_r; i++)
            if (!srv->hw_ram[i][0]) { slot = i; break; }
        if (slot < 0) return -3;
        strncpy(srv->hw_ram[slot], comp_id, 31);

    } else {
        int slot = -1;
        for (int i = 0; i < max_d; i++)
            if (!srv->hw_disk[i][0]) { slot = i; break; }
        if (slot < 0) return -3;
        strncpy(srv->hw_disk[slot], comp_id, 31);
    }

    hw_recompute(srv);
    return 0;
}

int hw_remove(Infra *inf, const char *server, const char *comp_id) {
    const HWComp *comp = hw_find(comp_id);
    if (!comp) return -1;

    PhysServer *srv = infra_find_server(inf, server);
    if (!srv) return -2;
    if (srv->powered) return -4;

    if (comp->type == HW_CPU) {
        if (strcmp(srv->hw_cpu, comp_id) != 0) return -1;
        srv->hw_cpu[0] = '\0';
    } else if (comp->type == HW_RAM) {
        int found = 0;
        for (int i = 0; i < HW_RAM_SLOTS; i++) {
            if (strcmp(srv->hw_ram[i], comp_id) == 0) {
                srv->hw_ram[i][0] = '\0'; found = 1; break;
            }
        }
        if (!found) return -1;
    } else {
        int found = 0;
        for (int i = 0; i < HW_DISK_SLOTS; i++) {
            if (strcmp(srv->hw_disk[i], comp_id) == 0) {
                srv->hw_disk[i][0] = '\0'; found = 1; break;
            }
        }
        if (!found) return -1;
    }

    hw_recompute(srv);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * Affichage catalogue
 * ═══════════════════════════════════════════════════════════════ */

static void addl(char lines[][128], int *n, int max, const char *fmt, ...) {
    if (*n >= max) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(lines[(*n)++], 128, fmt, ap);
    va_end(ap);
}

void hw_list_catalog(HWType type, char lines[][128], int *nlines, int max) {
    *nlines = 0;
    const char *headers[] = {"── CPU ──", "── RAM ──", "── Disque ──"};
    addl(lines, nlines, max, "  %s", headers[(int)type]);

    for (int i = 0; i < hw_catalog_size; i++) {
        const HWComp *c = &hw_catalog[i];
        if (c->type != type) continue;
        if (type == HW_CPU) {
            addl(lines, nlines, max, "  %-14s  %-26s  %dc @ %d.%dGHz  [%s]",
                 c->id, c->label, c->cores,
                 c->ghz10 / 10, c->ghz10 % 10, c->mem_gen);
        } else if (type == HW_RAM) {
            addl(lines, nlines, max, "  %-14s  %-26s  %5d MB  [%s]",
                 c->id, c->label, c->size_mb, c->mem_gen);
        } else {
            addl(lines, nlines, max, "  %-14s  %-26s  %4d GB  %-4s  %d IOPS",
                 c->id, c->label, c->size_gb, c->disk_type, c->iops);
        }
    }
    if (*nlines == 1)
        addl(lines, nlines, max, "  (aucun composant de ce type)");
}

void hw_list_server_models(char lines[][128], int *nlines, int max) {
    *nlines = 0;
    addl(lines, nlines, max, "  ── Modeles de serveurs ──");
    for (int i = 0; i < server_catalog_size; i++) {
        const ServerModel *m = &server_catalog[i];
        const char *form = (m->size_u == 0) ? "MNI"
                         : (m->size_u == 1)  ? " 1U" : " 2U";
        addl(lines, nlines, max, "  %-14s  %-34s  %s  IPMI:%s  RAM:%d  DSK:%d",
             m->id, m->label, form,
             m->has_ipmi ? "O" : "N",
             m->max_ram_slots, m->max_disk_slots);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Affichage métriques d'un serveur
 * ═══════════════════════════════════════════════════════════════ */

static void star_str(int n, char out[7]) {
    out[0] = '[';
    for (int i = 0; i < 4; i++) out[1+i] = (i < n) ? '*' : '.';
    out[5] = ']'; out[6] = '\0';
}

static int cpu_stars(float s) { return s<5?1:s<15?2:s<30?3:4; }
static int mem_stars(int mb)  { int g=mb/1024; return g<8?1:g<64?2:g<256?3:4; }
static int disk_stars(int io) { return io<=200?1:io<=10000?2:io<=25000?3:4; }

void hw_show_server(const PhysServer *srv, char lines[][128], int *nlines, int max) {
    *nlines = 0;

    int has_hw = (srv->hw_cpu[0] != '\0');
    for (int i = 0; i < HW_RAM_SLOTS  && !has_hw; i++) if (srv->hw_ram [i][0]) has_hw = 1;
    for (int i = 0; i < HW_DISK_SLOTS && !has_hw; i++) if (srv->hw_disk[i][0]) has_hw = 1;

    addl(lines, nlines, max, "Hardware : %s%s", srv->name,
         srv->has_ipmi ? "" : "  [pas d'IPMI]");
    if (srv->model_id[0]) {
        const ServerModel *m = srv_model_find(srv->model_id);
        addl(lines, nlines, max, "  Modele  : %s", m ? m->label : srv->model_id);
    }
    addl(lines, nlines, max, "--------------------------------------------");

    if (srv->hw_cpu[0]) {
        const HWComp *c = hw_find(srv->hw_cpu);
        if (c) addl(lines, nlines, max, "  CPU    : %s  (%dc @ %d.%dGHz  %s)",
                    c->label, c->cores, c->ghz10/10, c->ghz10%10, c->mem_gen);
    } else {
        addl(lines, nlines, max, "  CPU    : (non installe)");
    }

    int max_r = (srv->max_ram_slots > 0 && srv->max_ram_slots < HW_RAM_SLOTS)
                ? srv->max_ram_slots : HW_RAM_SLOTS;
    for (int i = 0; i < max_r; i++) {
        if (srv->hw_ram[i][0]) {
            const HWComp *c = hw_find(srv->hw_ram[i]);
            addl(lines, nlines, max, "  RAM [%d]: %s", i, c ? c->label : srv->hw_ram[i]);
        } else {
            addl(lines, nlines, max, "  RAM [%d]: (vide)", i);
        }
    }

    int max_d = (srv->max_disk_slots > 0 && srv->max_disk_slots < HW_DISK_SLOTS)
                ? srv->max_disk_slots : HW_DISK_SLOTS;
    for (int i = 0; i < max_d; i++) {
        if (srv->hw_disk[i][0]) {
            const HWComp *c = hw_find(srv->hw_disk[i]);
            addl(lines, nlines, max, "  DSK [%d]: %s", i, c ? c->label : srv->hw_disk[i]);
        } else {
            addl(lines, nlines, max, "  DSK [%d]: (vide)", i);
        }
    }

    if (!has_hw) {
        addl(lines, nlines, max, "  Aucun composant installe.");
        addl(lines, nlines, max, "  /hardware list    pour voir le catalogue");
        addl(lines, nlines, max, "  /hardware install %s <comp-id>", srv->name);
        return;
    }

    addl(lines, nlines, max, "--------------------------------------------");

    float cpu_score = 0.0f; int cs = 0; char cst[7] = "";
    if (srv->hw_cpu[0]) {
        const HWComp *c = hw_find(srv->hw_cpu);
        if (c) cpu_score = (float)c->cores * (float)c->ghz10 / 10.0f;
        cs = cpu_stars(cpu_score); star_str(cs, cst);
        addl(lines, nlines, max, "  Puissance : %4.1f GHz-coeurs  %s", cpu_score, cst);
    } else {
        addl(lines, nlines, max, "  Puissance : N/A");
    }

    int total_ram = 0, ms = 0; char mst[7] = "";
    for (int i = 0; i < HW_RAM_SLOTS; i++) {
        if (!srv->hw_ram[i][0]) continue;
        const HWComp *c = hw_find(srv->hw_ram[i]);
        if (c) total_ram += c->size_mb;
    }
    if (total_ram > 0) {
        ms = mem_stars(total_ram); star_str(ms, mst);
        addl(lines, nlines, max, "  Memoire   : %d MB (%d GB)  %s",
             total_ram, total_ram / 1024, mst);
    } else {
        addl(lines, nlines, max, "  Memoire   : N/A");
    }

    int total_disk = 0, max_iops = 0, ds = 0; char dst[7] = "";
    for (int i = 0; i < HW_DISK_SLOTS; i++) {
        if (!srv->hw_disk[i][0]) continue;
        const HWComp *c = hw_find(srv->hw_disk[i]);
        if (c) { total_disk += c->size_gb; if (c->iops > max_iops) max_iops = c->iops; }
    }
    if (total_disk > 0) {
        ds = disk_stars(max_iops); star_str(ds, dst);
        addl(lines, nlines, max, "  Stockage  : %d GB  %d IOPS  %s",
             total_disk, max_iops, dst);
    } else {
        addl(lines, nlines, max, "  Stockage  : N/A");
    }

    int total = cs + ms + ds;
    const char *tier = total<=4 ? "Entree de gamme"
                     : total<=7 ? "Standard"
                     : total<=10? "Haute performance"
                     :            "Enterprise";
    addl(lines, nlines, max, "--------------------------------------------");
    addl(lines, nlines, max, "  Score : %d/12  %s", total, tier);

    if (cs > 0 && ms > 0 && ds > 0) {
        int mn = cs; const char *bot = "CPU";
        if (ms < mn) { mn = ms; bot = "memoire"; }
        if (ds < mn) {          bot = "stockage"; }
        if (mn < total / 3)
            addl(lines, nlines, max, "  Goulot  : %s", bot);
    }
}
