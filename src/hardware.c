#include "hardware.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
 * Tableaux des catalogues
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
    /* CPU DDR3 */
    {"atom-d510",    "Intel Atom D510",           "cpu",
     {{"cores","2"},{"ghz10","16"},{"mem_gen","DDR3"}}, 3},
    {"xeon-e3-1220", "Intel Xeon E3-1220 v3",     "cpu",
     {{"cores","4"},{"ghz10","31"},{"mem_gen","DDR3"}}, 3},
    {"xeon-e5-2670", "Intel Xeon E5-2670",         "cpu",
     {{"cores","8"},{"ghz10","26"},{"mem_gen","DDR3"}}, 3},
    /* CPU DDR4 */
    {"epyc-7302",    "AMD EPYC 7302",              "cpu",
     {{"cores","16"},{"ghz10","30"},{"mem_gen","DDR4"}}, 3},
    {"epyc-7543",    "AMD EPYC 7543",              "cpu",
     {{"cores","32"},{"ghz10","28"},{"mem_gen","DDR4"}}, 3},
    /* CPU DDR5 */
    {"xeon-w3-2435", "Intel Xeon W3-2435",         "cpu",
     {{"cores","8"},{"ghz10","31"},{"mem_gen","DDR5"}}, 3},
    /* RAM DDR3 */
    {"ddr3-4gb",     "4 GB DDR3 ECC",              "dimm",
     {{"size_mb","4096"},{"mem_gen","DDR3"}}, 2},
    {"ddr3-16gb",    "16 GB DDR3 ECC",             "dimm",
     {{"size_mb","16384"},{"mem_gen","DDR3"}}, 2},
    /* RAM DDR4 */
    {"ddr4-16gb",    "16 GB DDR4 ECC",             "dimm",
     {{"size_mb","16384"},{"mem_gen","DDR4"}}, 2},
    {"ddr4-32gb",    "32 GB DDR4 ECC",             "dimm",
     {{"size_mb","32768"},{"mem_gen","DDR4"}}, 2},
    /* RAM DDR5 */
    {"ddr5-32gb",    "32 GB DDR5 ECC",             "dimm",
     {{"size_mb","32768"},{"mem_gen","DDR5"}}, 2},
    {"ddr5-64gb",    "64 GB DDR5 ECC",             "dimm",
     {{"size_mb","65536"},{"mem_gen","DDR5"}}, 2},
    /* Disques SATA */
    {"hdd-1tb",      "1 TB SATA HDD",              "sata",
     {{"size_gb","1000"},{"disk_type","HDD"},{"iops","150"}}, 3},
    {"hdd-4tb",      "4 TB SATA HDD",              "sata",
     {{"size_gb","4000"},{"disk_type","HDD"},{"iops","150"}}, 3},
    {"ssd-500gb",    "500 GB SATA SSD",            "sata",
     {{"size_gb","500"},{"disk_type","SSD"},{"iops","5000"}}, 3},
    {"ssd-2tb",      "2 TB SATA SSD",              "sata",
     {{"size_gb","2000"},{"disk_type","SSD"},{"iops","8000"}}, 3},
    /* NVMe M.2 */
    {"nvme-512gb",   "512 GB NVMe M.2",            "m2",
     {{"size_gb","512"},{"disk_type","NVMe"},{"iops","20000"}}, 3},
    /* NVMe U.2 */
    {"nvme-2tb",     "2 TB NVMe U.2",              "u2",
     {{"size_gb","2000"},{"disk_type","NVMe"},{"iops","60000"}}, 3},
};
static const int hw_defaults_size =
    (int)(sizeof(hw_defaults) / sizeof(hw_defaults[0]));

static const ServerModel srv_defaults[] = {
    {"generic-1u", "Serveur rack 1U generique",     1, 1,
     {{"cpu",1},{"dimm",4},{"sata",4}}, 3},
    {"generic-2u", "Serveur rack 2U generique",     2, 1,
     {{"cpu",2},{"dimm",8},{"sata",8}}, 3},
    {"dell-r240",  "Dell PowerEdge R240 (1U)",      1, 1,
     {{"cpu",1},{"dimm",4},{"sata",2}}, 3},
    {"hp-dl360",   "HP ProLiant DL360 Gen9 (1U)",   1, 1,
     {{"cpu",2},{"dimm",4},{"sata",4}}, 3},
    {"dell-r740",  "Dell PowerEdge R740 (2U)",      2, 1,
     {{"cpu",2},{"dimm",8},{"pcie_x16",3},{"u2",8}}, 4},
    {"hp-dl380",   "HP ProLiant DL380 Gen10 (2U)",  2, 1,
     {{"cpu",2},{"dimm",8},{"sata",8},{"pcie_x16",2}}, 4},
    {"nuc-i5",     "Intel NUC i5 (occasion)",       0, 0,
     {{"cpu",1},{"dimm",2},{"m2",1}}, 3},
    {"hp-prodesk", "HP ProDesk Mini G5 (occasion)", 0, 0,
     {{"cpu",1},{"dimm",2},{"sata",2}}, 3},
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

const HWProp *hw_prop(const HWComp *c, const char *key) {
    for (int i = 0; i < c->nprops; i++)
        if (strcmp(c->props[i].key, key) == 0) return &c->props[i];
    return NULL;
}

int hw_prop_int(const HWComp *c, const char *key, int def) {
    const HWProp *p = hw_prop(c, key);
    return p ? atoi(p->val) : def;
}

/* ═══════════════════════════════════════════════════════════════
 * Parsing des fichiers de données
 * ═══════════════════════════════════════════════════════════════ */

/* Découpe 'line' sur '|', supprime les espaces autour de chaque champ. */
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

/* Parse "key=val" into prop. Returns 1 on success. */
static int parse_prop(const char *kv, HWProp *out) {
    const char *eq = strchr(kv, '=');
    if (!eq) return 0;
    int klen = (int)(eq - kv);
    if (klen <= 0 || klen >= 16) return 0;
    strncpy(out->key, kv, (size_t)klen);
    out->key[klen] = '\0';
    strncpy(out->val, eq + 1, 31);
    return 1;
}

/* Parse "type=count,type=count,..." into m->slots. */
static void parse_slot_defs(const char *spec, ServerModel *m) {
    const char *p = spec;
    while (*p && m->nslot_defs < MAX_SLOT_DEFS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != ',') end++;
        char tok[32] = "";
        int tlen = (int)(end - p);
        if (tlen > 31) tlen = 31;
        strncpy(tok, p, (size_t)tlen);
        const char *eq = strchr(tok, '=');
        if (eq) {
            int klen = (int)(eq - tok);
            if (klen > 0 && klen < 16) {
                strncpy(m->slots[m->nslot_defs].type, tok, (size_t)klen);
                m->slots[m->nslot_defs].type[klen] = '\0';
                m->slots[m->nslot_defs].count = atoi(eq + 1);
                m->nslot_defs++;
            }
        }
        p = (*end == ',') ? end + 1 : end;
    }
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

        /* Format: slot_type | id | label | key=val | key=val | ... */
        char fields[2 + MAX_HW_PROPS][64];
        int nf = split_pipe(p, fields, 2 + MAX_HW_PROPS);
        if (nf < 3) continue;

        HWComp *c = &hw_catalog[hw_catalog_size++];
        memset(c, 0, sizeof(*c));
        strncpy(c->slot_type, fields[0], 15);
        strncpy(c->id,        fields[1], 31);
        strncpy(c->label,     fields[2], 63);
        for (int i = 3; i < nf && c->nprops < MAX_HW_PROPS; i++)
            if (parse_prop(fields[i], &c->props[c->nprops]))
                c->nprops++;
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

        /* Format: id | label | size_u | has_ipmi | slot_type=count,... */
        char fields[5][64];
        int nf = split_pipe(p, fields, 5);
        if (nf < 5) continue;

        ServerModel *m = &server_catalog[server_catalog_size++];
        memset(m, 0, sizeof(*m));
        strncpy(m->id,    fields[0], 31);
        strncpy(m->label, fields[1], 63);
        m->size_u   = atoi(fields[2]);
        m->has_ipmi = atoi(fields[3]);
        parse_slot_defs(fields[4], m);
    }
    fclose(f);
    return server_catalog_size;
}

/* ═══════════════════════════════════════════════════════════════
 * Initialisation des slots d'un serveur depuis un modèle
 * ═══════════════════════════════════════════════════════════════ */

void hw_server_init_slots(PhysServer *srv, const char *model_id) {
    const ServerModel *m = model_id ? srv_model_find(model_id) : NULL;
    srv->nhw_slots = 0;
    memset(srv->hw_slots, 0, sizeof(srv->hw_slots));

    if (!m) {
        /* Slots par défaut pour un serveur générique */
        static const SlotDef def[] = {{"cpu",1},{"dimm",4},{"sata",2}};
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < def[i].count && srv->nhw_slots < MAX_HW_SLOTS; j++) {
                strncpy(srv->hw_slots[srv->nhw_slots].type, def[i].type, 15);
                srv->nhw_slots++;
            }
        return;
    }
    for (int i = 0; i < m->nslot_defs; i++)
        for (int j = 0; j < m->slots[i].count && srv->nhw_slots < MAX_HW_SLOTS; j++) {
            strncpy(srv->hw_slots[srv->nhw_slots].type, m->slots[i].type, 15);
            srv->nhw_slots++;
        }
}

/* ═══════════════════════════════════════════════════════════════
 * Recalcul des métriques agrégées
 * ═══════════════════════════════════════════════════════════════ */

void hw_recompute(PhysServer *srv) {
    int cores = 0, total_ram = 0, total_disk = 0;
    for (int i = 0; i < srv->nhw_slots; i++) {
        if (!srv->hw_slots[i].comp_id[0]) continue;
        const HWComp *c = hw_find(srv->hw_slots[i].comp_id);
        if (!c) continue;
        if (strcmp(c->slot_type, "cpu") == 0)
            cores += hw_prop_int(c, "cores", 0);
        else if (strcmp(c->slot_type, "dimm") == 0)
            total_ram += hw_prop_int(c, "size_mb", 0);
        else
            total_disk += hw_prop_int(c, "size_gb", 0);
    }
    srv->cpu     = cores;
    srv->ram_mb  = total_ram;
    srv->disk_gb = total_disk;
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

    /* Trouver un slot libre du bon type */
    int free_slot = -1;
    for (int i = 0; i < srv->nhw_slots; i++) {
        if (strcmp(srv->hw_slots[i].type, comp->slot_type) == 0 &&
            !srv->hw_slots[i].comp_id[0]) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) return -3;

    /* Vérification compatibilité mémoire */
    const HWProp *new_gen_p = hw_prop(comp, "mem_gen");
    const char   *new_gen   = new_gen_p ? new_gen_p->val : "";

    if (strcmp(comp->slot_type, "cpu") == 0 && new_gen[0]) {
        /* Vérifier les DIMMs déjà installés */
        for (int i = 0; i < srv->nhw_slots; i++) {
            if (strcmp(srv->hw_slots[i].type, "dimm") != 0) continue;
            if (!srv->hw_slots[i].comp_id[0]) continue;
            const HWComp *ram = hw_find(srv->hw_slots[i].comp_id);
            if (!ram) continue;
            const HWProp *rg = hw_prop(ram, "mem_gen");
            if (rg && rg->val[0] && strcmp(new_gen, rg->val) != 0) return -5;
        }
    } else if (strcmp(comp->slot_type, "dimm") == 0 && new_gen[0]) {
        /* Vérifier le CPU installé */
        for (int i = 0; i < srv->nhw_slots; i++) {
            if (strcmp(srv->hw_slots[i].type, "cpu") != 0) continue;
            if (!srv->hw_slots[i].comp_id[0]) continue;
            const HWComp *cpu = hw_find(srv->hw_slots[i].comp_id);
            if (!cpu) continue;
            const HWProp *cg = hw_prop(cpu, "mem_gen");
            if (cg && cg->val[0] && strcmp(new_gen, cg->val) != 0) return -5;
        }
    }

    strncpy(srv->hw_slots[free_slot].comp_id, comp_id, 31);
    hw_recompute(srv);
    return 0;
}

int hw_remove(Infra *inf, const char *server, const char *comp_id) {
    const HWComp *comp = hw_find(comp_id);
    if (!comp) return -1;

    PhysServer *srv = infra_find_server(inf, server);
    if (!srv) return -2;
    if (srv->powered) return -4;

    for (int i = 0; i < srv->nhw_slots; i++) {
        if (strcmp(srv->hw_slots[i].comp_id, comp_id) == 0) {
            srv->hw_slots[i].comp_id[0] = '\0';
            hw_recompute(srv);
            return 0;
        }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════
 * Affichage
 * ═══════════════════════════════════════════════════════════════ */

static void addl(char lines[][128], int *n, int max, const char *fmt, ...) {
    if (*n >= max) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(lines[(*n)++], 128, fmt, ap);
    va_end(ap);
}

void hw_list_catalog(const char *slot_type_filter,
                     char lines[][128], int *nlines, int max) {
    *nlines = 0;
    if (slot_type_filter)
        addl(lines, nlines, max, "  ── Composants [%s] ──", slot_type_filter);
    else
        addl(lines, nlines, max, "  ── Catalogue composants ──");

    int found = 0;
    for (int i = 0; i < hw_catalog_size; i++) {
        const HWComp *c = &hw_catalog[i];
        if (slot_type_filter && strcmp(c->slot_type, slot_type_filter) != 0) continue;
        char props_buf[80] = "";
        for (int j = 0; j < c->nprops; j++) {
            char tmp[52];
            snprintf(tmp, sizeof(tmp), "%s=%s ", c->props[j].key, c->props[j].val);
            strncat(props_buf, tmp, sizeof(props_buf) - strlen(props_buf) - 1);
        }
        addl(lines, nlines, max, "  %-14s  %-28s  [%-8s]  %s",
             c->id, c->label, c->slot_type, props_buf);
        found++;
    }
    if (!found)
        addl(lines, nlines, max, "  (aucun composant de ce type)");
}

void hw_list_server_models(char lines[][128], int *nlines, int max) {
    *nlines = 0;
    addl(lines, nlines, max, "  ── Modeles de serveurs ──");
    for (int i = 0; i < server_catalog_size; i++) {
        const ServerModel *m = &server_catalog[i];
        const char *form = (m->size_u == 0) ? "MNI"
                         : (m->size_u == 1)  ? " 1U" : " 2U";
        char slots_buf[64] = "";
        for (int j = 0; j < m->nslot_defs; j++) {
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "%s=%d ", m->slots[j].type, m->slots[j].count);
            strncat(slots_buf, tmp, sizeof(slots_buf) - strlen(slots_buf) - 1);
        }
        addl(lines, nlines, max, "  %-14s  %-32s  %s  IPMI:%s  %s",
             m->id, m->label, form, m->has_ipmi ? "O" : "N", slots_buf);
    }
}

/* ── Affichage métriques d'un serveur ────────────────────────── */

static void star_str(int n, char out[7]) {
    out[0] = '[';
    for (int i = 0; i < 4; i++) out[1+i] = (i < n) ? '*' : '.';
    out[5] = ']'; out[6] = '\0';
}

static int cpu_stars (float s) { return s<5  ? 1 : s<15 ? 2 : s<30 ? 3 : 4; }
static int mem_stars (int mb)  { int g=mb/1024; return g<8?1:g<64?2:g<256?3:4; }
static int disk_stars(int io)  { return io<=200?1:io<=10000?2:io<=25000?3:4; }

void hw_show_server(const PhysServer *srv, char lines[][128], int *nlines, int max) {
    *nlines = 0;
    addl(lines, nlines, max, "Hardware : %s%s", srv->name,
         srv->has_ipmi ? "" : "  [pas d'IPMI]");
    if (srv->model_id[0]) {
        const ServerModel *m = srv_model_find(srv->model_id);
        addl(lines, nlines, max, "  Modele  : %s", m ? m->label : srv->model_id);
    }
    addl(lines, nlines, max, "--------------------------------------------");

    int has_hw = 0;
    for (int i = 0; i < srv->nhw_slots; i++) {
        const char *stype = srv->hw_slots[i].type;
        const char *cid   = srv->hw_slots[i].comp_id;
        if (cid[0]) has_hw = 1;

        if (cid[0]) {
            const HWComp *c = hw_find(cid);
            if (c) {
                char detail[192] = "";
                /* CPU: show cores, ghz, mem_gen */
                if (strcmp(stype, "cpu") == 0) {
                    const HWProp *cores = hw_prop(c, "cores");
                    const HWProp *ghz10 = hw_prop(c, "ghz10");
                    const HWProp *gen   = hw_prop(c, "mem_gen");
                    int g = ghz10 ? atoi(ghz10->val) : 0;
                    snprintf(detail, sizeof(detail), "%s  (%sc @ %d.%dGHz  %s)",
                             c->label, cores ? cores->val : "?",
                             g/10, g%10, gen ? gen->val : "?");
                } else if (strcmp(stype, "dimm") == 0) {
                    const HWProp *gen = hw_prop(c, "mem_gen");
                    snprintf(detail, sizeof(detail), "%s  [%s]",
                             c->label, gen ? gen->val : "?");
                } else {
                    const HWProp *gb  = hw_prop(c, "size_gb");
                    const HWProp *iops = hw_prop(c, "iops");
                    snprintf(detail, sizeof(detail), "%s  %s GB  %s IOPS",
                             c->label, gb ? gb->val : "?", iops ? iops->val : "?");
                }
                addl(lines, nlines, max, "  [%-8s] %d : %s", stype, i, detail);
            } else {
                addl(lines, nlines, max, "  [%-8s] %d : %s (inconnu)", stype, i, cid);
            }
        } else {
            addl(lines, nlines, max, "  [%-8s] %d : (vide)", stype, i);
        }
    }

    if (!has_hw && srv->nhw_slots == 0) {
        addl(lines, nlines, max, "  Aucun slot configure.");
        addl(lines, nlines, max, "  /server add <nom> <rack> <slot> <modele-id>");
        return;
    }

    addl(lines, nlines, max, "--------------------------------------------");

    /* Scores */
    float cpu_score = 0.0f; int cs = 0, ms = 0, ds = 0, max_iops = 0;
    char cst[7]="", mst[7]="", dst[7]="";

    for (int i = 0; i < srv->nhw_slots; i++) {
        if (!srv->hw_slots[i].comp_id[0]) continue;
        const HWComp *c = hw_find(srv->hw_slots[i].comp_id);
        if (!c) continue;
        if (strcmp(c->slot_type, "cpu") == 0) {
            int cores = hw_prop_int(c, "cores", 0);
            int ghz10 = hw_prop_int(c, "ghz10", 0);
            cpu_score += (float)cores * (float)ghz10 / 10.0f;
        } else if (strcmp(c->slot_type, "sata") == 0 ||
                   strcmp(c->slot_type, "u2")   == 0 ||
                   strcmp(c->slot_type, "m2")   == 0) {
            int iops = hw_prop_int(c, "iops", 0);
            if (iops > max_iops) max_iops = iops;
        }
    }

    if (cpu_score > 0.0f) {
        cs = cpu_stars(cpu_score); star_str(cs, cst);
        addl(lines, nlines, max, "  Puissance : %4.1f GHz-coeurs  %s", cpu_score, cst);
    } else {
        addl(lines, nlines, max, "  Puissance : N/A");
    }
    if (srv->ram_mb > 0) {
        ms = mem_stars(srv->ram_mb); star_str(ms, mst);
        addl(lines, nlines, max, "  Memoire   : %d MB (%d GB)  %s",
             srv->ram_mb, srv->ram_mb / 1024, mst);
    } else {
        addl(lines, nlines, max, "  Memoire   : N/A");
    }
    if (srv->disk_gb > 0) {
        ds = disk_stars(max_iops); star_str(ds, dst);
        addl(lines, nlines, max, "  Stockage  : %d GB  %d IOPS  %s",
             srv->disk_gb, max_iops, dst);
    } else {
        addl(lines, nlines, max, "  Stockage  : N/A");
    }

    int total = cs + ms + ds;
    const char *tier = total<=4  ? "Entree de gamme"
                     : total<=7  ? "Standard"
                     : total<=10 ? "Haute performance"
                     :             "Enterprise";
    addl(lines, nlines, max, "--------------------------------------------");
    addl(lines, nlines, max, "  Score : %d/12  %s", total, tier);

    if (cs > 0 && ms > 0 && ds > 0) {
        int mn = cs; const char *bot = "CPU";
        if (ms < mn) { mn = ms; bot = "memoire"; }
        if (ds < mn) { mn = ds; bot = "stockage"; }
        if (mn < total / 3)
            addl(lines, nlines, max, "  Goulot  : %s", bot);
    }
}
