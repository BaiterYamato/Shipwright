local ship = require("ship")

-- Demo do escape hatch: a DLL desvia Interface_Draw e o HUD pisca. O Lua registra o status a
-- cada 3 segundos.
local frames = 0

ship.log.info("escape-demo: " .. tostring(ship.native.call("status", "")))

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        ship.log.info("escape-demo: " .. tostring(ship.native.call("status", "")))
    end
end)
