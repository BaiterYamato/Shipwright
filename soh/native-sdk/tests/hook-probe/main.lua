-- Contadores dos hooks da HOST-SLIM-001 no log a cada 5 s de jogo.
local ship = require("ship")
local frames = 0
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 100 == 0 then
        ship.log.info((ship.native.call("report", "")))
    end
end)
