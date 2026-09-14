local ship = require("ship")

local last_state = nil
local mic_active = false
local hud_phase = 0
local mic_rms = 0.0
local mic_gate = 0.0056
local mic_hz = 0.0
local mic_clarity = 0.0
local mic_tone = false
local mic_phrase = 0
local mic_samples = 0
local shown_level = 0.0
local roll_segments = {}
local roll_anchor = false

-- Rolo de notas logo abaixo da tarja, no espaço virtual 320x240 do HUD.
local ROLL_X, ROLL_Y, ROLL_W, ROLL_H = 17, 61, 244, 52
local ROLL_SECONDS = 6.0
local ROLL_SEMITONES = 12.5

local note_names = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

local function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end

local function field_number(state, name, fallback)
    local value = state:match(name .. "=([%d%.%-]+)")
    return tonumber(value) or fallback
end

local function detected_note(hz)
    if hz <= 0.0 then
        return "--"
    end
    local midi = math.floor(69 + 12 * (math.log(hz / 440.0) / math.log(2.0)) + 0.5)
    local name = note_names[(midi % 12) + 1]
    return name .. tostring(math.floor(midi / 12) - 1)
end

-- Formato do provider: roll=novo:antigo:semitons:flags,... com tempos em ms, semitons x10
-- relativos à primeira nota aceita e flags 1 = aceita na frase, 2 = sustentada agora.
local function parse_roll(state)
    roll_segments = {}
    roll_anchor = field_number(state, "anchor", 0) == 1
    local roll = state:match("roll=([^;]*)")
    if not roll then
        return
    end
    for newest, oldest, semis, flags in roll:gmatch("(%-?%d+):(%-?%d+):(%-?%d+):(%d+)") do
        local bits = tonumber(flags)
        roll_segments[#roll_segments + 1] = {
            newest = tonumber(newest) / 1000.0,
            oldest = tonumber(oldest) / 1000.0,
            semis = tonumber(semis) / 10.0,
            accepted = bits % 2 == 1,
            live = bits >= 2,
        }
    end
end

-- Rolo de notas do protótipo MicOcarina: cada nota cantarolada vira uma barra na altura
-- relativa à primeira nota aceita, que marca o tom da frase. A barra fica branca enquanto
-- a nota ainda não conta e verde quando entrou na frase comparada com as músicas. As
-- barras nascem na esquerda e andam para a direita conforme envelhecem.
local function draw_note_roll(has_stream)
    ship.hud.draw_rect(ROLL_X, ROLL_Y, ROLL_W, ROLL_H, 0, 0, 0, 150)
    local center = ROLL_Y + math.floor(ROLL_H / 2)
    ship.hud.draw_rect(ROLL_X, center, ROLL_W, 1, 255, 255, 255, 40)
    local pixels_per_second = ROLL_W / ROLL_SECONDS
    local pixels_per_semi = (ROLL_H - 4) / (2 * ROLL_SEMITONES)
    for _, segment in ipairs(roll_segments) do
        local x = ROLL_X + math.floor(segment.newest * pixels_per_second)
        local x_end = ROLL_X + math.floor(math.min(ROLL_SECONDS, segment.oldest) * pixels_per_second)
        local width = math.max(2, x_end - x)
        local y = center - math.floor(segment.semis * pixels_per_semi + 0.5) - 1
        if segment.accepted then
            ship.hud.draw_rect(x, y, width, 3, 124, 232, 138, segment.live and 245 or 170)
        else
            ship.hud.draw_rect(x, y, width, 3, 255, 255, 255, segment.live and 190 or 90)
        end
    end
    if has_stream and not roll_anchor then
        ship.hud.draw_text("SEGURE UMA NOTA PARA MARCAR O TOM", ROLL_X + 6, ROLL_Y + ROLL_H - 12, 255, 213, 102,
            220, 0.34)
    end
end

-- Confirma visualmente que o dispositivo está capturando. As coordenadas usam
-- o espaço virtual 320x240 do HUD do OoT, então o aviso mantém o tamanho com
-- qualquer resolução ou proporção de tela.
local function draw_mic_status()
    if not mic_active then
        return
    end

    hud_phase = (hud_phase + 1) % 120
    local target_level = clamp(mic_rms / math.max(mic_gate * 4.0, 0.0001), 0.0, 1.0)
    shown_level = shown_level + (target_level - shown_level) * 0.28
    local pulse = math.floor(155 + 70 * shown_level + 30 * (0.5 + 0.5 * math.sin(hud_phase * 0.12)))
    local has_stream = mic_samples > 0
    local has_sound = has_stream and mic_rms > mic_gate
    local status = "SEM SINAL DO DISPOSITIVO"
    local status_r, status_g, status_b = 255, 125, 95
    if has_stream and not has_sound then
        status = "ENTRADA OK - CANTE"
        status_r, status_g, status_b = 150, 235, 255
    elseif has_sound and not mic_tone then
        status = "SOM RECEBIDO - BUSCANDO NOTA"
        status_r, status_g, status_b = 255, 215, 105
    elseif mic_tone then
        status = string.format("NOTA %s  %.0f Hz", detected_note(mic_hz), mic_hz)
        status_r, status_g, status_b = 130, 255, 170
    end

    ship.hud.draw_rect(17, 15, 244, 43, 0, 0, 0, 190)
    ship.hud.draw_rect(17, 15, 34, 43, 235, 86, 24, 245)
    ship.hud.draw_ring(34, 32, 12, 2, 1.0, 255, 255, 255, pulse)
    ship.hud.draw_rect(31, 23, 6, 10, 255, 255, 255, 255)
    ship.hud.draw_rect(29, 31, 10, 2, 255, 255, 255, 255)
    ship.hud.draw_rect(33, 33, 2, 4, 255, 255, 255, 255)
    ship.hud.draw_rect(30, 36, 8, 2, 255, 255, 255, 255)
    ship.hud.draw_text("MICROFONE ATIVO", 56, 17, 255, 255, 255, 255, 0.55)
    ship.hud.draw_text(status, 56, 31, status_r, status_g, status_b, 255, 0.38)
    ship.hud.draw_rect(56, 45, 154, 4, 42, 55, 58, 230)
    ship.hud.draw_rect(56, 45, math.floor(154 * shown_level), 4, status_r, status_g, status_b, 245)
    ship.hud.draw_rect(94, 44, 1, 6, 255, 255, 255, 150)
    ship.hud.draw_text("B: SAIR", 216, 44, 255, 205, 145, 255, 0.30)
    draw_note_roll(has_stream)
end

local function update()
    local state, failure = ship.native.call("update", "")
    if not state then
        state = "erro: " .. tostring(failure)
    end
    mic_active = state:match("^audio%-input%-on") ~= nil
    if mic_active then
        mic_rms = field_number(state, "rms", 0.0)
        mic_gate = field_number(state, "gate", 0.0056)
        mic_hz = field_number(state, "hz", 0.0)
        mic_clarity = field_number(state, "clarity", 0.0)
        mic_tone = field_number(state, "tone", 0) == 1
        mic_phrase = field_number(state, "phrase", 0)
        mic_samples = field_number(state, "samples", 0)
        parse_roll(state)
    else
        mic_rms, mic_hz, mic_clarity, mic_phrase, mic_samples = 0.0, 0.0, 0.0, 0, 0
        mic_tone = false
        shown_level = 0.0
        roll_segments = {}
        roll_anchor = false
    end
    local state_name = state:match("^([^;]+)") or state
    if state_name ~= last_state then
        ship.log.info("mic-ocarina: " .. state_name)
        last_state = state_name
    end
end

ship.events.on("game.ready", function()
    local status, failure = ship.native.call("status", "")
    ship.log.info("mic-ocarina: " .. (status or tostring(failure)))
    ship.events.on("game.frame", update)
    ship.events.on("hook.oot.hud.draw", draw_mic_status)
end)
