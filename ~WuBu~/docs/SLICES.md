# Slices 9–53 — kernel → meta engine → the gametype host → audio → interpolation → replay → fuzzing → transport → UDP → lockstep → AES-GCM → X25519 → HKDF → the sealed channel → fragmentation → clock sync → QoS → netem → STUN → ICE → TURN → signalling → QUIC → QUIC packet protection → the packet number space → the ACK tracker → the connection's three spaces → a packet on the wire → the short header and 1-RTT → the packet number as the receiver sees it → the ACK round trip → the paths joined → a datagram of several packets → path validation and migration

Status tracker for the roadmap in `docs/meta-engine-assessment.md`.
The hop-by-hop backlog — 113 pads in dependency order, each with its sources —
is `docs/LILYPADS.md`. The same backlog turned into an ordered, status-tracked
execution list is `docs/WORKLIST.md`.
Each slice ships real, tested code; nothing is marked done without a green gate.

| Slice | Pillar | Deliverable | Gate | Status |
|---|---|---|---|---|
| 9 | physics | MuJoCo 4 backend: bodies, collision, character controller, ECS binding | `make test-physics` — 27 | ✅ `7a56adf` |
| 10 | input + camera | input state, follow camera, input→character chain, headless playable demo | `make test-character` | ✅ |
| 11 | GPU backend | render graph + GPU backend behind the software rasterizer's geometry API | `make test-gpu` — 26 | ✅ |
| 12 | content pipeline | glTF/GLB import → GPU-ready, content-addressed DDC, manifest | `make test-assets` — 30 | ✅ |
| 13 | networking | authoritative server, client prediction, AOI on the quadkey | `make test-net` — 28 | ✅ |
| 14 | avatar + identity | glTF+VRM humanoid import, rig, cross-world identity | `make test-avatar` — 32 | ✅ |
| 15 | modding sandbox | untrusted-code sandbox (Luau model), mod manifest, hot reload | `make test-mod` — 21 | ✅ |
| 16 | experience distribution | portals/stadiums, instance matchmaking, discovery hooks | `make test-dist` — 41 | ✅ |
| 17 | editor | in-engine world editing + live edit | `make test-edit` — 37 | ✅ |
| 18 | server meshing | authority handover, per-component authority, persistent entity streaming | `make test-mesh` — 37 | ✅ |
| 19 | gametypes | mode manifests + typed config schema, spawn points, the spawn-a-worldspace facade | `make test-mode` — 42 · `make test-spawn` — 37 · `make test-worldspace` — 53 | ✅ |
| 20 | stadium travel | Yu-Gi-Oh portals: walk into a gate, leave every world, spawn into another gametype's instance at a tagged landing, same identity, empty source reaped | `make test-portal` — 22 | ✅ |
| 21 | gamemode-facing space | the stadium layer exposed to scripts: `wb.add_gate` / `gate_at` / `travel` / `instances` from inside the sandbox, attributed to the mode's own world | `make test-space` — 25 | ✅ |
| 22 | gametype host | the engine binary runs a gametype package: scan a directory of modes, register each with its declared spawns, run the mode's script, materialize the player, step the world | `make test-host` — 20 | ✅ |
| 23 | audio | a synthesiser and a mixer: Big Mac's oscillator, ADSR, biquad and FDN reverb, an HRTF binaural panner, a stereo positional mix, and every walking body's footfall placed in the world | `make test-audio` — 47 · `make test-synth` — 34 · `make test-wav` — 22 | ✅ |
| 24 | render interpolation | the frame is drawn BETWEEN sim steps: the previous step's positions lerped by the fixed accumulator's remainder, and the camera follows the same interpolated point | `make test-host` — 56 | ✅ |
| 25 | record / replay | a run is logged as the actions and digest of every fixed step, re-run from the log alone, and compared: the first divergent tick is the difference between "the checksum disagreed" and "here is where" | `make test-host` — 73 · `make test-replay` | ✅ |
| 26 | headless fuzzing | the input is driven by a seeded pseudo-random action stream; the same seed always produces the same stream, so a failing case is replayable -- and each run is re-played from its log and compared | `make test-host` — 82 · the slice | ✅ |
| 27 | transport & reliability | the datagram transport the netcode is shaped like: a transport vtable, a deterministic lossy pair as the reference implementation, and the three message classes -- REL sequenced/acked/retransmitted, RED a window of recent inputs repeated in every packet, UNS a pass-through that is not reliability | `make test-nett` — 233 | ✅ |
| 28 | UDP transport | the same contract over a real socket: bind, address a peer by name, send and poll datagrams on winsock2, drop traffic from an unaddressed source, and run the reliability layer over it unchanged | `make test-netudp` — 24 | ✅ |
| 29 | lockstep frame-stepper | two peers exchange each tick's inputs over the link and step only when both are present, so a peer never runs ahead; a missing input stalls the lock, and a digest disagreement is reported at the exact tick while the lock holds | `make test-netls` — 845 | ✅ |
| 30 | AES-256-GCM | the sealed channel's cipher: an AEAD, so a swapped, replayed or truncated packet is rejected rather than simulated, and the tag covers the packet header as associated data. Verified against the GCM spec's Appendix B AES-256 cases and FIPS-197 | `make test-aesgcm` — 22 | ✅ |
| 31 | X25519 | the Curve25519 key exchange: two hosts turn a private scalar and a peer's public key into a shared secret, in constant time with respect to the scalar, refusing low-order peers. Verified against the RFC 7748 single-shot, iterated and Diffie-Hellman vectors | `make test-x25519` — 14 | ✅ |
| 32 | SHA-256, HMAC, HKDF | the hash the key derivation stands on, and the extract-then-expand KDF that turns the shared secret into a channel key. Verified against the FIPS 180-4 vectors and the RFC 5869 test cases | `make test-sha256` — 21 | ✅ |
| 33 | the sealed channel | X25519 + HKDF + AES-256-GCM composed: the salt is both public keys in canonical order (so the key depends on which peers are talking), the packet sequence is part of the nonce, and a tampered ciphertext, tag, header, sequence or truncation is refused rather than delivered | `make test-netc` — 153 | ✅ |
| 34 | fragmentation & reassembly | a burst delta snapshot larger than the wire is split under the MTU and rebuilt at the far end; a 16-bit PSN with modular wrap ordering, a fragment type of its own, and the loss amplification left visible -- a message with a missing fragment is never delivered, and the next message still reassembles | `make test-netfrag` — 58 | ✅ |
| 35 | clock synchronisation | NTP's symmetric-delay offset formula, refined by minimum-delay selection over an 8-sample window (the least-delayed path is closest to symmetric), with a smoothed drift so `remote(now)` stays right between syncs | `make test-netclock` — 31 | ✅ |
| 36 | QoS: the cap and the marking | a per-client token bucket (Source's `rate`) with a bounded queue, drop accounting and exact boundary behaviour; DSCP by traffic class -- EF for a lockstep input, AF41 for state, CS0/CS1 for the rest -- and WireGuard's rule that a tunnel carries the inner packet's marking out with it | `make test-qos` — 55 | ✅ |
| 37 | netem link emulation | the link the other gates need and a loopback cannot be: delay, jitter, loss, duplication, corruption, reordering and a rate limit, all driven by a seeded stream so a failing gate replays | `make test-netem` — 30 | ✅ |
| 38 | STUN binding | the reflexive-address transaction (RFC 8489): a big-endian header, XOR-MAPPED-ADDRESS with the port XOR'd by the cookie's top half and the address by the cookie (IPv4) or the cookie and transaction ID (IPv6), the server side that answers with the address it saw, and refusal of malformed input. The address arithmetic is checked against a hand-computed vector | `make test-stun` — 53 | ✅ |
| 39 | ICE | candidate priority (2^24*type + 2^8*local + 256-component), foundations over the base IP address, pair formation by component and family, redundant-candidate elimination, the 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0) pair priority with its MAX/MIN property, the checklist ordered by it, and nomination of the best valid pair. Checks are driven by a reachability oracle so the tests are a table rather than a network | `make test-ice` — 71 | ✅ |
| 40 | TURN | the relay for symmetric NATs: Allocate hands out a relayed address, Refresh keeps it alive, CreatePermission is what stops the relay being an open relay, ChannelBind shortens the forward path, and Send/Data are indications because there is nothing to acknowledge. The relay refuses an unpermitted peer and counts the refusal | `make test-turn` — 52 | ✅ |
| 41 | signalling | the offer/answer that carries a candidate list and the ICE credentials, fixed-width records so no length can disagree with a count, capped at what fits one datagram; and the connect-token gateway, where a room token is a deterministic hash of the room name so both clients compute it, one redemption per peer, two peers per room, and tokens that expire | `make test-netsig` — 58 | ✅ |
| 42 | QUIC wire primitives | RFC 9000's variable-length integer, the packet number codec (which spends as few bytes as the receiver's ambiguity window allows) and the long header with its Initial variant. Checked against the RFC's own worked examples: 0xc2197c5eff14e88c is 151288809941952652, and 0x9b32 after a largest of 0xa82f30ea decodes to 0xa82f9b32. Then the frame layer: a packet payload walked frame by frame, with the ACK ranges (the largest range, then Gap and Range pairs, expanded back to the exact acknowledged set), ECN counts, CRYPTO, STREAM with its four flag bits, NEW_CONNECTION_ID, RETIRE_CONNECTION_ID, PATH_CHALLENGE/RESPONSE, CONNECTION_CLOSE in both variants, and the flow-control frames | `make test-quic` — 187 | ✅ |
| 43 | QUIC packet protection | RFC 9001: the Initial salt and the client's Destination Connection ID give the initial secret (HKDF-Extract, not Expand), then "client in"/"server in" and "quic key"/"quic iv"/"quic hp" through HKDF-Expand-Label; the payload is sealed with AES-128-GCM (the nonce is the IV XOR the packet number, the AAD the header), and a five-byte mask from AES-ECB(hp, sample) hides the packet number. Verified against appendix A byte for byte: the derived keys of A.1 and the whole 1200-byte client Initial packet of A.2 | `make test-quicprotect` — 33 | ✅ |

| 44 | a QUIC packet number space | RFC 9002's loss detection on a clock the caller injects: the packet threshold (a packet three numbers below the largest acknowledged one is lost), the time threshold (`now() - max(9/8 * max(smoothed_rtt, latest_rtt), 1 ms)`), the next loss deadline, the RTT estimators (first sample seeds smoothed and variance at half itself; later samples move them by an eighth and a quarter), a sample only for a newly acknowledged largest packet, and the probe timeout with its backoff. PADDING-only packets are in flight; ACK-only packets are not |

| 45 | the ACK tracker | RFC 9000's receive side: acknowledge at once on an out-of-order or gapped ack-eliciting packet and on an ECN-CE one, otherwise after two ack-eliciting packets or the peer's max_ack_delay, and never answer a non-ack-eliciting packet with a non-ack-eliciting one. The frame carries the newest ranges and drops the oldest when it would not fit |

| 46 | the connection's three spaces | the Initial, Handshake and Application packet number spaces as one object, each with its own keys, and RFC 9001 section 4.9's discard: a discarded space abandons its loss recovery state and keys, but its packet number keeps climbing because RFC 9000 section 17.1 forbids reuse. Also the 2^62-1 ceiling that forces a close |

| 47 | a packet on the wire | the first place a frame becomes a packet: build a long header, the Length field (which counts the AEAD tag), the packet number and the payload, seal it, then open one by trying each packet number length, since that length is itself protected. Verified byte for byte against RFC 9001 appendix A.2's 1200-byte client Initial |

| 48 | the short header and 1-RTT | application data's packet: one byte plus the connection ID, no Length and no Version, the fixed bit, the protected reserved bits that read non-zero only as PROTOCOL_VIOLATION, the key phase bit, and a mid-connection key update derived with "quic ku" while the header protection key stays put |

| 49 | the packet number as the receiver sees it | a space's receive side: reconstruct a full packet number from its truncated form exactly as RFC 9000 appendix A.3 says, then suppress duplicates with a bounded window and a floor below which old packets are dropped |

| 50 | the ACK round trip | a parsed ACK frame offered to a space: every packet number it covers is dropped from in flight, an RTT sample is taken only when the largest acknowledged packet is newly acknowledged, and the loss set is recomputed from the packet threshold |

| 51 | the paths joined | a clear packet handed to its space: the truncated number unwrapped, the duplicate suppressed, the frame offset reported; and on the send side the packet number's width chosen by RFC 9000 section 17.1 and appendix A.2, which counts unacknowledged packets including the new one |

| 52 | a datagram of several packets | RFC 9000 section 12.2: walk a datagram into its QUIC packets using the Length field, which is in the clear; a short-header packet has no Length and so is last; packets with different connection IDs must not share a datagram |

| 53 | path validation and migration | RFC 9000 section 8.2, not a separate RFC: PATH_CHALLENGE out, PATH_RESPONSE echoed on the path it arrived, the path validated on a matching response; a challenge in a datagram under 1200 bytes validates the address but not the MTU; failure only on abandonment, after three times the PTO, and losing one of several challenges is not failure |

## Running totals

`make test` currently: **120 gates, 11225 assertions, 0 fail.**

ECS 21 · SPATIAL 142 · TILES 49 · RENDER 12 · EXP 21 · LV 58 · LV-SCRIPT 14 ·
PHYSICS 27 · CHARACTER 29 · GPU 26 · ASSET 30 · NET 28 · NETT 233 · NETUDP 24 · NETLS 845 · AESGCM 33 · X25519 14 · SHA256 21 · NETC 153 · NETFRAG 58 · NETCLOCK 31 · QOS 55 · NETEM 30 · STUN 53 · ICE 71 · TURN 52 · NETSIG 58 · QUIC 193 · QUICPROTECT 33 · QUICSPACE 128 · QUICACK 64 · QUICCONN 54 · QUICPACKET 61 · QUICSHORT 61 · QUICCOALESCE 49 · QUICPATH 51 · AVATAR 32 · MOD 21 ·
DIST 41 · EDIT 37 · MESH 37 · MODE 48 · SPAWN 37 · WORLDSPACE 53 · PORTAL 22 ·
SPACE 27 · HOST 82 · WALK 21 · AUDIO 47 · SYNTH 34 · WAV 22 · DSP 24 · OBJECTIVE 48 ·
HUD 43 · RENDER 20 · WINDOW 16 ·
PROF 29 · DET 15 · SNAP 17 · MATH 22 · FASTMATH 7 · INTERP 14 · JOB 12 ·
POOL 5074 · ACCESS 26 · ATOMIC 8 · FRAME 21 · HIVE 46 · TAGHEAP 21 · THREADHEAP 14 · BUDGET 23 ·
GEOM 10 · MESHLET 12 · LOD 22 · HLOD 14 · GALLOC 27 · OCCL 21 · BINDLESS 23 ·
STREAM 15 · MATERIAL 10 · COMPACT 12 · TAA 11 · DELTA 22 · SNAPSHOT 23 ·
HISTORY 23 · ROLLBACK 19 · NETMODEL 17 · HANDOFF 33 · ASSETGRAPH 21 · MATCH 13 ·
SHADERVAR 28 · WATCH 25 · CVAR 49 · I18N 31 · REPLAY 50 · VALIDATE 31 ·
COOK 35 · PATCH 33 · TLS 16 · EVENTLOG 59 · SOCIAL 57 · CATALOG 48 ·
MODERATE 39 · AGEGATE 45 · LEDGER 76 · FED 72 · RESV 88 · QKEY 95 · ADDR 77 ·
PLACE 57 · LAYER 63 · INTEREST 125 · TRAVEL 83 · LODCHAIN 67 · PERSIST 93 ·
PLANETS 96 · QUOTA 144 · PROV 74 · GEOLIC 105 · PLACEVAL 30 · HEIGHTFIELD 55.

## Design rules carried forward

- One spatial index (the quadkey core) for AOI + streaming + sharding. Never fork
  a second index.
- Physics owns transforms for bound entities; the ECS holds the game-logic view.
- C11 owns the core; Lua/slerm-Lua owns game logic. No managed runtime.
- Every slice gets a real test harness with pass/fail counts. No gate, no commit.
- The Lua bridge (`wb_script`) carries an opaque space context but never
  references the worldspace stack; the bindings live in `wb_space_bind`, so a
  gate that only needs the generic bridge does not link the world.
- **A mode's spawn points are declared data, not script side effects.** They
  live in `mode.json` so they exist before any script runs — which is what makes
  one mode able to travel INTO another. (`wb.add_spawn` exists for a mode that
  needs to add points at runtime; it cannot be the only source.)
