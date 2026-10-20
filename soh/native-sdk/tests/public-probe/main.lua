-- Prova do catálogo público (RFC 0029): o relatório da DLL vai para o log no game.ready.
local ship = require("ship")
ship.events.on("game.ready", function()
    local report, failure = ship.native.call("report", "")
    ship.log.info(report or ("public-probe falhou: " .. tostring(failure)))
end)
