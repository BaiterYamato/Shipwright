local ship = require("ship")

-- Demo do registro de cenas: monta assets/ (com unbound/scenes.json) na raiz do VFS e, com um
-- save aberto, viaja uma vez para a cópia do Hyrule Field registrada pelo framework Unbound.
local TARGET = "linkspan_demo/field_copy/main"

local configured, failure = ship.native.call("configure", TARGET)
ship.log.info("unbound-scene-demo: " .. (configured or ("falhou: " .. tostring(failure))))

ship.events.on("game.frame", function()
    local event = ship.native.call("update", "")
    if event and event ~= "idle" then
        ship.log.info("unbound-scene-demo: " .. event)
    end
end)
