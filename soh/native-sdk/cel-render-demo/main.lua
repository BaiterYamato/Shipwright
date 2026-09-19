local ship = require("ship")

-- A fixture só controla seu próprio estado. O host remove o estado também no
-- unload da DLL, portanto deixar o mod fora de mods/ devolve a imagem padrão.
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("cel-render-demo: " .. (stats or ("falhou: " .. tostring(failure))))
    end
end)

ship.hotkeys.register("toggle_cel_render_demo", { default = "Y", label = "CEL demo: ligar/desligar" }, function()
    local result, failure = ship.native.call("toggle", "")
    ship.log.info("cel-render-demo: " .. (result or ("falhou: " .. tostring(failure))))
end)

ship.hotkeys.register("mode_cel_render_demo", { default = "U", label = "CEL demo: modo (tudo/toon/sombra)" }, function()
    local result, failure = ship.native.call("mode", "")
    ship.log.info("cel-render-demo: " .. (result or ("falhou: " .. tostring(failure))))
end)
