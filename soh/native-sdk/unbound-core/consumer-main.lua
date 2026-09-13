local ship = require("ship")

ship.events.on("game.ready", function()
    local result, failure = ship.native.call("unbound_factory_probe", "")
    ship.log.info("unbound-002b-factory: " .. (result or ("falhou: " .. tostring(failure))))
end)
