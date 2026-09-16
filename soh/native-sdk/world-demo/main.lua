local ship = require("ship")

-- Demo de mundo e colliders: a DLL cria o alvo, conta golpes e toques e consulta chão,
-- água, linha e parede. O Lua só registra os contadores a cada 3 segundos.
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("world-demo: " .. (stats or ("falhou: " .. tostring(failure))))
    end
end)
