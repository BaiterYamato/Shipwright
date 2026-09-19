local ship = require("ship")

-- Fixtures da fase E do Unbound (UNBOUND-007 a 011). A hotkey K viaja para a próxima entrada do roteiro;
-- o log registra chegada, posição, chão e atores vivos, e o estado das mensagens da fixture de texto.
local PLAN = table.concat({
    "linkspan_e/house/main",  -- 007/010: cena JSON própria, título, placas e exit por nome
    "linkspan_e/wide/main",   -- 008/009: colisão de 45 mil polígonos, x=z=45000, 600 atores
    "linkspan_e/cavern/main", -- 011: materialAnims (texCycle, scroll, cor por segmento)
    "linkspan_e/cavern_b/main",
    "linkspan_e/house/main",  -- retorno
}, ";")

local configured, failure = ship.native.call("configure", PLAN)
ship.log.info("unbound-e: " .. (configured or ("falhou: " .. tostring(failure))))

ship.hotkeys.register("next_fixture", { default = "K", label = "Unbound E: próxima fixture" }, function()
    local result = ship.native.call("next", "")
    ship.log.info("unbound-e: " .. tostring(result))
end)

-- O framework aplica o texto no primeiro frame; a sonda olha a tabela logo depois.
local frames = 0
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames == 5 then
        ship.log.info("unbound-e texto: " .. tostring(ship.native.call("text_probe", "")))
    end
    local event = ship.native.call("update", "")
    if event and event ~= "idle" then
        ship.log.info("unbound-e: " .. event)
    end
end)
