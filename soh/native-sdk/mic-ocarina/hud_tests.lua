-- Execute the shipped Lua HUD, checking the real host's shared graphics budget.
local hooks, state, telemetry = {}, "visible=0", "waiting for ocarina"
local cost, labels, icons = 0, {}, {}
local registeredMenu
local songbook = ""
for song = 0, 12 do songbook = songbook .. song .. ":3,4,2,3,4,2;" end
package.preload.ship = function() return {
    native = { call = function(name)
        if name == "ui_state" then return state end
        if name == "update" then return telemetry end
        if name == "songbook" then return songbook end
        return "ready"
    end },
    hud = {
        draw_icon = function(path,x,y,w,h)
            assert(w >= 1 and w <= 64 and h >= 1 and h <= 64, "Invalid icon size")
            assert(x >= 0 and y >= 0 and x+w <= 320 and y+h <= 240, "HUD exceeds the 320x240 safe area")
            cost = cost + 1; icons[path:match("[^/]+$")] = true
        end,
        draw_text = function(label) cost = cost + #label; labels[#labels + 1] = label end,
    },
    events = { on = function(name, callback) hooks[name] = callback end },
    menu = { register = function(menu) registeredMenu = menu end }, storage = { get = function(_, fallback) return fallback end },
    log = { info = function() end },
} end
dofile(MIC_MAIN); hooks["game.ready"]()
local function frame(name, snapshot, audio)
    state, telemetry = snapshot, audio or "ocarina ready"
    hooks["game.frame"](); cost, labels, icons = 0, {}, {}
    hooks["hook.oot.hud.draw"]()
    assert(cost <= 350, name .. " exceeds budget reserved for UI and coexisting mods: " .. cost)
    print(name .. ": " .. cost .. " draw units")
end
frame("Closed ocarina", "visible=0"); assert(cost == 0)
local base = "visible=1;gamepad=1;remap=1;action=1;held=4;known=8191;"
frame("Live staff", base .. "mode=12;song=8;book=0;notes=3,4,2,3,4,2")
assert(icons.note_Y and icons.note_X and icons.note_A and icons.button_L and icons.button_R)
frame("Song confirmation", base .. "mode=21;song=8;book=0;notes=3,4,2,3,4,2")
assert(icons.heading_song8_0 and icons.heading_song8_7)
frame("All learned songs", base .. "mode=12;song=8;book=1;notes=")
assert(table.concat(labels):find("Scarecrow's Song", 1, true))
frame("Keyboard fallback", "visible=1;gamepad=0;remap=1;mode=12;action=1;book=0;held=0;notes=0,4")
assert(icons.note_A and icons.note_CU and icons.button_START and icons.button_Z)
frame("Mic listening", base .. "mode=12;book=0;practice=0;notes=", "audio-input-on;rms=0;gate=0.0056;samples=4096;tone=0;anchor=0;roll=")
assert(icons.mic and table.concat(labels):find("Inputting audio...", 1, true))
frame("Slow microphone opening", base .. "mode=12;book=0;notes=", "audio-input-opening")
assert(table.concat(labels):find("Opening microphone...", 1, true))
local roll = ""
for i = 0, 23 do roll = roll .. (i*200) .. ":" .. (i*200+180) .. ":" .. ((i%8-4)*30) .. ":1," end
frame("Mic practice history", base .. "mode=12;book=0;practice=1;notes=", "audio-input-on;rms=0.02;gate=0.0056;hz=440;samples=12000;tone=1;anchor=1;roll=" .. roll)
assert(table.concat(labels):find("Practicing audio...", 1, true) and table.concat(labels):find("A4  440 Hz", 1, true))
frame("Localized driver failure", base .. "mode=12;book=0;notes=", "Could not open microphone: WASAPI: parametro incorreto")
assert(registeredMenu.widgets[#registeredMenu.widgets].label == "Microphone unavailable. Check your input device.",
       "Localized driver diagnostics must not leak into the English game interface")
print("Shipped HUD: control glyphs, visibility, confirmation, audio state and render budget passed.")
