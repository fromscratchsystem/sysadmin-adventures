#include "framework.h"
#include "hardware.h"
#include "infra.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════ */

static Infra make_infra_with_server(void) {
    Infra inf;
    memset(&inf, 0, sizeof(inf));
    infra_rack_create(&inf, "rack-A", 42);
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 2048, 100);
    /* Slots par défaut : cpu=1, dimm=4, sata=2 */
    hw_server_init_slots(&inf.servers[0], NULL);
    return inf;
}

/* Cherche un slot vide du type donné, retourne 1 si trouvé */
static int slot_is_empty(const PhysServer *s, const char *type) {
    for (int i = 0; i < s->nhw_slots; i++)
        if (strcmp(s->hw_slots[i].type, type) == 0 && !s->hw_slots[i].comp_id[0])
            return 1;
    return 0;
}

/* Cherche un slot occupé par comp_id, retourne 1 si trouvé */
static int slot_has_comp(const PhysServer *s, const char *comp_id) {
    for (int i = 0; i < s->nhw_slots; i++)
        if (strcmp(s->hw_slots[i].comp_id, comp_id) == 0)
            return 1;
    return 0;
}

static void setup(void) {
    hw_catalog_load(NULL);
    server_catalog_load(NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_install / hw_remove
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_install_cpu_ok) {
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_install(&inf, "srv1", "epyc-7302"), 0);
    ASSERT_EQ(slot_has_comp(&inf.servers[0], "epyc-7302"), 1);
    ASSERT_EQ(inf.servers[0].cpu, 16);  /* EPYC 7302 = 16 cores */
}

TEST(hw_install_unknown_comp) {
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_install(&inf, "srv1", "no-such-thing"), -1);
}

TEST(hw_install_unknown_server) {
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_install(&inf, "ghost", "epyc-7302"), -2);
}

TEST(hw_install_cpu_already_present) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "epyc-7302");
    /* default slots = 1 cpu slot, so second cpu → no free slot → -3 */
    ASSERT_EQ(hw_install(&inf, "srv1", "epyc-7543"), -3);
}

TEST(hw_install_ddr_mismatch) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "epyc-7302");  /* DDR4 CPU */
    ASSERT_EQ(hw_install(&inf, "srv1", "ddr3-16gb"), -5);
    ASSERT_EQ(hw_install(&inf, "srv1", "ddr5-32gb"), -5);
    ASSERT_EQ(hw_install(&inf, "srv1", "ddr4-16gb"),  0);
}

TEST(hw_install_powered_on) {
    Infra inf = make_infra_with_server();
    inf.servers[0].powered = 1;
    ASSERT_EQ(hw_install(&inf, "srv1", "epyc-7302"), -4);
}

TEST(hw_remove_cpu_ok) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "epyc-7302");
    ASSERT_EQ(hw_remove(&inf, "srv1", "epyc-7302"), 0);
    ASSERT_EQ(inf.servers[0].cpu, 0);
    ASSERT_EQ(slot_has_comp(&inf.servers[0], "epyc-7302"), 0);
    ASSERT_EQ(slot_is_empty(&inf.servers[0], "cpu"), 1);
}

TEST(hw_remove_not_installed) {
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_remove(&inf, "srv1", "epyc-7302"), -1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_recompute — bug #4 (zero-guard)
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_recompute_ram_clears_on_full_remove) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "ddr4-16gb");
    ASSERT_EQ(inf.servers[0].ram_mb, 16384);
    hw_remove(&inf, "srv1", "ddr4-16gb");
    ASSERT_EQ(inf.servers[0].ram_mb, 0);
}

TEST(hw_recompute_disk_clears_on_full_remove) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "ssd-500gb");
    ASSERT_EQ(inf.servers[0].disk_gb, 500);
    hw_remove(&inf, "srv1", "ssd-500gb");
    ASSERT_EQ(inf.servers[0].disk_gb, 0);
}

TEST(hw_recompute_ram_accumulates) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "ddr4-16gb");
    hw_install(&inf, "srv1", "ddr4-32gb");
    ASSERT_EQ(inf.servers[0].ram_mb, 16384 + 32768);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_server_init_slots
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_server_init_slots_default) {
    Infra inf;
    memset(&inf, 0, sizeof(inf));
    infra_rack_create(&inf, "rack-A", 42);
    infra_server_add(&inf, "s", "rack-A", 1, 1, 0, 0, 0);
    hw_server_init_slots(&inf.servers[0], NULL);
    /* default = 1 cpu + 4 dimm + 2 sata = 7 slots */
    ASSERT_EQ(inf.servers[0].nhw_slots, 7);
    ASSERT_EQ(slot_is_empty(&inf.servers[0], "cpu"),  1);
    ASSERT_EQ(slot_is_empty(&inf.servers[0], "dimm"), 1);
    ASSERT_EQ(slot_is_empty(&inf.servers[0], "sata"), 1);
}

TEST(hw_server_init_slots_model) {
    Infra inf;
    memset(&inf, 0, sizeof(inf));
    infra_rack_create(&inf, "rack-A", 42);
    infra_server_add(&inf, "s", "rack-A", 1, 1, 0, 0, 0);
    hw_server_init_slots(&inf.servers[0], "dell-r740");
    /* dell-r740 = cpu=2,dimm=8,pcie_x16=3,u2=8 = 21 slots */
    ASSERT_EQ(inf.servers[0].nhw_slots, 21);
    ASSERT_EQ(slot_is_empty(&inf.servers[0], "pcie_x16"), 1);
    ASSERT_EQ(slot_is_empty(&inf.servers[0], "u2"), 1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_prop
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_prop_found) {
    const HWComp *c = hw_find("epyc-7302");
    ASSERT_EQ(c != NULL, 1);
    ASSERT_EQ(hw_prop_int(c, "cores", 0), 16);
    ASSERT_EQ(hw_prop_int(c, "ghz10", 0), 30);
}

TEST(hw_prop_not_found) {
    const HWComp *c = hw_find("epyc-7302");
    ASSERT_EQ(c != NULL, 1);
    ASSERT_EQ(hw_prop_int(c, "vram_gb", -1), -1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_remove — cas supplémentaires
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_remove_powered_on) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "epyc-7302");
    inf.servers[0].powered = 1;
    ASSERT_EQ(hw_remove(&inf, "srv1", "epyc-7302"), -4);
    ASSERT_EQ(slot_has_comp(&inf.servers[0], "epyc-7302"), 1); /* toujours là */
}

TEST(hw_remove_unknown_server) {
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_remove(&inf, "ghost", "epyc-7302"), -2);
}

TEST(hw_remove_unknown_comp) {
    Infra inf = make_infra_with_server();
    /* comp_id introuvable dans le catalogue → -1 immédiat */
    ASSERT_EQ(hw_remove(&inf, "srv1", "no-such-thing"), -1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : compatibilité mémoire DIMM→CPU
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_install_dimm_before_cpu_mismatch) {
    /* DDR3 installé en premier, puis CPU DDR4 → incompatible */
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_install(&inf, "srv1", "ddr3-16gb"), 0);     /* OK sans CPU */
    ASSERT_EQ(hw_install(&inf, "srv1", "epyc-7302"), -5);    /* DDR4 refusé */
}

TEST(hw_install_dimm_before_cpu_match) {
    /* DDR3 installé en premier, puis CPU DDR3 → OK */
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_install(&inf, "srv1", "ddr3-16gb"),    0);
    ASSERT_EQ(hw_install(&inf, "srv1", "xeon-e3-1220"), 0);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_list_catalog
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_list_catalog_all) {
    char lines[64][128]; int nl = 0;
    hw_list_catalog(NULL, lines, &nl, 64);
    ASSERT_GT(nl, 1);
    ASSERT_NE(strstr(lines[0], "Catalogue"), NULL);
}

TEST(hw_list_catalog_filtered_cpu) {
    char lines[32][128]; int nl = 0;
    hw_list_catalog("cpu", lines, &nl, 32);
    ASSERT_GT(nl, 1);
    /* Header contient "cpu", chaque composant aussi (entre crochets) */
    for (int i = 0; i < nl; i++)
        ASSERT_NE(strstr(lines[i], "cpu"), NULL);
}

TEST(hw_list_catalog_no_match) {
    char lines[8][128]; int nl = 0;
    hw_list_catalog("gpu", lines, &nl, 8); /* aucun GPU dans le catalogue par défaut */
    ASSERT_GT(nl, 0);
    ASSERT_NE(strstr(lines[nl - 1], "aucun"), NULL);
}

TEST(hw_list_server_models_basic) {
    char lines[32][128]; int nl = 0;
    hw_list_server_models(lines, &nl, 32);
    ASSERT_GT(nl, 1);
    ASSERT_NE(strstr(lines[0], "Modeles"), NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_show_server
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_show_server_with_components) {
    Infra inf = make_infra_with_server();
    hw_install(&inf, "srv1", "epyc-7302");
    hw_install(&inf, "srv1", "ddr4-16gb");
    hw_install(&inf, "srv1", "ssd-500gb");
    char lines[32][128]; int nl = 0;
    hw_show_server(&inf.servers[0], lines, &nl, 32);
    ASSERT_GT(nl, 0);
    ASSERT_NE(strstr(lines[0], "srv1"), NULL);
    int found_cpu = 0, found_ram = 0;
    for (int i = 0; i < nl; i++) {
        if (strstr(lines[i], "epyc") || strstr(lines[i], "EPYC")) found_cpu = 1;
        if (strstr(lines[i], "DDR4")) found_ram = 1;
    }
    ASSERT_EQ(found_cpu, 1);
    ASSERT_EQ(found_ram, 1);
}

TEST(hw_show_server_no_slots) {
    Infra inf;
    memset(&inf, 0, sizeof(inf));
    infra_rack_create(&inf, "rack-A", 42);
    infra_server_add(&inf, "bare", "rack-A", 1, 1, 1, 2048, 100);
    /* nhw_slots == 0 : hw_server_init_slots jamais appelé */
    char lines[16][128]; int nl = 0;
    hw_show_server(&inf.servers[0], lines, &nl, 16);
    ASSERT_GT(nl, 0);
    int found = 0;
    for (int i = 0; i < nl; i++)
        if (strstr(lines[i], "Aucun") || strstr(lines[i], "slot")) found = 1;
    ASSERT_EQ(found, 1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : save / load avec hw_slots
 * ═══════════════════════════════════════════════════════════════ */

TEST(save_load_roundtrip_hw_slots) {
    const char *path = "/tmp/test_hw_slots_roundtrip.inf";

    Infra orig = {0};
    infra_rack_create(&orig, "rack-A", 42);
    infra_server_add(&orig, "srv1", "rack-A", 1, 1, 1, 2048, 100);
    hw_server_init_slots(&orig.servers[0], NULL); /* cpu=1, dimm=4, sata=2 */
    hw_install(&orig, "srv1", "epyc-7302");       /* 16 cores DDR4         */
    hw_install(&orig, "srv1", "ddr4-16gb");       /* 16384 MB              */

    infra_save(&orig, path);

    Infra loaded;
    infra_load(&loaded, path);

    PhysServer *s = infra_find_server(&loaded, "srv1");
    ASSERT_NN(s);
    ASSERT_GT(s->nhw_slots, 0);
    hw_recompute(s);
    ASSERT_EQ(s->cpu,    16);
    ASSERT_EQ(s->ram_mb, 16384);

    remove(path);
}

/* ═══════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    setup();
    puts("=== test_hardware ===");

    puts("-- install/remove --");
    hw_install_cpu_ok();
    hw_install_unknown_comp();
    hw_install_unknown_server();
    hw_install_cpu_already_present();
    hw_install_ddr_mismatch();
    hw_install_powered_on();
    hw_remove_cpu_ok();
    hw_remove_not_installed();
    hw_remove_powered_on();
    hw_remove_unknown_server();
    hw_remove_unknown_comp();

    puts("-- compatibilite memoire DIMM->CPU --");
    hw_install_dimm_before_cpu_mismatch();
    hw_install_dimm_before_cpu_match();

    puts("-- hw_recompute --");
    hw_recompute_ram_clears_on_full_remove();
    hw_recompute_disk_clears_on_full_remove();
    hw_recompute_ram_accumulates();

    puts("-- hw_server_init_slots --");
    hw_server_init_slots_default();
    hw_server_init_slots_model();

    puts("-- hw_prop --");
    hw_prop_found();
    hw_prop_not_found();

    puts("-- hw_list_catalog --");
    hw_list_catalog_all();
    hw_list_catalog_filtered_cpu();
    hw_list_catalog_no_match();
    hw_list_server_models_basic();

    puts("-- hw_show_server --");
    hw_show_server_with_components();
    hw_show_server_no_slots();

    puts("-- save/load hw_slots --");
    save_load_roundtrip_hw_slots();

    RESULTS();
}
