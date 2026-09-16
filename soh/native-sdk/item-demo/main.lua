local ship = require("ship")

-- Demo de itens sintéticos e tipos de ator: a DLL registra o item e o orbe e põe o item
-- num botão C ao carregar o arquivo. O Lua só registra os contadores a cada 3 segundos.
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("item-demo: " .. (stats or ("falhou: " .. tostring(failure))))
    end
end)
