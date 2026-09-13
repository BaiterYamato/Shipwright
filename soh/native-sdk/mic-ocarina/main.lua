local ship = require("ship")

local last_state = nil
local mic_active = false
local hud_phase = 0

-- Confirma visualmente que o dispositivo está capturando. As coordenadas usam
-- o espaço virtual 320x240 do HUD do OoT, então o aviso mantém o tamanho com
-- qualquer resolução ou proporção de tela.
local function draw_mic_status()
    if not mic_active then
        return
    end

    hud_phase = (hud_phase + 1) % 120
    local pulse = math.floor(190 + 65 * (0.5 + 0.5 * math.sin(hud_phase * 0.12)))

    -- Tarja, bloco laranja e um glifo simples de microfone. O pulso branco
    -- deixa claro que a captura continua ativa, mesmo quando não há voz.
    ship.hud.draw_rect(17, 15, 192, 34, 0, 0, 0, 185)
    ship.hud.draw_rect(17, 15, 34, 34, 235, 86, 24, 245)
    ship.hud.draw_ring(34, 32, 12, 2, 1.0, 255, 255, 255, pulse)
    ship.hud.draw_rect(31, 23, 6, 10, 255, 255, 255, 255)
    ship.hud.draw_rect(29, 31, 10, 2, 255, 255, 255, 255)
    ship.hud.draw_rect(33, 33, 2, 4, 255, 255, 255, 255)
    ship.hud.draw_rect(30, 36, 8, 2, 255, 255, 255, 255)
    ship.hud.draw_text("MICROFONE ATIVO", 56, 17, 255, 255, 255, 255, 0.55)
    ship.hud.draw_text("B: SAIR", 56, 32, 255, 205, 145, 255, 0.45)
end

local function update()
    local state, failure = ship.native.call("update", "")
    if not state then
        state = "erro: " .. tostring(failure)
    end
    mic_active = state == "audio-input-on"
    if state ~= last_state then
        ship.log.info("mic-ocarina: " .. state)
        last_state = state
    end
end

ship.events.on("game.ready", function()
    local status, failure = ship.native.call("status", "")
    ship.log.info("mic-ocarina: " .. (status or tostring(failure)))
    ship.events.on("game.frame", update)
    ship.events.on("hook.oot.hud.draw", draw_mic_status)
end)
