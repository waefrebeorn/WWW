/*
 * wb_dist_test.c -- slice 16 gate: experience distribution.
 *
 * Proves the "world space and distribution system": instances hold players
 * up to capacity, the directory lists them, matchmaking picks the best one,
 * and a portal in world space carries a player in.
 */
#include "wb_dist.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(float a, float b, float tol) { return fabsf(a - b) <= tol; }

/* ------------------------------------------------------------ instances */
static void test_instance(void) {
    wb_dist_instance *in = wb_dist_instance_new("storage-wars", 2, "us-east");
    CHECK(in != NULL, "instance created");
    CHECK(wb_dist_instance_id(in)[0] != 0, "instance has a generated id");
    {
        wb_dist_info gi;
        wb_dist_instance_info(in, &gi);
        CHECK(!strcmp(gi.gamemode, "storage-wars"), "gamemode recorded");
    }
    CHECK(wb_dist_instance_capacity(in) == 2, "capacity recorded");
    CHECK(wb_dist_instance_state(in) == WB_DIST_WAITING, "new instance is waiting");
    CHECK(wb_dist_instance_player_count(in) == 0, "new instance is empty");

    CHECK(wb_dist_instance_join(in, "alice") == 1, "first join succeeds");
    CHECK(wb_dist_instance_join(in, "bob") == 1, "second join succeeds");
    CHECK(wb_dist_instance_join(in, "carol") == -1, "join past capacity is refused");
    CHECK(wb_dist_instance_is_full(in), "instance reports full");

    /* joining twice is idempotent, not a duplicate */
    CHECK(wb_dist_instance_join(in, "alice") == 0, "re-joining is idempotent");
    CHECK(wb_dist_instance_player_count(in) == 2, "no duplicate player");

    CHECK(wb_dist_instance_has_player(in, "bob"), "player lookup works");
    CHECK(!wb_dist_instance_has_player(in, "dave"), "absent player not found");

    CHECK(wb_dist_instance_leave(in, "alice") == 1, "leave succeeds");
    CHECK(wb_dist_instance_leave(in, "alice") == 0, "leaving twice is a no-op");
    CHECK(wb_dist_instance_player_count(in) == 1, "player count after leave");

    wb_dist_info info;
    wb_dist_instance_info(in, &info);
    CHECK(!strcmp(info.id, wb_dist_instance_id(in)) && !strcmp(info.gamemode, "storage-wars"),
          "directory info carries id and gamemode");
    CHECK(info.players == 1 && info.capacity == 2, "directory info carries counts");

    wb_dist_instance_set_state(in, WB_DIST_PLAYING);
    CHECK(wb_dist_instance_state(in) == WB_DIST_PLAYING, "state transitions work");
    wb_dist_instance_set_ping(in, 42);
    wb_dist_instance_info(in, &info);
    CHECK(info.ping_ms == 42, "ping recorded");

    wb_dist_instance_free(in);
}

/* ------------------------------------------------------------- registry */
static void test_registry(void) {
    wb_dist_registry *r = wb_dist_registry_new();
    CHECK(r != NULL, "registry created");
    CHECK(wb_dist_registry_count(r) == 0, "registry starts empty");

    wb_dist_instance *a = wb_dist_instance_new("obby", 10, "us-east");
    wb_dist_instance *b = wb_dist_instance_new("obby", 10, "eu-west");
    wb_dist_instance *c = wb_dist_instance_new("tycoon", 10, "us-east");
    wb_dist_registry_add(r, a);
    wb_dist_registry_add(r, b);
    wb_dist_registry_add(r, c);
    CHECK(wb_dist_registry_count(r) == 3, "three instances registered");

    CHECK(wb_dist_registry_find(r, wb_dist_instance_id(a)) == a, "find by id works");
    CHECK(wb_dist_registry_find(r, "nope") == NULL, "unknown id is not found");

    wb_dist_info list[8];
    int n = wb_dist_registry_list(r, list, 8);
    CHECK(n == 3, "directory lists all instances");
    CHECK(!strcmp(list[0].id, wb_dist_instance_id(a)) && !strcmp(list[2].gamemode, "tycoon"),
          "directory entries carry their data");

    wb_dist_registry_remove(r, wb_dist_instance_id(b));
    CHECK(wb_dist_registry_count(r) == 2, "removal shrinks the registry");
    CHECK(wb_dist_registry_find(r, wb_dist_instance_id(b)) == NULL, "removed instance is gone");

    wb_dist_registry_free(r);
    /* `b` was already freed by wb_dist_registry_remove (it owns its
     * instances), so it must not be freed here. */
}

/* ---------------------------------------------------------- matchmaking */
static void test_matchmaking(void) {
    wb_dist_registry *r = wb_dist_registry_new();

    /* A full waiting instance, a roomy waiting one, and a playing one. */
    wb_dist_instance *full = wb_dist_instance_new("obby", 2, "us-east");
    wb_dist_instance_join(full, "p1");
    wb_dist_instance_join(full, "p2");

    wb_dist_instance *roomy = wb_dist_instance_new("obby", 10, "us-east");
    wb_dist_instance_join(roomy, "p3");

    wb_dist_instance *playing = wb_dist_instance_new("obby", 10, "eu-west");
    wb_dist_instance_set_state(playing, WB_DIST_PLAYING);
    wb_dist_instance_join(playing, "p4");
    wb_dist_instance_join(playing, "p5");
    wb_dist_instance_join(playing, "p6");

    wb_dist_instance *other = wb_dist_instance_new("tycoon", 10, "us-east");

    wb_dist_registry_add(r, full);
    wb_dist_registry_add(r, roomy);
    wb_dist_registry_add(r, playing);
    wb_dist_registry_add(r, other);

    wb_dist_instance *m = wb_dist_match(r, "obby");
    CHECK(m == roomy, "matchmaking skips a full instance and picks a waiting one");

    /* A waiting instance always beats a playing one with room. */
    CHECK(m != playing, "matchmaking prefers waiting over playing");

    /* The wrong gamemode is never matched. */
    CHECK(wb_dist_match(r, "racing") == NULL, "no match for a gamemode with no instances");

    /* An ended instance is not matchable. */
    wb_dist_instance_set_state(roomy, WB_DIST_ENDED);
    m = wb_dist_match(r, "obby");
    CHECK(m == playing, "an ended instance is skipped, a playing one is used");

    /* join_best puts the player in and reports the id. */
    char id[WB_DIST_ID_LEN] = {0};
    wb_dist_instance *joined = wb_dist_join_best(r, "obby", "newbie", id);
    CHECK(joined == playing, "join_best uses the matched instance");
    CHECK(!strcmp(id, wb_dist_instance_id(playing)), "join_best reports the instance id");
    CHECK(wb_dist_instance_has_player(playing, "newbie"), "the player really joined");

    wb_dist_registry_free(r);
}

/* -------------------------------------------------------------- portals */
static void test_portals(void) {
    wb_portal p;
    wb_portal_init(&p);
    float pos[3] = { 10.0f, 0.0f, 0.0f };
    memcpy(p.pos, pos, sizeof pos);
    snprintf(p.target, sizeof p.target, "exp-1");

    CHECK(wb_portal_check(&p, (const float[3]){11.0f, 0.0f, 0.0f}),
          "a player inside the portal radius triggers it");
    CHECK(!wb_portal_check(&p, (const float[3]){20.0f, 0.0f, 0.0f}),
          "a player outside the portal radius does not");
    CHECK(nearly(p.radius, 2.0f, 1e-6), "the default portal radius is sane");

    p.active = 0;
    CHECK(!wb_portal_check(&p, (const float[3]){10.5f, 0.0f, 0.0f}),
          "an inactive portal never triggers");
}

int main(void) {
    test_instance();
    test_registry();
    test_matchmaking();
    test_portals();
    printf("\n=== DIST TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
