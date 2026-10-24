local ship = require("ship")
local hits = 0

-- Prova do P2 da F2i: grava 0 e depois falha. A ponte tem de manter o dano do jogo
-- e registrar a falha; depois de três falhas o core remove a inscrição.
ship.events.on("hook.oot.actor.damage", function(p)
    hits = hits + 1
    ship.log.info(string.format("damage-fail: hit=%d target_id=%d damage=%.9g", hits, p.target_id, p.damage))
    assert(ship.hooks.result(0))
    error(string.format("damage-fail: falha proposital no hit %d", hits))
end)

ship.log.info("damage-fail: ativo; hook.oot.actor.damage inscrito")
