local ship = require("ship")
local hits = 0

ship.events.on("hook.oot.actor.damage", function(p)
    local result
    if p.attacker_is_player then
        result = math.floor(p.damage) + 1 -- inteiro Lua: int64 no core
    else
        result = p.damage * 0.5           -- float Lua: double no core, inclusive 0.0
    end

    assert(ship.hooks.result(result))
    hits = hits + 1
    ship.log.info(string.format(
        "damage-swap: hit=%d target_id=%d target_category=%d attacker_id=%d " ..
        "attacker_is_player=%s flags=0x%x effect=%d base_damage=%.9g damage=%.9g " ..
        "result=%.9g result_type=%s",
        hits, p.target_id, p.target_category, p.attacker_id,
        tostring(p.attacker_is_player), p.dmg_flags, p.damage_effect,
        p.base_damage, p.damage, result, math.type(result)))
end)

ship.log.info("damage-swap: ativo; hook.oot.actor.damage inscrito")
