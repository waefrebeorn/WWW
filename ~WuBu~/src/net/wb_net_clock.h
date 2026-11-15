/*
 * wb_net_clock.h -- LP-96: clock synchronisation, the time base the netcode
 * otherwise assumes.
 *
 * Lag compensation, rollback and replays all compare a local event time with a
 * remote one. Without a shared base those comparisons are wrong by the offset
 * between the two clocks, which on a real machine is seconds and drifts.
 *
 * The estimator is NTP's symmetric-delay one: a sync exchange gives four
 * timestamps (local send t0, remote receive t1, remote send t2, local receive
 * t3) and
 *
 *     offset = ((t1 - t0) + (t2 - t3)) / 2
 *     delay  = (t3 - t0) - (t2 - t1)
 *
 * The offset formula cancels the path delay only if the two directions are
 * symmetric. They usually are not, so the estimate is refined by selection:
 * of the last few samples, the one with the smallest round trip is the closest
 * to symmetric, and its offset is the one used. That is NTP's minimum-delay
 * peer selection, reduced to what a game client needs.
 *
 * Drift is the rate at which the offset changes, estimated from successive
 * accepted offsets and smoothed. `wb_net_clock_remote` applies both, so a
 * caller that polls occasionally still gets a correct remote time between
 * syncs.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_CLOCK_H
#define WB_NET_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_net_clock wb_net_clock;

wb_net_clock *wb_net_clock_new(void);
void wb_net_clock_free(wb_net_clock *c);

/* Record one sync exchange. t0..t3 are as above, on this host's clock. Returns
 * the raw offset for this exchange, or NaN if the exchange is impossible
 * (negative delay, non-finite timestamps). */
double wb_net_clock_sample(wb_net_clock *c, double t0, double t1,
                           double t2, double t3);

/* This host's estimate of the remote clock at local time `local_now`. Valid
 * only after at least one accepted sample. */
double wb_net_clock_remote(const wb_net_clock *c, double local_now);

/* The current offset (remote - local), the smoothed drift in remote seconds
 * per local second, and the round trip of the sample the offset came from. */
double wb_net_clock_offset(const wb_net_clock *c);
double wb_net_clock_drift(const wb_net_clock *c);
double wb_net_clock_delay(const wb_net_clock *c);

int wb_net_clock_samples(const wb_net_clock *c);
int wb_net_clock_ready(const wb_net_clock *c);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_CLOCK_H */
