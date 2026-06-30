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
    return inf;
}

/* hw_catalog doit être chargé avant les tests */
static void setup(void) {
    hw_catalog_load(NULL);       /* charge les défauts */
    server_catalog_load(NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : hw_install / hw_remove
 * ═══════════════════════════════════════════════════════════════ */

TEST(hw_install_cpu_ok) {
    Infra inf = make_infra_with_server();
    ASSERT_EQ(hw_install(&inf, "srv1", "epyc-7302"), 0);
    ASSERT_STR(inf.servers[0].hw_cpu, "epyc-7302");
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
    ASSERT_EQ(inf.servers[0].hw_cpu[0], '\0');
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
    /* après retrait complet, ram_mb doit revenir à 0, pas rester à 16384 */
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

    RESULTS();
}
