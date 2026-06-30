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

    RESULTS();
}
