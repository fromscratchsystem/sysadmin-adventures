#include "framework.h"
#include "infra.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════ */

static Infra make_infra_with_rack(void) {
    Infra inf;
    memset(&inf, 0, sizeof(inf));
    infra_rack_create(&inf, "rack-A", 42);
    return inf;
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : Rack
 * ═══════════════════════════════════════════════════════════════ */

TEST(rack_create_basic) {
    Infra inf = {0};
    ASSERT_EQ(infra_rack_create(&inf, "rack-A", 42), 0);
    ASSERT_EQ(inf.nracks, 1);
    ASSERT_STR(inf.racks[0].name, "rack-A");
    ASSERT_EQ(inf.racks[0].units, 42);
}

TEST(rack_create_default_units) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-B", 0);
    ASSERT_EQ(inf.racks[0].units, RACK_DEFAULT_U);
}

TEST(rack_create_duplicate) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    ASSERT_EQ(infra_rack_create(&inf, "rack-A", 10), -3);
    ASSERT_EQ(inf.nracks, 1);
}

TEST(rack_create_limit) {
    Infra inf = {0};
    for (int i = 0; i < MAX_RACKS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "rack-%d", i);
        infra_rack_create(&inf, name, 42);
    }
    ASSERT_EQ(infra_rack_create(&inf, "rack-overflow", 42), -1);
    ASSERT_EQ(inf.nracks, MAX_RACKS);
}

TEST(rack_find) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    Rack *r = infra_find_rack(&inf, "rack-A");
    ASSERT_NN(r);
    ASSERT_STR(r->name, "rack-A");
    ASSERT_NULL(infra_find_rack(&inf, "rack-Z"));
}

TEST(rack_delete_empty) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    ASSERT_EQ(infra_rack_delete(&inf, "rack-A"), 0);
    ASSERT_EQ(inf.nracks, 0);
    ASSERT_NULL(infra_find_rack(&inf, "rack-A"));
}

TEST(rack_delete_nonempty) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    ASSERT_EQ(infra_rack_delete(&inf, "rack-A"), -2);
    ASSERT_EQ(inf.nracks, 1);
}

TEST(rack_delete_unknown) {
    Infra inf = {0};
    ASSERT_EQ(infra_rack_delete(&inf, "ghost"), -1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : Serveur
 * ═══════════════════════════════════════════════════════════════ */

TEST(server_add_basic) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_server_add(&inf, "srv1", "rack-A", 2, 1, 4, 8192, 500), 0);
    ASSERT_EQ(inf.nservers, 1);
    ASSERT_STR(inf.servers[0].name, "srv1");
    ASSERT_EQ(inf.servers[0].slot,    2);
    ASSERT_EQ(inf.servers[0].size_u,  1);
    ASSERT_EQ(inf.servers[0].cpu,     4);
    ASSERT_EQ(inf.servers[0].ram_mb,  8192);
    ASSERT_EQ(inf.servers[0].disk_gb, 500);
    ASSERT_EQ(inf.servers[0].powered, 0);
}

TEST(server_add_slot_zero) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_server_add(&inf, "srv1", "rack-A", 0, 1, 1, 1024, 100), -4);
    ASSERT_EQ(inf.nservers, 0);
}

TEST(server_add_slot_negative) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_server_add(&inf, "srv1", "rack-A", -3, 1, 1, 1024, 100), -4);
    ASSERT_EQ(inf.nservers, 0);
}

TEST(server_add_slot_overlap) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 2, 2, 1, 1024, 100); /* slots 2-3 */
    ASSERT_EQ(infra_server_add(&inf, "srv2", "rack-A", 3, 1, 1, 1024, 100), -5);
    ASSERT_EQ(inf.nservers, 1);
}

TEST(server_add_bad_rack) {
    Infra inf = {0};
    ASSERT_EQ(infra_server_add(&inf, "srv1", "rack-ghost", 1, 1, 1, 1024, 100), -2);
}

TEST(server_add_duplicate) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    ASSERT_EQ(infra_server_add(&inf, "srv1", "rack-A", 5, 1, 1, 1024, 100), -3);
    ASSERT_EQ(inf.nservers, 1);
}

TEST(server_delete_off) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    ASSERT_EQ(infra_server_delete(&inf, "srv1"), 0);
    ASSERT_EQ(inf.nservers, 0);
    ASSERT_NULL(infra_find_server(&inf, "srv1"));
}

TEST(server_delete_on) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    inf.servers[0].powered = 1;
    ASSERT_EQ(infra_server_delete(&inf, "srv1"), -2);
    ASSERT_EQ(inf.nservers, 1);
}

TEST(server_delete_removes_cables) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    infra_cable_connect(&inf, "srv1", "eth0", "sw1", 1);
    ASSERT_EQ(inf.ncables, 1);
    infra_server_delete(&inf, "srv1");
    ASSERT_EQ(inf.ncables, 0);
}

TEST(server_delete_unknown) {
    Infra inf = {0};
    ASSERT_EQ(infra_server_delete(&inf, "ghost"), -1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : Switch
 * ═══════════════════════════════════════════════════════════════ */

TEST(switch_add_basic) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_switch_add(&inf, "sw1", "rack-A", 1, 1, 24), 0);
    ASSERT_EQ(inf.nswitches, 1);
    ASSERT_STR(inf.switches[0].name, "sw1");
    ASSERT_EQ(inf.switches[0].ports, 24);
}

TEST(switch_add_slot_zero) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_switch_add(&inf, "sw1", "rack-A", 0, 1, 24), -4);
    ASSERT_EQ(inf.nswitches, 0);
}

TEST(switch_slot_overlap_with_server) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 2, 2, 1, 1024, 100); /* slots 2-3 */
    ASSERT_EQ(infra_switch_add(&inf, "sw1", "rack-A", 3, 1, 24), -5);
}

TEST(switch_delete_off) {
    Infra inf = make_infra_with_rack();
    infra_switch_add(&inf, "sw1", "rack-A", 1, 1, 24);
    ASSERT_EQ(infra_switch_delete(&inf, "sw1"), 0);
    ASSERT_EQ(inf.nswitches, 0);
}

TEST(switch_delete_on) {
    Infra inf = make_infra_with_rack();
    infra_switch_add(&inf, "sw1", "rack-A", 1, 1, 24);
    inf.switches[0].powered = 1;
    ASSERT_EQ(infra_switch_delete(&inf, "sw1"), -2);
    ASSERT_EQ(inf.nswitches, 1);
}

TEST(switch_delete_removes_cables) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    infra_cable_connect(&inf, "srv1", "eth0", "sw1", 1);
    infra_cable_connect(&inf, "srv1", "eth1", "sw1", 2);
    ASSERT_EQ(inf.ncables, 2);
    infra_switch_delete(&inf, "sw1");
    ASSERT_EQ(inf.ncables, 0);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : Câbles
 * ═══════════════════════════════════════════════════════════════ */

TEST(cable_connect_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1", 3), 0);
    ASSERT_EQ(inf.ncables, 1);
    ASSERT_STR(inf.cables[0].server, "srv1");
    ASSERT_STR(inf.cables[0].nic,   "eth0");
    ASSERT_STR(inf.cables[0].sw,    "sw1");
    ASSERT_EQ(inf.cables[0].port, 3);
}

TEST(cable_connect_dup_nic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    infra_cable_connect(&inf, "srv1", "eth0", "sw1", 1);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1", 2), -3);
    ASSERT_EQ(inf.ncables, 1);
}

TEST(cable_connect_unknown_server) {
    Infra inf = make_infra_with_rack();
    infra_switch_add(&inf, "sw1", "rack-A", 1, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "ghost", "eth0", "sw1", 1), -2);
}

TEST(cable_connect_port_zero) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1", 0), -4);
    ASSERT_EQ(inf.ncables, 0);
}

TEST(cable_connect_port_negative) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1", -5), -4);
    ASSERT_EQ(inf.ncables, 0);
}

TEST(cable_connect_port_exceeds_capacity) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1", 25), -4);
    ASSERT_EQ(inf.ncables, 0);
}

TEST(cable_connect_port_boundary) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1",  1), 0);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth1", "sw1", 24), 0);
    ASSERT_EQ(inf.ncables, 2);
}

TEST(cable_connect_dup_port) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_server_add(&inf, "srv2", "rack-A", 2, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "sw1", 3), 0);
    ASSERT_EQ(infra_cable_connect(&inf, "srv2", "eth0", "sw1", 3), -5);
    ASSERT_EQ(inf.ncables, 1);
}

TEST(server_port_unique_after_delete) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "A", "rack-A", 1, 1, 1, 1024, 100);
    infra_server_add(&inf, "B", "rack-A", 2, 1, 1, 1024, 100);
    int port_b = infra_find_server(&inf, "B")->port;
    infra_server_delete(&inf, "A");
    infra_server_add(&inf, "C", "rack-A", 1, 1, 1, 1024, 100);
    int port_c = infra_find_server(&inf, "C")->port;
    ASSERT_NE(port_b, port_c);
}

TEST(cable_disconnect_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    infra_cable_connect(&inf, "srv1", "eth0", "sw1", 1);
    ASSERT_EQ(infra_cable_disconnect(&inf, "srv1", "eth0"), 0);
    ASSERT_EQ(inf.ncables, 0);
}

TEST(cable_disconnect_unknown) {
    Infra inf = {0};
    ASSERT_EQ(infra_cable_disconnect(&inf, "ghost", "eth0"), -1);
}

TEST(cable_server_nets) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw-lan",  "rack-A", 5, 1, 24);
    infra_switch_add(&inf, "sw-mgmt", "rack-A", 7, 1, 24);
    infra_cable_connect(&inf, "srv1", "eth0", "sw-lan",  1);
    infra_cable_connect(&inf, "srv1", "eth1", "sw-mgmt", 1);

    const char *nets[8];
    int n = infra_server_nets(&inf, "srv1", nets, 8);
    ASSERT_EQ(n, 2);
    ASSERT_STR(nets[0], "sw-lan");
    ASSERT_STR(nets[1], "sw-mgmt");
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : Persistance
 * ═══════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════
 * Suite : limites MAX
 * ═══════════════════════════════════════════════════════════════ */

TEST(server_add_limit) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    for (int i = 0; i < MAX_SERVERS; i++) {
        char name[16]; snprintf(name, sizeof(name), "s%d", i);
        ASSERT_EQ(infra_server_add(&inf, name, "rack-A", i + 1, 1, 1, 1024, 100), 0);
    }
    ASSERT_EQ(infra_server_add(&inf, "overflow", "rack-A", MAX_SERVERS + 1, 1, 1, 1024, 100), -1);
    ASSERT_EQ(inf.nservers, MAX_SERVERS);
}

TEST(switch_add_duplicate) {
    Infra inf = make_infra_with_rack();
    infra_switch_add(&inf, "sw1", "rack-A", 1, 1, 24);
    ASSERT_EQ(infra_switch_add(&inf, "sw1", "rack-A", 5, 1, 48), -3);
    ASSERT_EQ(inf.nswitches, 1);
}

TEST(switch_add_limit) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    for (int i = 0; i < MAX_SWITCHES; i++) {
        char name[16]; snprintf(name, sizeof(name), "sw%d", i);
        ASSERT_EQ(infra_switch_add(&inf, name, "rack-A", i + 1, 1, 24), 0);
    }
    ASSERT_EQ(infra_switch_add(&inf, "sw-overflow", "rack-A", MAX_SWITCHES + 1, 1, 24), -1);
    ASSERT_EQ(inf.nswitches, MAX_SWITCHES);
}

TEST(switch_delete_unknown) {
    Infra inf = {0};
    ASSERT_EQ(infra_switch_delete(&inf, "ghost-sw"), -1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : câbles — cas supplémentaires
 * ═══════════════════════════════════════════════════════════════ */

TEST(cable_connect_unknown_switch) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    ASSERT_EQ(infra_cable_connect(&inf, "srv1", "eth0", "ghost-sw", 1), -2);
    ASSERT_EQ(inf.ncables, 0);
}

TEST(server_nets_no_cables) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    const char *nets[4];
    ASSERT_EQ(infra_server_nets(&inf, "srv1", nets, 4), 0);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : infra_server_add_model / infra_minipc_add
 * ═══════════════════════════════════════════════════════════════ */

TEST(server_add_model_basic) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_server_add_model(&inf, "web01", "rack-A", 1, 1, 0, "dell-r240"), 0);
    PhysServer *s = infra_find_server(&inf, "web01");
    ASSERT_NN(s);
    ASSERT_STR(s->model_id, "dell-r240");
    ASSERT_EQ(s->has_ipmi, 0);
}

TEST(minipc_add_basic) {
    Infra inf = make_infra_with_rack();
    ASSERT_EQ(infra_minipc_add(&inf, "nuc1", "rack-A", 5, "nuc-i5"), 0);
    PhysServer *s = infra_find_server(&inf, "nuc1");
    ASSERT_NN(s);
    ASSERT_EQ(s->is_minipc, 1);
    ASSERT_EQ(s->subslot,   0);
    ASSERT_STR(s->model_id, "nuc-i5");
}

TEST(minipc_add_two_per_slot) {
    Infra inf = make_infra_with_rack();
    infra_minipc_add(&inf, "nuc1", "rack-A", 5, "");
    ASSERT_EQ(infra_minipc_add(&inf, "nuc2", "rack-A", 5, ""), 0);
    PhysServer *s1 = infra_find_server(&inf, "nuc1");
    PhysServer *s2 = infra_find_server(&inf, "nuc2");
    ASSERT_NN(s1); ASSERT_NN(s2);
    ASSERT_NE(s1->subslot, s2->subslot);
}

TEST(minipc_add_slot_full) {
    Infra inf = make_infra_with_rack();
    infra_minipc_add(&inf, "nuc1", "rack-A", 5, "");
    infra_minipc_add(&inf, "nuc2", "rack-A", 5, "");
    ASSERT_EQ(infra_minipc_add(&inf, "nuc3", "rack-A", 5, ""), -5);
    ASSERT_EQ(inf.nservers, 2);
}

TEST(minipc_add_bad_rack) {
    Infra inf = {0};
    ASSERT_EQ(infra_minipc_add(&inf, "nuc1", "ghost-rack", 1, ""), -2);
}

TEST(minipc_add_duplicate) {
    Infra inf = make_infra_with_rack();
    infra_minipc_add(&inf, "nuc1", "rack-A", 1, "");
    ASSERT_EQ(infra_minipc_add(&inf, "nuc1", "rack-A", 3, ""), -3);
    ASSERT_EQ(inf.nservers, 1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : fonctions list_*
 * ═══════════════════════════════════════════════════════════════ */

TEST(list_racks_empty) {
    Infra inf = {0};
    char lines[4][128]; int nl = 0;
    infra_list_racks(&inf, lines, &nl, 4);
    ASSERT_EQ(nl, 1);
    ASSERT_NE(strstr(lines[0], "aucune"), NULL);
}

TEST(list_racks_basic) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    infra_rack_create(&inf, "rack-B", 24);
    char lines[8][128]; int nl = 0;
    infra_list_racks(&inf, lines, &nl, 8);
    ASSERT_EQ(nl, 2);
    ASSERT_NE(strstr(lines[0], "rack-A"), NULL);
    ASSERT_NE(strstr(lines[1], "rack-B"), NULL);
}

TEST(list_servers_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "s1", "rack-A", 1, 1, 4, 8192, 200);
    infra_server_add(&inf, "s2", "rack-A", 2, 1, 8, 16384, 500);
    char lines[8][128]; int nl = 0;
    infra_list_servers(&inf, NULL, lines, &nl, 8);
    ASSERT_EQ(nl, 2);
}

TEST(list_servers_filtered_by_rack) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    infra_rack_create(&inf, "rack-B", 42);
    infra_server_add(&inf, "s1", "rack-A", 1, 1, 4, 8192, 200);
    infra_server_add(&inf, "s2", "rack-B", 1, 1, 4, 8192, 200);
    char lines[8][128]; int nl = 0;
    infra_list_servers(&inf, "rack-A", lines, &nl, 8);
    ASSERT_EQ(nl, 1);
    ASSERT_NE(strstr(lines[0], "s1"), NULL);
}

TEST(list_switches_basic) {
    Infra inf = make_infra_with_rack();
    infra_switch_add(&inf, "sw1", "rack-A", 1, 1, 24);
    char lines[4][128]; int nl = 0;
    infra_list_switches(&inf, NULL, lines, &nl, 4);
    ASSERT_EQ(nl, 1);
    ASSERT_NE(strstr(lines[0], "sw1"), NULL);
}

TEST(list_cables_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "srv1", "rack-A", 1, 1, 1, 1024, 100);
    infra_switch_add(&inf, "sw1",  "rack-A", 5, 1, 24);
    infra_cable_connect(&inf, "srv1", "eth0", "sw1", 3);
    char lines[4][128]; int nl = 0;
    infra_list_cables(&inf, lines, &nl, 4);
    ASSERT_EQ(nl, 1);
    ASSERT_NE(strstr(lines[0], "srv1"), NULL);
    ASSERT_NE(strstr(lines[0], "eth0"), NULL);
}

TEST(list_cables_empty) {
    Infra inf = {0};
    char lines[4][128]; int nl = 0;
    infra_list_cables(&inf, lines, &nl, 4);
    ASSERT_EQ(nl, 1);
    ASSERT_NE(strstr(lines[0], "aucun"), NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : rack_render
 * ═══════════════════════════════════════════════════════════════ */

TEST(rack_render_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "web01", "rack-A", 1, 1, 4, 8192, 200);
    infra_switch_add(&inf, "sw1",   "rack-A", 3, 1, 24);
    char lines[64][128]; int nl = 0;
    infra_rack_render(&inf, "rack-A", lines, &nl, 64);
    ASSERT_GT(nl, 2);
    ASSERT_NE(strstr(lines[0], "rack-A"), NULL);
    int found_srv = 0, found_sw = 0;
    for (int i = 0; i < nl; i++) {
        if (strstr(lines[i], "web01")) found_srv = 1;
        if (strstr(lines[i], "sw1"))   found_sw  = 1;
    }
    ASSERT_EQ(found_srv, 1);
    ASSERT_EQ(found_sw,  1);
}

TEST(rack_render_unknown) {
    Infra inf = {0};
    char lines[4][128]; int nl = 0;
    infra_rack_render(&inf, "ghost", lines, &nl, 4);
    ASSERT_GT(nl, 0);
    ASSERT_NE(strstr(lines[0], "ghost"), NULL);
}

TEST(rack_render_with_minipc) {
    Infra inf = make_infra_with_rack();
    infra_minipc_add(&inf, "nuc1", "rack-A", 5, "");
    infra_minipc_add(&inf, "nuc2", "rack-A", 5, "");
    char lines[64][128]; int nl = 0;
    infra_rack_render(&inf, "rack-A", lines, &nl, 64);
    int found_mini = 0;
    for (int i = 0; i < nl; i++)
        if (strstr(lines[i], "MNI")) found_mini = 1;
    ASSERT_EQ(found_mini, 1);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : Persistance
 * ═══════════════════════════════════════════════════════════════ */

TEST(save_load_roundtrip) {
    const char *path = "/tmp/test_infra_roundtrip.inf";

    /* Construire une infra */
    Infra orig = {0};
    infra_rack_create(&orig, "rack-A", 42);
    infra_server_add(&orig, "srv1", "rack-A", 1, 2, 8, 16384, 1000);
    infra_switch_add(&orig, "sw1",  "rack-A", 5, 1, 48);
    infra_cable_connect(&orig, "srv1", "eth0", "sw1", 3);
    orig.servers[0].powered = 1;
    orig.switches[0].powered = 1;

    infra_save(&orig, path);

    Infra loaded;
    infra_load(&loaded, path);

    ASSERT_EQ(loaded.nracks,    1);
    ASSERT_EQ(loaded.nservers,  1);
    ASSERT_EQ(loaded.nswitches, 1);
    ASSERT_EQ(loaded.ncables,   1);

    ASSERT_STR(loaded.racks[0].name,    "rack-A");
    ASSERT_EQ (loaded.racks[0].units,   42);

    ASSERT_STR(loaded.servers[0].name,   "srv1");
    ASSERT_EQ (loaded.servers[0].cpu,    8);
    ASSERT_EQ (loaded.servers[0].ram_mb, 16384);
    ASSERT_EQ (loaded.servers[0].slot,   1);
    ASSERT_EQ (loaded.servers[0].size_u, 2);
    ASSERT_EQ (loaded.servers[0].powered, 1);

    ASSERT_STR(loaded.switches[0].name,   "sw1");
    ASSERT_EQ (loaded.switches[0].ports,  48);
    ASSERT_EQ (loaded.switches[0].powered, 1);

    ASSERT_STR(loaded.cables[0].server, "srv1");
    ASSERT_STR(loaded.cables[0].nic,    "eth0");
    ASSERT_STR(loaded.cables[0].sw,     "sw1");
    ASSERT_EQ (loaded.cables[0].port,   3);

    remove(path);
}

/* ═══════════════════════════════════════════════════════════════
 * Suite : show server / show switch
 * ═══════════════════════════════════════════════════════════════ */

TEST(server_show_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "web01", "rack-A", 3, 2, 8, 16384, 500);
    infra_switch_add(&inf, "sw1", "rack-A", 10, 1, 24);
    infra_cable_connect(&inf, "web01", "eth0", "sw1", 1);
    infra_cable_connect(&inf, "web01", "eth1", "sw1", 2);

    char lines[32][128]; int nl = 0;
    infra_server_show(&inf, "web01", lines, &nl, 32);

    ASSERT_GT(nl, 0);
    /* Première ligne : nom du serveur */
    ASSERT_NE(strstr(lines[0], "web01"), NULL);
    /* Doit mentionner le rack et le slot */
    int found_rack = 0;
    for (int i = 0; i < nl; i++)
        if (strstr(lines[i], "rack-A") && strstr(lines[i], "3")) { found_rack = 1; break; }
    ASSERT_EQ(found_rack, 1);
    /* Doit lister les deux câbles */
    int found_eth0 = 0, found_eth1 = 0;
    for (int i = 0; i < nl; i++) {
        if (strstr(lines[i], "eth0")) found_eth0 = 1;
        if (strstr(lines[i], "eth1")) found_eth1 = 1;
    }
    ASSERT_EQ(found_eth0, 1);
    ASSERT_EQ(found_eth1, 1);
}

TEST(server_show_unknown) {
    Infra inf = {0};
    char lines[8][128]; int nl = 0;
    infra_server_show(&inf, "ghost", lines, &nl, 8);
    ASSERT_GT(nl, 0);
    ASSERT_NE(strstr(lines[0], "ghost"), NULL);
}

TEST(switch_show_basic) {
    Infra inf = make_infra_with_rack();
    infra_server_add(&inf, "db01", "rack-A", 1, 1, 4, 8192, 200);
    infra_switch_add(&inf, "core-sw", "rack-A", 5, 1, 48);
    infra_cable_connect(&inf, "db01", "eth0", "core-sw", 7);

    char lines[32][128]; int nl = 0;
    infra_switch_show(&inf, "core-sw", lines, &nl, 32);

    ASSERT_GT(nl, 0);
    ASSERT_NE(strstr(lines[0], "core-sw"), NULL);
    /* Doit mentionner le nombre de ports */
    int found_ports = 0;
    for (int i = 0; i < nl; i++)
        if (strstr(lines[i], "48")) { found_ports = 1; break; }
    ASSERT_EQ(found_ports, 1);
    /* Doit lister la connexion db01:eth0 sur port 7 */
    int found_conn = 0;
    for (int i = 0; i < nl; i++)
        if (strstr(lines[i], "db01") && strstr(lines[i], "eth0")) { found_conn = 1; break; }
    ASSERT_EQ(found_conn, 1);
}

TEST(switch_show_unknown) {
    Infra inf = {0};
    char lines[8][128]; int nl = 0;
    infra_switch_show(&inf, "ghost-sw", lines, &nl, 8);
    ASSERT_GT(nl, 0);
    ASSERT_NE(strstr(lines[0], "ghost-sw"), NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    puts("=== test_infra ===");

    puts("-- rack --");
    rack_create_basic();
    rack_create_default_units();
    rack_create_duplicate();
    rack_create_limit();
    rack_find();
    rack_delete_empty();
    rack_delete_nonempty();
    rack_delete_unknown();

    puts("-- server --");
    server_add_basic();
    server_add_slot_zero();
    server_add_slot_negative();
    server_add_slot_overlap();
    server_add_bad_rack();
    server_add_duplicate();
    server_delete_off();
    server_delete_on();
    server_delete_removes_cables();
    server_delete_unknown();

    puts("-- switch --");
    switch_add_basic();
    switch_add_slot_zero();
    switch_slot_overlap_with_server();
    switch_delete_off();
    switch_delete_on();
    switch_delete_removes_cables();
    switch_add_duplicate();
    switch_add_limit();
    switch_delete_unknown();

    puts("-- cables --");
    cable_connect_basic();
    cable_connect_dup_nic();
    cable_connect_unknown_server();
    cable_connect_port_zero();
    cable_connect_port_negative();
    cable_connect_port_exceeds_capacity();
    cable_connect_port_boundary();
    cable_connect_dup_port();
    server_port_unique_after_delete();
    cable_disconnect_basic();
    cable_disconnect_unknown();
    cable_server_nets();
    cable_connect_unknown_switch();
    server_nets_no_cables();

    puts("-- limites --");
    server_add_limit();

    puts("-- server_add_model / minipc --");
    server_add_model_basic();
    minipc_add_basic();
    minipc_add_two_per_slot();
    minipc_add_slot_full();
    minipc_add_bad_rack();
    minipc_add_duplicate();

    puts("-- list_* --");
    list_racks_empty();
    list_racks_basic();
    list_servers_basic();
    list_servers_filtered_by_rack();
    list_switches_basic();
    list_cables_basic();
    list_cables_empty();

    puts("-- rack_render --");
    rack_render_basic();
    rack_render_unknown();
    rack_render_with_minipc();

    puts("-- show --");
    server_show_basic();
    server_show_unknown();
    switch_show_basic();
    switch_show_unknown();

    puts("-- persistance --");
    save_load_roundtrip();

    RESULTS();
}
