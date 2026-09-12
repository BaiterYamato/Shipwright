local ship = require("ship")

-- Perfil canônico do mod para controles Nintendo. Os índices SDL são físicos:
-- B=0, A=1, Y=2 e X=3 no Switch Pro.
local controls = {
    profile = "nintendo",
    move = { physical = "left_stick" },
    free_camera = { physical = "right_stick", active = true },
    jump = { physical = "X", sdl_button = 3 },
    context = { physical = "A", sdl_button = 1, virtual_mask = 0x8000 },
    roll = { physical = "A", condition = "moving" },
    sprint = { physical = "A", condition = "hold_after_roll" },
    sword = { physical = "Y", sdl_button = 2, virtual_mask = 0x4000 },
    cancel = { physical = "B", sdl_button = 0, virtual_mask = 0x4000 },
    target = { physical = "ZL" },
    equipped_item = { physical = "ZR" },
    quick_swap = { physical = "R", gesture = "hold" },
    shield = { physical = "L" },
    ocarina = { physical = "dpad_left" },
    navi = { physical = "dpad_right" },
    gear = { physical = "dpad_up", tap = "toggle_last", hold = "quick_swap_gear" },
    boots = { physical = "dpad_down", tap = "toggle_kokiri", hold = "quick_swap_boots" },
    menu = { physical = "+" },
    map = { physical = "-" },
}

local last_state = nil

local function update_movement()
    local state, failure = ship.native.call("update", "")
    if not state then
        if last_state ~= "error" then
            ship.log.warn("dynamic-movement: " .. tostring(failure))
            last_state = "error"
        end
        return
    end
    if state ~= last_state then
        ship.log.info("dynamic-movement: " .. state)
        last_state = state
    end
end

ship.events.on("game.ready", function()
    -- Zero desliga fallback por botão virtual: X é lido fisicamente no gamepad.
    local configured, configure_error = ship.native.call("configure", "0,4000")
    if not configured then
        ship.log.warn("dynamic-movement: configuração Nintendo recusada: " .. tostring(configure_error))
        return
    end
    local status, status_error = ship.native.call("status", "")
    ship.log.info("dynamic-movement: " .. (status or tostring(status_error)))
    -- Prova de runtime do OOT-CORE-001 contra o VFS real.
    local probe, probe_error = ship.native.call("resource_runtime_probe", "")
    ship.log.info("core-001-probe: " .. (probe or ("falhou: " .. tostring(probe_error))))
    ship.events.on("game.frame", update_movement)
end)
