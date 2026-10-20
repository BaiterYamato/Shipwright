-- O bootstrap entrega os archives antes de game.ready.
local ship = require("ship")
ship.events.on("game.ready", function()
    local report, failure = ship.native.call("report", "")
    ship.log.info(report or ("archives-probe falhou: " .. tostring(failure)))
end)
