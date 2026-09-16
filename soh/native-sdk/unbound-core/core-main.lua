local ship = require("ship")

ship.log.info("unbound-core: factory JSON registrada")

-- Os archives dos mods já estão montados no game.ready: o registro de cenas lê todas as camadas.
ship.events.on("game.ready", function()
    local result, failure = ship.native.call("load_scene_registry", "")
    ship.log.info("unbound-scenes: " .. (result or ("falhou: " .. tostring(failure))))
end)
