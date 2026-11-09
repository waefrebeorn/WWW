-- game/main.lua -- WuBu Engine demo: scripted simulation.
-- The C#-replacement proof: pure Lua game logic drives C11-owned entities.

wb.running = true

local tick_count = 0

-- spawn a few actors with distinct labels
for i = 1, 3 do
  local e = wb.spawn("walker_" .. i)
  wb.set_vel(e, { 2.0 * i, 0.0, 1.0 })
  wb.set_mass(e, i)
end

-- step() is called by the C11 host once per physics frame
function step(dt)
  tick_count = tick_count + 1

  -- every 60 ticks, report positions of the walkers
  if tick_count % 60 == 0 then
    local n = wb.count()
    -- we can't enumerate handles from Lua here; use a static set we formed
    print(string.format("tick %d  live_entities=%d", tick_count, n))
    -- report one known handle via a namespace table
    if _G.actors then
      for k, e in pairs(_G.actors) do
        local p = wb.get_pos(e)
        print(string.format("  %s @ (%.2f, %.2f, %.2f)", k, p[1], p[2], p[3]))
      end
    end
  end

  if tick_count >= 180 then
    wb.running = false   -- stop the engine after 3 simulated seconds
  end
end

-- stash handles in a namespace table for the reporting loop above
_G.actors = {
  a1 = wb.spawn("named_a"),
  a2 = wb.spawn("named_b"),
}
wb.set_vel(_G.actors.a1, { 0.5, 0.0, 0.0 })
wb.set_vel(_G.actors.a2, { -0.5, 0.0, 0.25 })
