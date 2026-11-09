-- omniverse-hub -- the 1:1 mirror of the universe.
--
-- The arrival point is declared in mode.json. The hub's own script declares
-- its stadium gate, then walks the player toward the rift. Stepping through is
-- the whole mechanic: the player leaves this world and materializes in the
-- destination gametype's instance at its landing tag, with the same identity.
--
-- Nothing here is hub-specific in the engine. The engine knows a mode has an
-- entry script, a set of spawn points and a set of gates; it does not know what
-- a "rift" is. That ignorance is what lets any gametype do this.

local rift = wb.add_gate("duel-stadium", "landing", 10, 0, 0, 2)

local RIFT = { 10, 0, 0 }
local WALK_SPEED = 4.0
local ticks = 0
local handed_off = false

function step(dt)
  ticks = ticks + 1
  if handed_off then return end

  local p = wb.get_pos(wb.player)
  local dx, dz = RIFT[1] - p[1], RIFT[3] - p[3]
  local dist = math.sqrt(dx * dx + dz * dz)

  if dist > 0.25 then
    wb.set_vel(wb.player, { dx / dist * WALK_SPEED, 0, dz / dist * WALK_SPEED })
    return
  end

  -- standing in the gate volume: travel
  wb.set_vel(wb.player, { 0, 0, 0 })
  local g = wb.gate_at(p[1], p[2], p[3])
  if g then
    local ok, inst, x, y, z = wb.travel(g, wb.player_id, -1)
    if ok then
      handed_off = true
      print(string.format(
        "hub: %s stepped through rift %d into instance %s at (%.0f, %.0f, %.0f)",
        wb.player_id, g, inst, x, y, z))
      wb.running = false
    end
  end
end
