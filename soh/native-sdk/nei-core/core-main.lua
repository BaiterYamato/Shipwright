local ship = require("ship")

-- Coremod do Not Enough Items: a DLL publica linkspan.nei.items e cuida do save. O Lua só registra o estado no log
-- quando ele muda. A leitura do pipeline de Player e dos atores muda a cada passo do Link e fica fora do log; ela
-- continua na função nativa "stats".
local last = nil
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 ~= 0 then
        return
    end
    local stats = ship.native.call("stats", "")
    if not stats then
        return
    end
    local summary = stats:gsub(" | pipeline: .*$", "")
    if summary ~= last then
        last = summary
        ship.log.info("nei-core: " .. summary)
    end
end)
