/*
 * wb_net_test.c -- slice 13 gate: the networking core.
 *
 * Proves the multiplayer path end to end over a loopback transport:
 *   - AOI on the quadkey filters what gets replicated
 *   - the server simulates authoritatively
 *   - the client predicts its own movement
 *   - reconciliation is exact when the model matches, and corrects when the
 *     server diverges
 *   - delta compression suppresses unchanged entities
 *   - packet loss does not break convergence
 */
#include "wb_net.h"
#include "../core/wb_spatial.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(float a, float b, float tol) { return fabsf(a - b) <= tol; }

/* New York-ish. 0.001 deg latitude is ~111 m. */
static const double kNY[2] = { -74.0060, 40.7128 };

static wb_net_input mk_input(float mx, float my, float yaw) {
    wb_net_input in;
    memset(&in, 0, sizeof in);
    in.move_x = mx; in.move_y = my; in.yaw = yaw;
    return in;
}

/* ------------------------------------------------------------------ AOI */
static void test_aoi(void) {
    /* level choice: cell size should fall to around the radius */
    int lvl = wb_net_aoi_level(500.0f);
    CHECK(lvl > 0 && lvl <= (int)WB_MAX_LEVEL, "AOI picks a sane quadkey level");

    /* 111 m apart: visible at a 500 m radius */
    double near_[2] = { -74.0060, 40.7128 + 0.001 };
    CHECK(wb_net_aoi_visible(kNY, near_, 500.0f), "nearby entity is inside the AOI");

    /* ~8.5 km apart: not visible at 500 m */
    double far_[2] = { -74.1000, 40.7128 };
    CHECK(!wb_net_aoi_visible(kNY, far_, 500.0f), "distant entity is outside the AOI");

    /* the same point is always visible to itself */
    CHECK(wb_net_aoi_visible(kNY, kNY, 500.0f), "an entity is visible to itself");
}

/* -------------------------------------------------------------- server */
static void test_server_aoi(void) {
    wb_net_server *s = wb_net_server_new(20.0f);
    wb_net_server_set_aoi_radius(s, 500.0f);

    uint32_t me = wb_net_server_add_player(s, 0, kNY, (const float[3]){0, 0, 0});
    CHECK(me != 0, "player entity added");
    CHECK(wb_net_server_entity_count(s) == 1, "one entity on the server");

    double near_[2] = { -74.0060, 40.7128 + 0.001 };
    double far_[2]  = { -74.1000, 40.7128 };
    uint32_t n1 = wb_net_server_add_entity(s, near_, (const float[3]){1, 0, 0});
    uint32_t f1 = wb_net_server_add_entity(s, far_,  (const float[3]){2, 0, 0});
    CHECK(n1 && f1, "near and far entities added");

    /* the server simulates movement from input */
    wb_net_input in = mk_input(1.0f, 0.0f, 0.0f);
    wb_net_server_recv_input(s, 0, &in);
    for (int i = 0; i < 20; i++) wb_net_server_tick(s);   /* 1 second */

    wb_net_ent_state st = wb_net_server_entity(s, me);
    CHECK(nearly(st.pos[0], 5.0f, 0.05f),
          "server moved the player 5 m in one second at 5 m/s");
    CHECK(wb_net_server_tick_count(s) == 20, "server ticked 20 times");

    /* snapshot: own entity + near, never the far one */
    wb_net_snapshot snap;
    wb_net_server_snapshot(s, 0, &snap);
    int found_near = 0, found_far = 0;
    for (int i = 0; i < snap.count; i++) {
        if (snap.ents[i].id == n1) found_near = 1;
        if (snap.ents[i].id == f1) found_far = 1;
    }
    CHECK(found_near, "snapshot includes the nearby entity");
    CHECK(!found_far, "snapshot excludes the distant entity (AOI)");
    CHECK(snap.count >= 1, "snapshot includes the player's own entity");
    CHECK(snap.ack_seq == 0, "snapshot acks the last consumed input");

    wb_net_server_free(s);
}

/* --------------------------------------------------- prediction + reconcile */
static void test_prediction(void) {
    wb_net_server *s = wb_net_server_new(20.0f);
    wb_net_server_set_aoi_radius(s, 500.0f);
    uint32_t me = wb_net_server_add_player(s, 0, kNY, (const float[3]){0, 0, 0});

    wb_net_client *c = wb_net_client_new(me, 500.0f);
    wb_net_loopback *lo = wb_net_loopback_new(0.0f);
    wb_net_loopback_connect(lo, 0, s, c);

    /* 1 second of holding "forward" at 20 Hz */
    for (int i = 0; i < 20; i++) {
        wb_net_input in = mk_input(1.0f, 0.0f, 0.0f);
        wb_net_client_apply_input(c, &in, 1.0f / 20.0f);  /* predict + queue */
        wb_net_loopback_deliver(lo, 0);                   /* server receives */
        wb_net_server_tick(s);                            /* server simulates */
        wb_net_loopback_snapshot(lo, 0);                  /* client reconciles */
    }

    wb_net_ent_state pred, srv;
    wb_net_client_predicted(c, &pred);
    srv = wb_net_server_entity(s, me);

    CHECK(nearly(pred.pos[0], 5.0f, 0.05f), "client predicted 5 m of travel");
    CHECK(nearly(pred.pos[0], srv.pos[0], 0.01f) &&
          nearly(pred.pos[1], srv.pos[1], 0.01f),
          "prediction matches the authoritative state");
    CHECK(wb_net_client_corrections(c) == 0,
          "no corrections when the client model matches the server");
    CHECK(wb_net_client_last_error(c) < 0.001f, "prediction error is ~0");
    CHECK(wb_net_client_pending_inputs(c) == 0, "all inputs acked");

    wb_net_loopback_free(lo);
    wb_net_client_free(c);
    wb_net_server_free(s);
}

/* ---------------------------------------------------------- correction */
static void test_correction(void) {
    wb_net_server *s = wb_net_server_new(20.0f);
    wb_net_server_set_aoi_radius(s, 500.0f);
    uint32_t me = wb_net_server_add_player(s, 0, kNY, (const float[3]){0, 0, 0});

    wb_net_client *c = wb_net_client_new(me, 500.0f);
    wb_net_loopback *lo = wb_net_loopback_new(0.0f);
    wb_net_loopback_connect(lo, 0, s, c);

    for (int i = 0; i < 10; i++) {
        wb_net_input in = mk_input(1.0f, 0.0f, 0.0f);
        wb_net_client_apply_input(c, &in, 1.0f / 20.0f);
        wb_net_loopback_deliver(lo, 0);
        wb_net_server_tick(s);
        wb_net_loopback_snapshot(lo, 0);
    }
    CHECK(wb_net_client_corrections(c) == 0, "no corrections before the divergence");

    /* The server applies an authoritative displacement (knockback, a hit, a
     * teleporter). The client has no idea, so its prediction is now wrong. */
    wb_net_server_apply_impulse(s, me, 3.0f, 0.0f);

    /* A few ticks with no input: the server's authority reaches the client. */
    for (int i = 0; i < 5; i++) {
        wb_net_input in = mk_input(0.0f, 0.0f, 0.0f);
        wb_net_client_apply_input(c, &in, 1.0f / 20.0f);
        wb_net_loopback_deliver(lo, 0);
        wb_net_server_tick(s);
        wb_net_loopback_snapshot(lo, 0);
    }

    /* The client must have corrected onto the server's position. */
    wb_net_ent_state pred, srv;
    wb_net_client_predicted(c, &pred);
    srv = wb_net_server_entity(s, me);
    CHECK(wb_net_client_corrections(c) > 0, "a correction was applied");
    CHECK(nearly(pred.pos[0], srv.pos[0], 0.05f) &&
          nearly(pred.pos[1], srv.pos[1], 0.05f),
          "client converged back to the authoritative state");
    CHECK(nearly(pred.pos[0], 2.5f + 3.0f, 0.1f),
          "the correction landed on the knocked-back position");

    wb_net_loopback_free(lo);
    wb_net_client_free(c);
    wb_net_server_free(s);
}

/* --------------------------------------------------------- packet loss */
static void test_packet_loss(void) {
    wb_net_server *s = wb_net_server_new(20.0f);
    wb_net_server_set_aoi_radius(s, 500.0f);
    uint32_t me = wb_net_server_add_player(s, 0, kNY, (const float[3]){0, 0, 0});

    wb_net_client *c = wb_net_client_new(me, 500.0f);
    wb_net_loopback *lo = wb_net_loopback_new(0.5f);   /* half of all packets drop */
    wb_net_loopback_connect(lo, 0, s, c);

    for (int i = 0; i < 200; i++) {
        wb_net_input in = mk_input(1.0f, 0.0f, 0.0f);
        wb_net_client_apply_input(c, &in, 1.0f / 20.0f);
        wb_net_loopback_deliver(lo, 0);
        wb_net_server_tick(s);
        wb_net_loopback_snapshot(lo, 0);
    }

    wb_net_ent_state pred, srv;
    wb_net_client_predicted(c, &pred);
    srv = wb_net_server_entity(s, me);
    float err = sqrtf((pred.pos[0]-srv.pos[0])*(pred.pos[0]-srv.pos[0]) +
                      (pred.pos[1]-srv.pos[1])*(pred.pos[1]-srv.pos[1]));
    CHECK(err < 1.0f, "client stays within 1 m of the server under 50% loss");
    CHECK(wb_net_client_corrections(c) > 0, "corrections happened under loss");

    wb_net_loopback_free(lo);
    wb_net_client_free(c);
    wb_net_server_free(s);
}

/* ------------------------------------------------------ delta compression */
static void test_delta(void) {
    wb_net_server *s = wb_net_server_new(20.0f);
    wb_net_server_set_aoi_radius(s, 500.0f);
    wb_net_server_add_player(s, 0, kNY, (const float[3]){0, 0, 0});
    double near_[2] = { -74.0060, 40.7128 + 0.001 };
    uint32_t other = wb_net_server_add_player(s, 1, near_, (const float[3]){0, 0, 0});

    wb_net_snapshot a, b, cc;
    wb_net_server_snapshot(s, 0, &a);
    CHECK(a.count == 2, "first snapshot sends both entities");

    /* nothing moved: the other entity is suppressed by delta compression */
    wb_net_server_snapshot(s, 0, &b);
    int found_other = 0;
    for (int i = 0; i < b.count; i++) if (b.ents[i].id == other) found_other = 1;
    CHECK(!found_other, "unchanged entity is not re-sent (delta compression)");
    CHECK(b.count == 1, "only the client's own entity remains");

    /* move the other entity: it must come back */
    wb_net_input in = mk_input(1.0f, 0.0f, 0.0f);
    wb_net_server_recv_input(s, 1, &in);
    wb_net_server_tick(s);
    wb_net_server_snapshot(s, 0, &cc);
    found_other = 0;
    for (int i = 0; i < cc.count; i++) if (cc.ents[i].id == other) found_other = 1;
    CHECK(found_other, "a moved entity is replicated again");

    wb_net_server_free(s);
}

int main(void) {
    test_aoi();
    test_server_aoi();
    test_prediction();
    test_correction();
    test_packet_loss();
    test_delta();
    printf("\n=== NET TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
