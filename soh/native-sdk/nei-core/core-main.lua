local ship = require("ship")

-- Coremod do Not Enough Items: a DLL publica linkspan.nei.items e cuida do save. O Lua só registra
-- os contadores quando mudam.
local last = nil
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 ~= 0 then
        return
    end
    local stats = ship.native.call("stats", "")
    if stats and stats ~= last then
        last = stats
        ship.log.info("nei-core: " .. stats)
    end
end)
