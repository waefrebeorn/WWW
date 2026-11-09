/*
 * wb_space_bind.h -- the gamemode-facing space bindings (slice 21).
 *
 * The Yu-Gi-Oh stadium mechanic, exposed to a script. A gamemode's own
 * main.lua declares its stadium gates (wb.add_gate), tests whether a player is
 * standing in one (wb.gate_at), travels them into another gametype's instance
 * (wb.travel), and reads the live directory of worlds (wb.instances).
 *
 * This is a SEPARATE module from wb_script on purpose: wb_script is the
 * generic Lua<->core bridge and must not drag in the worldspace stack. A
 * state carries an opaque space context (attached with
 * wb_script_sandbox_attach_space); this module turns that context into the
 * wb.* bindings, and is linked only by hosts that want them.
 *
 * C11. The Lua host is the vendored Lua 5.4.
 */
#ifndef WB_SPACE_BIND_H
#define WB_SPACE_BIND_H

struct lua_State;

#ifdef __cplusplus
extern "C" {
#endif

/* Merge the space bindings into the state's wb.* table. Does nothing (returns
 * 0) when no space context is attached; returns -1 when a context is attached
 * but the wb table is missing. */
int wb_space_bind_install(struct lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* WB_SPACE_BIND_H */
