local ship = require("ship")
local polling = false
ship.events.on("game.ready", function()
    local report, failure = ship.native.call("start", "")
    ship.log.info(report or ("res-probe ERRO start: " .. tostring(failure)))
    polling = report ~= nil and report:find("stage=quiet", 1, true) ~= nil
end)
ship.events.on("game.frame", function()
    if not polling then return end
    local report, failure = ship.native.call("poll", "")
    if not report then
        polling = false
        ship.log.info("res-probe ERRO poll: " .. tostring(failure))
    elseif report:find("stage=complete", 1, true) then
        polling = false
        ship.log.info(report)
    end
end)
