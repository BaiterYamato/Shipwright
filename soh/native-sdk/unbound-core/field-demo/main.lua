local ship = require("ship")

-- Demo do patch de atores de sala (slice D1): monta assets/ com scenes/spot00/rooms/0.json
-- (unbound/room/1) na raiz do VFS e, com um save aberto, viaja uma vez para o Hyrule Field.
-- O framework Unbound aplica o patch quando a sala carrega.
local TARGET = "ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN"

local configured, failure = ship.native.call("configure", TARGET)
ship.log.info("unbound-field-demo: " .. (configured or ("falhou: " .. tostring(failure))))

ship.events.on("game.frame", function()
    local event = ship.native.call("update", "")
    if event and event ~= "idle" then
        ship.log.info("unbound-field-demo: " .. event)
    end
end)
