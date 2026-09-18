local ship = require("ship")

ship.log.info("unbound-core: factory JSON registrada")

-- Os archives dos mods já estão montados no game.ready: o registro de cenas lê todas as camadas.
ship.events.on("game.ready", function()
    local result, failure = ship.native.call("load_scene_registry", "")
    ship.log.info("unbound-scenes: " .. (result or ("falhou: " .. tostring(failure))))
end)

-- Salas alteradas por scenes/<cena>/rooms/<n>.json: registra o relatório quando muda.
local lastReport = nil
local frames = 0
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 30 ~= 0 then
        return
    end
    local report = ship.native.call("room_report", "")
    if report and report ~= lastReport then
        lastReport = report
        ship.log.info("unbound-rooms: " .. report)
    end
end)
