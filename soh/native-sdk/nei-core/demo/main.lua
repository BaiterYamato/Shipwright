local ship = require("ship")

-- Demo do NEI: a DLL define o item, oferece e equipa. O Lua registra o estado quando muda.
local last = nil
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 ~= 0 then
        return
    end
    local stats, failure = ship.native.call("stats", "")
    local text = stats or ("falhou: " .. tostring(failure))
    if text ~= last then
        last = text
        ship.log.info("nei-demo: " .. text)
    end
end)
