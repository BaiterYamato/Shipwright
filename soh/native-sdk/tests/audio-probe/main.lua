-- Contadores e uma janela de mute de 100 frames. O relatório sai antes de trocar o mute, para o pico de cada
-- linha cobrir uma janela inteira (a de frames=200 é a mutada).
local ship = require("ship")
local frames = 0
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 100 == 0 then
        local report, failure = ship.native.call("report", "")
        ship.log.info(report or ("audio-probe falhou: " .. tostring(failure)))
    end
    if frames == 100 then
        ship.native.call("mute", "1")
    elseif frames == 200 then
        ship.native.call("mute", "0")
    end
end)
