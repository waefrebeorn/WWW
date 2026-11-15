/*
 * wb_net_udp.c -- the UDP socket transport. See wb_net_udp.h.
 */
#include "wb_net_udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct {
    int              used;
    int              id;
    struct sockaddr_in addr;
} wb_udp_peer;

struct wb_net_udp {
    wb_net_transport vt;
    SOCKET           sock;
    uint16_t         port;
    wb_udp_peer      peers[WB_NET_MAX_PEERS];
};

/* Winsock is brought up once per process. The engine's netcode is single-
 * threaded at this layer; a later job-driven host would guard this. */
static int udp_winsock_up(char *err, size_t errn) {
    static int      ready = 0;
    static WSADATA  wsa;
    if (ready) return 0;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        snprintf(err, errn, "WSAStartup failed (%d)", WSAGetLastError());
        return -1;
    }
    ready = 1;
    return 0;
}

static int udp_send(wb_net_transport *t, int peer, const wb_net_datagram *d) {
    wb_net_udp *u = (wb_net_udp *)t;
    if (!u || !d || u->sock == INVALID_SOCKET) return -1;
    if (peer < 0 || peer >= WB_NET_MAX_PEERS || !u->peers[peer].used) return -1;
    if (d->len == 0 || d->len > WB_NET_MAX_WIRE) return -1;

    int sent = sendto(u->sock, (const char *)d->data, d->len, 0,
                      (const struct sockaddr *)&u->peers[peer].addr,
                      (int)sizeof u->peers[peer].addr);
    if (sent == SOCKET_ERROR) {
        int e = WSAGetLastError();
        /* A full socket buffer is congestion, not a failure: drop it. The
         * reliability layer decides whether that matters. */
        if (e == WSAEWOULDBLOCK) return 0;
        return -1;
    }
    return 0;
}

static int udp_peer_from_addr(wb_net_udp *u, const struct sockaddr_in *from) {
    for (int i = 0; i < WB_NET_MAX_PEERS; i++) {
        if (!u->peers[i].used) continue;
        if (u->peers[i].addr.sin_addr.s_addr == from->sin_addr.s_addr &&
            u->peers[i].addr.sin_port == from->sin_port) return i;
    }
    return -1;
}

static int udp_poll(wb_net_transport *t, wb_net_recv_fn cb, void *ud) {
    wb_net_udp *u = (wb_net_udp *)t;
    if (!u || u->sock == INVALID_SOCKET) return 0;
    int delivered = 0;
    for (;;) {
        wb_net_datagram d;
        struct sockaddr_in from;
        int fromlen = (int)sizeof from;
        int got = recvfrom(u->sock, (char *)d.data, (int)sizeof d.data, 0,
                           (struct sockaddr *)&from, &fromlen);
        if (got == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) break;     /* drained */
            /* A previous send hit a closed port; the error is consumed and the
             * next recvfrom reports the real state. */
            if (e == WSAECONNRESET) continue;
            break;
        }
        if (got == 0) continue;
        /* A datagram from an unaddressed peer is noise: drop it. Addressing is
         * explicit, so a peer that was never named cannot inject traffic. */
        int peer = udp_peer_from_addr(u, &from);
        if (peer < 0) continue;
        d.len = (uint16_t)got;
        if (cb) cb(ud, peer, &d);
        delivered++;
    }
    return delivered;
}

static void udp_tick(wb_net_transport *t, double dt) {
    (void)t;
    (void)dt;                   /* nothing timer-driven in a plain UDP endpoint */
}

static void udp_destroy(wb_net_transport *t) {
    wb_net_udp *u = (wb_net_udp *)t;
    if (!u) return;
    if (u->sock != INVALID_SOCKET) closesocket(u->sock);
    free(u);
}

wb_net_udp *wb_net_udp_new(uint16_t port, char *err, size_t errn) {
    if (udp_winsock_up(err, errn) != 0) return NULL;

    wb_net_udp *u = (wb_net_udp *)calloc(1, sizeof *u);
    if (!u) {
        snprintf(err, errn, "out of memory");
        return NULL;
    }
    u->sock = INVALID_SOCKET;
    u->vt.send = udp_send;
    u->vt.poll = udp_poll;
    u->vt.tick = udp_tick;
    u->vt.destroy = udp_destroy;
    u->vt.impl = u;

    u->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (u->sock == INVALID_SOCKET) {
        snprintf(err, errn, "socket failed (%d)", WSAGetLastError());
        free(u);
        return NULL;
    }
    /* Non-blocking: poll drains, it never waits. */
    u_long nb = 1;
    ioctlsocket(u->sock, FIONBIO, &nb);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(u->sock, (const struct sockaddr *)&a, (int)sizeof a) != 0) {
        snprintf(err, errn, "bind failed (%d)", WSAGetLastError());
        closesocket(u->sock);
        free(u);
        return NULL;
    }
    struct sockaddr_in bound;
    int blen = (int)sizeof bound;
    getsockname(u->sock, (struct sockaddr *)&bound, &blen);
    u->port = ntohs(bound.sin_port);
    return u;
}

void wb_net_udp_destroy(wb_net_udp *u) {
    if (u) u->vt.destroy((wb_net_transport *)u);
}

uint16_t wb_net_udp_bound_port(const wb_net_udp *u) {
    return u ? u->port : 0;
}

int wb_net_udp_add_peer(wb_net_udp *u, int peer, const char *host, uint16_t port) {
    if (!u || !host || port == 0) return -1;
    if (peer < 0 || peer >= WB_NET_MAX_PEERS) return -1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char portbuf[16];
    snprintf(portbuf, sizeof portbuf, "%u", (unsigned)port);
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0 || !res) return -1;

    wb_udp_peer *p = &u->peers[peer];
    memset(p, 0, sizeof *p);
    memcpy(&p->addr, res->ai_addr, sizeof p->addr);
    p->addr.sin_family = AF_INET;
    p->addr.sin_port = htons(port);
    p->used = 1;
    p->id = peer;
    freeaddrinfo(res);
    return 0;
}

void wb_net_udp_clear_peers(wb_net_udp *u) {
    if (!u) return;
    memset(u->peers, 0, sizeof u->peers);
}
