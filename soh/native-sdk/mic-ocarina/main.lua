local ship = require("ship")
local ROOT = "textures/baiteryamato/mic_ocarina/"
local ui, meter, songs = {}, {}, {}
local last_state, known_snapshot = nil, nil
local shown_level = 0.0
local publish_menu
local menu_status = "Microphone idle. Open the ocarina to begin."
local error_text, error_frames = "", 0
local SONG_NAMES = {
    "Minuet of Forest", "Bolero of Fire", "Serenade of Water", "Requiem of Spirit",
    "Nocturne of Shadow", "Prelude of Light", "Saria's Song", "Epona's Song",
    "Zelda's Lullaby", "Sun's Song", "Song of Time", "Song of Storms", "Scarecrow's Song",
}
local REMAKE_NOTES = { "L", "R", "A", "Y", "X" }
local CLASSIC_NOTES = { "A", "CD", "CR", "CL", "CU" }
local NOTE_Y = { 197, 191, 185, 181, 173 }
local NOTE_NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

local function clamp(v, low, high) return math.max(low, math.min(high, v)) end
local function number(state, key, fallback)
    return tonumber(state:match(key .. "=([%d%.%-]+)")) or fallback or 0
end
local function icon(name, x, y, w, h, alpha)
    ship.hud.draw_icon(ROOT .. name, math.floor(x), math.floor(y), w, h, { alpha = alpha or 255 })
end
-- Texturas próprias evitam herdar o tile de medidores do HUD nativo.
local function fill(name, x, y, w, h, alpha)
    w, h = math.floor(w), math.floor(h)
    for dy = 0, h - 1, 64 do for dx = 0, w - 1, 64 do
        icon(name, x + dx, y + dy, math.min(64, w - dx), math.min(64, h - dy), alpha)
    end end
end
local function text(label, x, y, scale, color, alpha)
    local c = color or { 244, 239, 220 }
    ship.hud.draw_text(label, x, y, c[1], c[2], c[3], alpha or 255, scale or 0.38)
end
local function note_label(index)
    local labels = ui.gamepad == 1 and ui.remap == 1 and REMAKE_NOTES or CLASSIC_NOTES
    return labels[index + 1]
end
local function detected_note(hz)
    if hz <= 0 then return "--" end
    local midi = math.floor(69 + 12 * math.log(hz / 440.0) / math.log(2.0) + 0.5)
    return NOTE_NAMES[(midi % 12) + 1] .. tostring(math.floor(midi / 12) - 1)
end
local function refresh_songs()
    if known_snapshot == ui.known then return end
    known_snapshot = ui.known
    songs = {}
    local snapshot = ship.native.call("songbook", "") or ""
    for song, pattern in snapshot:gmatch("(%d+):([^;]*);") do
        local notes = {}
        for n in pattern:gmatch("%d+") do notes[#notes + 1] = tonumber(n) end
        songs[#songs + 1] = { id = tonumber(song), notes = notes }
    end
end
local function update()
    local state, failure = ship.native.call("update", "")
    state = state or ("Native call failed: " .. tostring(failure and failure.code or failure))
    local snapshot = ship.native.call("ui_state", "") or "visible=0"
    ui = { notes = {} }
    for key, value in snapshot:gmatch("([%w_]+)=([%d%-]+)") do
        if key ~= "notes" then ui[key] = tonumber(value) end
    end
    for note in (snapshot:match("notes=([^;]*)") or ""):gmatch("%d+") do
        ui.notes[#ui.notes + 1] = tonumber(note)
    end
    meter = { active = state:match("^audio%-input%-on") ~= nil,
              opening = state == "audio-input-opening", segments = {} }
    if meter.active then
        for _, key in ipairs({ "rms", "gate", "hz", "clarity", "tone", "phrase", "samples", "anchor" }) do
            meter[key] = number(state, key)
        end
        for newest, oldest, semis, flags in (state:match("roll=([^;]*)") or ""):gmatch("(%-?%d+):(%-?%d+):(%-?%d+):(%d+)") do
            local bits = tonumber(flags)
            meter.segments[#meter.segments + 1] = {
                newest = tonumber(newest) / 1000, oldest = tonumber(oldest) / 1000,
                semis = tonumber(semis) / 10, accepted = bits % 2 == 1, live = bits >= 2,
            }
        end
    else shown_level = 0 end
    if state:find("Could not open microphone", 1, true) or state:find("Audio capture unavailable", 1, true) then
        error_text, error_frames = "Microphone unavailable. Check your input device.", 180
    end
    error_frames = math.max(0, error_frames - 1)
    if ui.book == 1 then refresh_songs() end
    local summary = state:match("^([^;]+)") or state
    if summary ~= last_state then
        ship.log.info("mic-ocarina: " .. summary); last_state = summary
    end
    local status = summary
    if meter.active then status = ui.practice == 1 and "Audio practice active. No song effects will trigger." or "Microphone capturing. B closes audio input."
    elseif meter.opening then status = "Opening microphone... You can keep using the ocarina."
    elseif state == "audio-input-closing" then status = "Closing microphone..."
    elseif state == "waiting for ocarina" then status = "Open the ocarina to begin."
    elseif summary == "ocarina ready" then status = "Ocarina ready. Use + to start audio input."
    elseif state:find("Could not open microphone", 1, true) or state:find("Audio capture unavailable", 1, true) then
        status = "Microphone unavailable. Check your input device."
    end
    if menu_status ~= status then
        menu_status = status
        if publish_menu then publish_menu() end
    end
end

local function draw_staff()
    fill("panel", 50, 146, 224, 72, 80)
    for row = 0, 2 do for column = 0, 6 do
        icon("paper_" .. row .. "_" .. column, 48 + column * 32, 143 + row * 24, 32, 24)
    end end
    local confirmed = ui.mode and ui.mode >= 0x11 and ui.mode <= 0x17
    local heading = "play"
    if confirmed and SONG_NAMES[(ui.song or -1) + 1] then heading = "song" .. ui.song
    elseif ui.mode == 0x19 or ui.mode == 0x1B then heading = "listen"
    elseif ui.mode == 0x0E or ui.mode == 0x1D then heading = "retry" end
    for column = 0, 7 do icon("heading_" .. heading .. "_" .. column, 96 + column * 16, 148, 16, 16) end
    for y = 171, 195, 6 do fill("ink", 89, y, 164, 1, 80) end
    for index, note in ipairs(ui.notes) do
        local label = note_label(note)
        if label then
            local x, y = 96 + (index - 1) * 20, NOTE_Y[note + 1] - 6
            icon("note_" .. label, x, y, 13, 13)
            if ui.held == note and index == #ui.notes then fill("ink", x + 3, 207, 7, 1, 150) end
        end
    end
end

local function draw_capture()
    local target = clamp((meter.rms or 0) / math.max((meter.gate or 0.0056) * 4, 0.0001), 0, 1)
    shown_level = shown_level + (target - shown_level) * 0.25
    fill("panel", 112, 13, 111, 19, 210)
    icon("mic", 98, 10, 25, 25)
    text(ui.practice == 1 and "Practicing audio..." or "Inputting audio...", 125, 18, 0.40)
    fill("muted", 124, 29, 85, 2)
    fill("cyan", 124, 29, math.max(1, 85 * shown_level), 2, 230)
    local x, y, w, h = 48, 148, 224, 57
    fill("panel", x, y, w, h, 215)
    fill("cyan", x, y, w, 1, 80)
    for line = 0, 4 do fill("grid", x + 5, y + 9 + line * 9, w - 10, 1, line == 2 and 90 or 35) end
    for line = 0, 6 do fill("grid", x + 5 + line * 35, y + 8, 1, 40, 30) end
    local center, plot_x, plot_w = y + 27, x + 5, w - 10
    for _, segment in ipairs(meter.segments) do
        local left = clamp(plot_x + (6 - segment.oldest) / 6 * plot_w, plot_x, plot_x + plot_w - 2)
        local right = clamp(plot_x + (6 - segment.newest) / 6 * plot_w, left + 2, plot_x + plot_w)
        local pitch_y = clamp(center - segment.semis * 1.45, y + 8, y + h - 12)
        fill(segment.accepted and "cyan" or "amber", left, pitch_y, math.max(2, right - left), 2, segment.live and 255 or 170)
        if segment.live then icon("button_A", right - 3, pitch_y - 2, 5, 5, 245) end
    end
    local status = "No input signal"
    if (meter.samples or 0) > 0 then
        if meter.tone == 1 then status = detected_note(meter.hz) .. "  " .. string.format("%.0f Hz", meter.hz)
        elseif (meter.rms or 0) > (meter.gate or 0.0056) then status = "Finding the pitch..."
        else status = "Listening - hum a melody" end
    end
    text(status, x + 8, y + h - 9, 0.30, { 159, 225, 233 })
    if meter.anchor ~= 1 then text("Hold a note to set the pitch", x + 28, y + 23, 0.34, { 221, 218, 195 }) end
end

local function draw_songbook()
    fill("panel", 23, 88, 274, 129, 235)
    fill("amber", 23, 88, 274, 1, 140)
    text("Songbook", 133, 94, 0.48, { 250, 223, 163 })
    if #songs == 0 then text("No songs learned yet.", 103, 148, 0.4); return end
    for index, song in ipairs(songs) do
        local column = index > 7 and 1 or 0
        local row = (index - 1) % 7
        local x, y = 33 + column * 135, 108 + row * 15
        text(SONG_NAMES[song.id + 1] or "Melody", x, y, 0.30, { 232, 225, 202 })
        for n, note in ipairs(song.notes) do
            local label = note_label(note)
            if label then icon("button_" .. label, x + (n - 1) * 10, y + 5, 8, 8) end
        end
    end
end

local function draw_commands()
    local pad = ui.gamepad == 1
    fill("panel", 10, 225, 300, 14, 190)
    if ui.book == 1 then
        text("Close songbook", 25, 229, 0.37)
        icon("button_" .. (pad and "ZL" or "Z"), 107, 227, 11, 10)
    else
        if ui.mode == 0x0C and ui.action == 1 then
            text("Songbook", 20, 229, 0.36); icon("button_" .. (pad and "ZL" or "Z"), 67, 227, 11, 10)
        end
        text("Play a note", 92, 229, 0.36)
        local labels = pad and ui.remap == 1 and { "L", "R", "Y", "X", "A" } or CLASSIC_NOTES
        for index, label in ipairs(labels) do
            local native = pad and ui.remap == 1 and ({ 0, 1, 3, 4, 2 })[index] or index - 1
            icon("button_" .. label, 150 + (index - 1) * 13, 227, 11, 10, ui.held == native and 255 or 215)
        end
    end
    text("Cancel", 253, 229, 0.36); icon("button_B", 289, 227, 11, 10)
    if ui.book ~= 1 then
        if meter.active then
            fill("panel", 59, 211, 97, 11, 190); fill("panel", 166, 211, 94, 11, 190)
            text(ui.practice == 1 and "Practice: on" or "Practice audio", 66, 214, 0.30, { 244, 220, 174 })
            icon("button_" .. (pad and "MINUS" or "L"), 141, 212, 9, 9)
            text("Reset audio input", 171, 214, 0.30, { 244, 220, 174 })
            icon("button_" .. (pad and "PLUS" or "START"), 246, 212, 9, 9)
        elseif meter.opening then
            fill("panel", 99, 211, 122, 11, 190)
            text("Opening microphone...", 108, 214, 0.32, { 244, 220, 174 })
        else
            fill("panel", 115, 211, 93, 11, 180)
            text("Audio input", 124, 214, 0.32, { 244, 220, 174 })
            icon("button_" .. (pad and "PLUS" or "START"), 189, 212, 10, 9)
        end
    end
end
local function draw()
    if ui.visible ~= 1 then return end
    if ui.book == 1 then draw_songbook()
    elseif meter.active then draw_capture()
    else draw_staff() end
    draw_commands()
    if error_frames > 0 then
        fill("panel", 39, 37, 242, 13, 230)
        text(error_text, 47, 41, 0.31, { 255, 190, 137 })
    end
end

local function configure()
    local hud = ship.storage.get("remake_hud", true)
    local controls = ship.storage.get("remake_controls", true)
    ship.native.call("configure", (hud and "1" or "0") .. "," .. (controls and "1" or "0"))
end
local function action(id, label)
    return { id = id, type = "action", label = label, on_activate = function()
        local result, failure = ship.native.call("control", id)
        menu_status = result or tostring(failure and failure.code or failure)
        ship.log.info("mic-ocarina: " .. menu_status)
        publish_menu()
    end }
end
publish_menu = function()
ship.menu.register({ id = "ocarina", title = "Mic Ocarina", sidebar = "Ocarina & Audio", columns = 1, widgets = {
    { id = "remake_hud", type = "checkbox", label = "Remake ocarina interface", storage_key = "remake_hud", default = true,
      tooltip = "Parchment staff, songbook and audio input display.", on_change = configure },
    { id = "remake_controls", type = "checkbox", label = "L / R / Y / X / A note buttons", storage_key = "remake_controls", default = true,
      tooltip = "Nintendo button positions. Active only while playing the ocarina.", on_change = configure },
    { id = "help", type = "text", label = "Open the ocarina. ZL: songbook | +: audio/reset | -: practice | B: cancel." },
    action("open", "Open ocarina"), action("audio", "Start microphone / Reset input"), action("practice", "Toggle audio practice"),
    action("reset", "Reset audio input"), action("book", "Open / Close songbook"),
    { id = "status", type = "text", label = menu_status },
} })
end
publish_menu()
ship.events.on("game.ready", function()
    configure()
    ship.log.info("mic-ocarina: " .. (ship.native.call("status", "") or "unavailable"))
    ship.events.on("game.frame", update)
    ship.events.on("hook.oot.hud.draw", draw)
end)
