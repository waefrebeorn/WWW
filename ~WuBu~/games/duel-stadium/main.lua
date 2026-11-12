-- duel-stadium -- a duel: first to three tags takes the match.
--
-- The landing tag matches the hub's rift destination, so a player who steps
-- through the hub's gate materializes here.
--
-- The objective is declared in mode.json, not here. The host owns the rule, the
-- target and the clock, and it ends the match when the objective resolves -- so
-- this script carries no tick counter and no score of its own. It only plays
-- toward the objective: an opponent body that walks at the player and tags on
-- contact. Every number that tunes it is a config key in mode.json, read through
-- wb.cfg, so the mode can be re-balanced without touching this file.
--
--   wubu_engine.exe games duel-stadium              -- you stand still, bot wins
--   wubu_engine.exe games duel-stadium --input fwd  -- you run, and it chases

wb.add_spawn(0, 0, 0, -1, "landing")
wb.add_spawn(-4, 0, 0, 0, "home")
wb.add_spawn(4, 0, 0, 1, "away")

local speed    = wb.cfg("bot_speed", 2)
local radius   = wb.cfg("tag_radius", 1.2)
local cooldown = wb.cfg("tag_cooldown_ticks", 45)

-- The opponent. wb.spawn only takes a label, so it is placed by hand.
local bot = wb.spawn("bot")
wb.set_pos(bot, 4, 0, 0)

local ready_at = 0
local ticks = 0

function step(dt)
  ticks = ticks + 1
  if ticks == 1 then
    local p = wb.get_pos(wb.player)
    print(string.format("stadium: %s is at (%.0f, %.0f, %.0f)",
                        wb.player_id, p[1], p[2], p[3]))
  end

  -- The ground plane is x/y; z is up. A finished match is left alone.
  if wb.objective_state() ~= 0 then return end

  local pp = wb.get_pos(wb.player)
  local bp = wb.get_pos(bot)
  local dx = pp[1] - bp[1]
  local dy = pp[2] - bp[2]
  local d  = math.sqrt(dx * dx + dy * dy)

  if d > 1e-3 then
    wb.set_vel(bot, dx / d * speed, dy / d * speed, 0)
  end

  if d <= radius and ticks >= ready_at then
    wb.objective_score(1, 1)
    ready_at = ticks + cooldown
    print(string.format("stadium: bot tags %s -- %d-%d",
                        wb.player_id,
                        wb.objective_score_of(0),
                        wb.objective_score_of(1)))
  end
end
