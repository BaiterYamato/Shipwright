local ship = require("ship")

-- Demo dos hooks nativos: a DLL conta frames e updates de ator e pisca o Link pelo
-- replace do draw. O Lua só registra os contadores a cada 3 segundos.
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("hooks-demo: " .. (stats or ("falhou: " .. tostring(failure))))
    end
end)
