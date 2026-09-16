local ship = require("ship")

-- Demo do save por namespace: a DLL conta cargas de arquivo em linkspan-demo.save. O Lua só
-- registra o status quando ele muda.
local last = nil

ship.events.on("game.frame", function()
    local status = ship.native.call("status", "")
    if status and status ~= last then
        last = status
        ship.log.info("save-demo: " .. status)
    end
end)
