/*
 * wb_net_lockstep_test.c -- LP-87's last piece: the lockstep frame-stepper.
 *
 * Proves the lock property and the detection:
 *   - two peers over a 30% lossy link step the SAME ticks, in lock, and end in
 *     the same state
 *   - a peer that has not submitted its input STALLS the other: tick T does not
 *     happen for anyone until both inputs exist
 *   - when one peer's digests disagree from tick 5, both peers report the desync
 *     at tick 5 -- the whole point of stepping in lock
 */
#include "wb_net_lockstep.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* ------------------------------------------------------------------ mock sim */

typedef struct {
    uint32_t tick;
    float    pos[3];
    uint64_t h;
    int      steps;
    uint32_t last_tick;
    wb_net_input last_in[2];
    int      digest_xor;          /* report a wrong digest ... */
    uint32_t xor_from;            /* ... from this tick on */
} sim;

static void sim_step(void *ud, uint32_t tick, const wb_net_input *in, int nin) {
    sim *s = (sim *)ud;
    for (int i = 0; i < nin; i++) {
        s->pos[0] += in[i].move_x * 0.1f;
        s->pos[1] += in[i].move_y * 0.1f;
    }
    s->h = s->h * 1000003u + (uint32_t)(s->pos[0] * 1000.0f)
                             + (uint32_t)(s->pos[1] * 1000.0f) + tick;
    s->tick = tick;
    s->steps++;
    s->last_tick = tick;
    if (nin == 2) {
        s->last_in[0] = in[0];
        s->last_in[1] = in[1];
    }
}

static uint64_t sim_digest(const sim *s, uint32_t tick) {
    uint64_t d = s->h;
    if (s->digest_xor && tick >= s->xor_from) d ^= 0x5A5A5A5A5A5A5A5Aull;
    return d;
}

static wb_net_input mk_input(uint32_t tick, int peer) {
    wb_net_input in;
    memset(&in, 0, sizeof in);
    in.tick   = tick;
    in.move_x = (peer == 0 ? 1.0f : -1.0f) * (float)((tick % 7) - 3) * 0.25f;
    in.move_y = (float)((tick % 5) - 2) * 0.5f;
    in.yaw    = (float)tick * 0.1f;
    in.jump   = (tick % 11) == 0;
    return in;
}

/* ------------------------------------------------------------------- harness */

typedef struct {
    wb_net_transport *ta, *tb;
    wb_net_reliable  *ra, *rb;
    wb_net_lockstep  *la, *lb;
    sim sa, sb;
} link;

static void link_open(link *l, float loss) {
    memset(l, 0, sizeof *l);
    l->ta = wb_net_transport_pair_new(loss, 0.0, &l->tb);
    l->ra = wb_net_reliable_new(l->ta, 0.05);
    l->rb = wb_net_reliable_new(l->tb, 0.05);
    l->la = wb_net_lockstep_new(0, 20.0f, l->ra);
    l->lb = wb_net_lockstep_new(1, 20.0f, l->rb);
}

static void link_close(link *l) {
    wb_net_lockstep_free(l->la);
    wb_net_lockstep_free(l->lb);
    wb_net_reliable_destroy(l->ra);
    wb_net_reliable_destroy(l->rb);
    l->ta->destroy(l->ta);
    l->tb->destroy(l->tb);
}

static void pump(link *l, int n) {
    for (int i = 0; i < n; i++) {
        wb_net_reliable_tick(l->ra, 0.01);
        wb_net_reliable_tick(l->rb, 0.01);
        wb_net_lockstep_poll(l->la);
        wb_net_lockstep_poll(l->lb);
    }
}

/* ------------------------------------------------------- lockstep in lock */

static void test_lockstep(void) {
    enum { N = 200 };
    link l;
    link_open(&l, 0.3f);            /* lossy: the inputs go over REL */
    CHECK(l.la && l.lb, "both peers open a lockstep link");

    for (uint32_t t = 0; t < N; t++) {
        wb_net_input ia = mk_input(t, 0);
        wb_net_input ib = mk_input(t, 1);
        CHECK(wb_net_lockstep_submit(l.la, t, &ia) == 0, "peer 0 submits");
        CHECK(wb_net_lockstep_submit(l.lb, t, &ib) == 0, "peer 1 submits");
        /* A real host pumps its link every frame and steps whatever tick is
         * ready; doing it in one coarse burst would hide the retransmission
         * timing this gate exists to exercise. */
        for (int guard = 0; guard < 400; guard++) {
            pump(&l, 1);
            wb_net_lockstep_step(l.la, sim_step, &l.sa);
            wb_net_lockstep_step(l.lb, sim_step, &l.sb);
            if (wb_net_lockstep_tick(l.la) == t + 1 &&
                wb_net_lockstep_tick(l.lb) == t + 1) break;
        }
        CHECK(wb_net_lockstep_tick(l.la) == t + 1, "peer 0 kept up");
        CHECK(wb_net_lockstep_tick(l.lb) == t + 1, "peer 1 kept up");
        wb_net_lockstep_digest(l.la, t, sim_digest(&l.sa, t));
        wb_net_lockstep_digest(l.lb, t, sim_digest(&l.sb, t));
        pump(&l, 5);
    }

    CHECK(wb_net_lockstep_tick(l.la) == N, "peer 0 stepped every tick");
    CHECK(wb_net_lockstep_tick(l.lb) == N, "peer 1 stepped every tick");
    CHECK(l.sa.steps == N && l.sb.steps == N, "both sims stepped N times");
    CHECK(l.sa.h == l.sb.h, "the two sims ended in the same state");
    CHECK(l.sa.pos[0] == l.sb.pos[0] && l.sa.pos[1] == l.sb.pos[1],
          "the two sims ended at the same position");
    uint32_t dt = 0;
    CHECK(!wb_net_lockstep_desynced(l.la, &dt), "peer 0 sees no desync");
    CHECK(!wb_net_lockstep_desynced(l.lb, &dt), "peer 1 sees no desync");
    CHECK(wb_net_lockstep_dt(l.la) == 1.0f / 20.0f, "the fixed step is 1/tick_rate");

    /* What peer 0 saw as peer 1's last input is what peer 1 submitted. */
    wb_net_input want = mk_input(N - 1, 1);
    CHECK(l.sa.last_in[1].tick == want.tick &&
          l.sa.last_in[1].move_x == want.move_x &&
          l.sa.last_in[1].jump == want.jump,
          "peer 0 received peer 1's actual input");

    link_close(&l);
}

/* ---------------------------------------------------------- the stall */

static void test_stall(void) {
    enum { N = 10, LATE = 5 };
    link l;
    link_open(&l, 0.0f);

    /* Peer 0 submits everything up front; peer 1 only the first LATE ticks. */
    for (uint32_t t = 0; t < N; t++) {
        wb_net_input ia = mk_input(t, 0);
        CHECK(wb_net_lockstep_submit(l.la, t, &ia) == 0, "peer 0 submits ahead");
    }
    for (uint32_t t = 0; t < LATE; t++) {
        wb_net_input ib = mk_input(t, 1);
        CHECK(wb_net_lockstep_submit(l.lb, t, &ib) == 0, "peer 1 submits");
    }
    pump(&l, 40);

    int stepped = 0;
    for (uint32_t t = 0; t < LATE; t++)
        if (wb_net_lockstep_step(l.la, sim_step, &l.sa) == 1) stepped++;
    CHECK(stepped == LATE, "the shared ticks step");
    CHECK(wb_net_lockstep_step(l.la, sim_step, &l.sa) == 0,
          "tick 5 does not happen without peer 1's input");
    CHECK(wb_net_lockstep_stalled(l.la) > 0, "the stall is counted");
    CHECK(wb_net_lockstep_tick(l.la) == LATE, "peer 0 is still at tick 5");

    /* Peer 1 catches up; the lock resumes exactly where it stopped. */
    for (uint32_t t = LATE; t < N; t++) {
        wb_net_input ib = mk_input(t, 1);
        wb_net_lockstep_submit(l.lb, t, &ib);
    }
    pump(&l, 40);
    stepped = 0;
    for (uint32_t t = LATE; t < N; t++)
        if (wb_net_lockstep_step(l.la, sim_step, &l.sa) == 1) stepped++;
    CHECK(stepped == (int)(N - LATE), "the lock resumes");
    CHECK(wb_net_lockstep_tick(l.la) == N, "peer 0 reaches tick N");

    link_close(&l);
}

/* ------------------------------------------------------- the desync */

static void test_desync(void) {
    enum { N = 20, FROM = 5 };
    link l;
    link_open(&l, 0.0f);
    l.sb.digest_xor = 1;            /* peer 1 reports a wrong digest ... */
    l.sb.xor_from = FROM;           /* ... from tick 5 on */

    for (uint32_t t = 0; t < N; t++) {
        wb_net_input ia = mk_input(t, 0);
        wb_net_input ib = mk_input(t, 1);
        wb_net_lockstep_submit(l.la, t, &ia);
        wb_net_lockstep_submit(l.lb, t, &ib);
        pump(&l, 40);
        wb_net_lockstep_step(l.la, sim_step, &l.sa);
        wb_net_lockstep_step(l.lb, sim_step, &l.sb);
        wb_net_lockstep_digest(l.la, t, sim_digest(&l.sa, t));
        wb_net_lockstep_digest(l.lb, t, sim_digest(&l.sb, t));
        pump(&l, 40);
    }

    uint32_t ta = 0, tb = 0;
    CHECK(wb_net_lockstep_desynced(l.la, &ta), "peer 0 detects the desync");
    CHECK(wb_net_lockstep_desynced(l.lb, &tb), "peer 1 detects the desync");
    CHECK(ta == FROM, "peer 0 localizes it to tick 5");
    CHECK(tb == FROM, "peer 1 localizes it to tick 5");
    CHECK(wb_net_lockstep_tick(l.la) == N && wb_net_lockstep_tick(l.lb) == N,
          "both peers still stepped every tick -- the desync did not stall the lock");

    link_close(&l);
}

static void test_refusals(void) {
    link l;
    link_open(&l, 0.0f);
    CHECK(wb_net_lockstep_new(2, 20.0f, l.ra) == NULL, "a peer id of 2 is refused");
    CHECK(wb_net_lockstep_new(-1, 20.0f, l.ra) == NULL, "a peer id of -1 is refused");
    CHECK(wb_net_lockstep_new(0, 0.0f, l.ra) == NULL, "a zero tick rate is refused");
    CHECK(wb_net_lockstep_new(0, 20.0f, NULL) == NULL, "a missing link is refused");

    wb_net_input in = mk_input(0, 0);
    CHECK(wb_net_lockstep_submit(l.la, WB_LOCKSTEP_WINDOW, &in) == -1,
          "a tick past the window is refused");
    CHECK(wb_net_lockstep_submit(l.la, 0, &in) == 0, "tick 0 is accepted");

    /* Step tick 0 in lock, then prove the past cannot be resubmitted. */
    wb_net_input ib0 = mk_input(0, 1);
    CHECK(wb_net_lockstep_submit(l.lb, 0, &ib0) == 0, "peer 1 submits tick 0");
    pump(&l, 40);
    CHECK(wb_net_lockstep_step(l.la, sim_step, &l.sa) == 1,
          "tick 0 steps once both inputs exist");
    CHECK(wb_net_lockstep_submit(l.la, 0, &in) == -1,
          "a tick already stepped is refused");

    link_close(&l);
}

int main(void) {
    test_lockstep();
    test_stall();
    test_desync();
    test_refusals();

    printf("\n=== NET LOCKSTEP TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
