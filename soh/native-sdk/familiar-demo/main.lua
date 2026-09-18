local ship = require("ship")

-- Demo do slice D4: a DLL registra o apito e o familiar Keese e dá o item em gameplay. O Lua só
-- registra os contadores a cada 3 segundos.
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("familiar-demo: " .. (stats or ("falhou: " .. tostring(failure))))
    end
end)
