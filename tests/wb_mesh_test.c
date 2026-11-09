/*
 * wb_mesh_test.c -- slice 18 gate: server meshing.
 *
 * Proves authority is per-entity and per-component, that it hands over when
 * an entity crosses a zone boundary, and that streaming keeps a server
 * resident in its zone plus the persistent set.
 */
#include "wb_mesh.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int contains(const uint32_t *list, int n, uint32_t v) {
    for (int i = 0; i < n; i++) if (list[i] == v) return 1;
    return 0;
}

static void test_zones(void) {
    wb_mesh_node *m = wb_mesh_new();
    CHECK(m != NULL, "mesh created");

    CHECK(wb_mesh_zone_add(m, 1, (const float[3]){0.0f, 0.0f, 0.0f}, 100.0f) == 0,
          "zone 1 added");
    CHECK(wb_mesh_zone_add(m, 2, (const float[3]){1000.0f, 0.0f, 0.0f}, 100.0f) == 0,
          "zone 2 added");
    CHECK(wb_mesh_zone_count(m) == 2, "two zones registered");

    CHECK(wb_mesh_zone_for_pos(m, (const float[3]){10.0f, 10.0f, 0.0f}) == 1,
          "a position inside zone 1 resolves to server 1");
    CHECK(wb_mesh_zone_for_pos(m, (const float[3]){1005.0f, 0.0f, 0.0f}) == 2,
          "a position inside zone 2 resolves to server 2");
    CHECK(wb_mesh_zone_for_pos(m, (const float[3]){500.0f, 0.0f, 0.0f}) == -1,
          "a position in no zone resolves to nobody");

    wb_mesh_zone_set_active(m, 1, 0);
    CHECK(wb_mesh_zone_for_pos(m, (const float[3]){10.0f, 10.0f, 0.0f}) == -1,
          "a disabled zone does not claim positions");
    wb_mesh_zone_set_active(m, 1, 1);

    wb_mesh_free(m);
}

static void test_authority(void) {
    wb_mesh_node *m = wb_mesh_new();
    wb_mesh_entity_add(m, 1, (const float[3]){0.0f, 0.0f, 0.0f});

    /* A brand-new entity's components are client-owned until a server claims
     * them -- nothing is implicitly authoritative. */
    CHECK(wb_mesh_authority(m, 1, 0) == WB_MESH_CLIENT_AUTH,
          "a new entity's components start client-owned");
    CHECK(wb_mesh_authority(m, 1, 3) == WB_MESH_CLIENT_AUTH,
          "all components of a new entity start client-owned");
    CHECK(wb_mesh_authority(m, 999, 0) == WB_MESH_CLIENT_AUTH,
          "an unknown entity reports client-owned");

    wb_mesh_set_authority(m, 1, 0, 1);
    wb_mesh_set_authority(m, 1, 1, 1);
    wb_mesh_set_authority(m, 1, 2, 1);
    CHECK(wb_mesh_authority(m, 1, 0) == 1, "position authority set to server 1");
    CHECK(wb_mesh_authority(m, 1, 1) == 1, "velocity authority set to server 1");
    CHECK(wb_mesh_authority(m, 1, 3) == WB_MESH_CLIENT_AUTH,
          "an untouched component is still client-owned");

    uint8_t all[WB_MESH_MAX_COMPS];
    CHECK(wb_mesh_entity_authority_all(m, 1, all), "authority snapshot returned");
    CHECK(all[0] == 1 && all[1] == 1 && all[2] == 1 && all[3] == WB_MESH_CLIENT_AUTH,
          "authority snapshot is per-component");

    wb_mesh_free(m);
}

static void test_handover(void) {
    wb_mesh_node *m = wb_mesh_new();
    wb_mesh_entity_add(m, 1, (const float[3]){0.0f, 0.0f, 0.0f});
    wb_mesh_entity_add(m, 2, (const float[3]){1000.0f, 0.0f, 0.0f});
    wb_mesh_set_authority(m, 1, 0, 1);
    wb_mesh_set_authority(m, 1, 1, 1);
    wb_mesh_set_authority(m, 2, 0, 2);

    wb_mesh_auth_change changes[16];
    int n = wb_mesh_handover(m, 1, 1, 2, changes, 16);
    CHECK(n == 2, "handover moved exactly the two components server 1 owned");
    CHECK(wb_mesh_authority(m, 1, 0) == 2 && wb_mesh_authority(m, 1, 1) == 2,
          "the entity's components now belong to server 2");
    CHECK(changes[0].entity == 1 && changes[0].authority == 2,
          "the handover reports the change it made");

    /* Entity 2 was never server 1's, so it must not move. */
    CHECK(wb_mesh_authority(m, 2, 0) == 2, "another server's entity is untouched");

    /* Handing over to the same server is a no-op. */
    CHECK(wb_mesh_handover(m, 1, 2, 2, NULL, 0) == 0,
          "handover to the same server changes nothing");

    /* handover_all moves every component the source owns. */
    wb_mesh_set_authority(m, 1, 3, 1);
    int total = wb_mesh_handover_all(m, 1, 3);
    CHECK(total >= 1, "handover_all moved server 1's remaining components");
    CHECK(wb_mesh_authority(m, 1, 3) == 3, "the remaining component moved to server 3");

    wb_mesh_free(m);
}

static void test_streaming(void) {
    wb_mesh_node *m = wb_mesh_new();
    wb_mesh_zone_add(m, 1, (const float[3]){0.0f, 0.0f, 0.0f}, 100.0f);
    wb_mesh_zone_add(m, 2, (const float[3]){1000.0f, 0.0f, 0.0f}, 100.0f);

    wb_mesh_entity_add(m, 1, (const float[3]){10.0f, 0.0f, 0.0f});      /* zone 1 */
    wb_mesh_entity_add(m, 2, (const float[3]){-20.0f, 0.0f, 0.0f});     /* zone 1 */
    wb_mesh_entity_add(m, 3, (const float[3]){1005.0f, 0.0f, 0.0f});    /* zone 2 */
    wb_mesh_entity_add(m, 4, (const float[3]){5000.0f, 0.0f, 0.0f});    /* nowhere */
    wb_mesh_entity_add(m, 5, (const float[3]){6000.0f, 0.0f, 0.0f});    /* nowhere, persistent */

    wb_mesh_set_persistent(m, 5, 1);
    CHECK(wb_mesh_is_persistent(m, 5), "entity 5 is persistent");
    CHECK(!wb_mesh_is_persistent(m, 1), "entity 1 is not persistent");

    uint32_t resident[64];
    int n = wb_mesh_stream_to(m, 1, resident, 64);
    CHECK(contains(resident, n, 1) && contains(resident, n, 2),
          "server 1 streams the entities in its zone");
    CHECK(!contains(resident, n, 3),
          "server 1 does not stream an entity in another zone");
    CHECK(contains(resident, n, 5),
          "server 1 streams a persistent entity outside its zone");
    CHECK(!contains(resident, n, 4),
          "server 1 does not stream a distant non-persistent entity");

    /* Eviction is streaming's inverse. */
    uint32_t evict[64];
    int ne = wb_mesh_evict_from(m, 1, evict, 64);
    CHECK(contains(evict, ne, 3) && contains(evict, ne, 4),
          "server 1 evicts entities outside its zone");
    CHECK(!contains(evict, ne, 5), "a persistent entity is never evicted");
    CHECK(!contains(evict, ne, 1), "an entity still in the zone is not evicted");

    /* Moving an entity out of a zone makes it evictable. */
    wb_mesh_entity_set_pos(m, 1, (const float[3]){50.0f, 0.0f, 0.0f});
    ne = wb_mesh_evict_from(m, 1, evict, 64);
    CHECK(!contains(evict, ne, 1),
          "an entity still inside the zone is not evictable");

    wb_mesh_entity_set_pos(m, 1, (const float[3]){900.0f, 0.0f, 0.0f});
    ne = wb_mesh_evict_from(m, 1, evict, 64);
    CHECK(contains(evict, ne, 1),
          "an entity moved out of the zone becomes evictable");

    /* (900,0,0) is exactly on zone 2's boundary, so it is server 2's now. */
    CHECK(wb_mesh_owner_for_pos(m, (const float[3]){900.0f, 0.0f, 0.0f}) == 2,
          "the moved entity has crossed into server 2's zone");

    wb_mesh_entity_set_pos(m, 1, (const float[3]){5000.0f, 0.0f, 0.0f});
    CHECK(wb_mesh_owner_for_pos(m, (const float[3]){5000.0f, 0.0f, 0.0f}) == -1,
          "the moved entity is now in no zone");

    wb_mesh_entity_remove(m, 5);
    CHECK(!wb_mesh_is_persistent(m, 5), "a removed entity is gone");

    wb_mesh_free(m);
}

int main(void) {
    test_zones();
    test_authority();
    test_handover();
    test_streaming();
    printf("\n=== MESH TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
