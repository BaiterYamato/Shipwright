local ship = require("ship")

-- Demo de câmera e render: a DLL desenha um modelo na mão do Link e alterna a câmera
-- própria com a do jogo. O Lua só registra os contadores a cada 3 segundos.
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("view-demo: " .. (stats or ("falhou: " .. tostring(failure))))
    end
end)
