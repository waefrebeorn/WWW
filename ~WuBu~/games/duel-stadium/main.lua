-- duel-stadium -- a stadium game space you step into.
--
-- The landing tag matches the hub's rift destination, so a player who steps
-- through the hub's gate materializes here. Run the host on this directory to
-- see the destination side on its own:
--
--   wubu_engine.exe games duel-stadium

wb.add_spawn(0, 0, 0, -1, "landing")

local ticks = 0

function step(dt)
  ticks = ticks + 1
  if ticks == 1 then
    local p = wb.get_pos(wb.player)
    print(string.format("stadium: %s is at (%.0f, %.0f, %.0f)",
                        wb.player_id, p[1], p[2], p[3]))
  end
  if ticks >= 120 then
    wb.running = false
  end
end
