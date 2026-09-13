local ship = require("ship")

-- Perfil canônico do mod para controles Nintendo. Índices SDL físicos no Switch
-- Pro: B=0, A=1, Y=2, X=3, "-"=4, R3=8, L=9, R=10 e D-pad direita=14; ZR é o eixo 5.
local controls = {
    profile = "nintendo",
    move = { physical = "left_stick" },
    free_camera = { physical = "right_stick", follow_delay_ms = 500 },
    jump = { physical = "X", sdl_button = 3 },
    context = { physical = "A", sdl_button = 1, virtual_mask = 0x8000 },
    roll = { physical = "A", condition = "moving" },
    sprint = { physical = "A", condition = "hold_after_roll" },
    sword = { physical = "Y", sdl_button = 2, virtual_mask = 0x4000 },
    cancel = { physical = "B", sdl_button = 0, virtual_mask = 0x4000 },
    target = { physical = "ZL" },
    equipped_item = { physical = "ZR", sdl_axis = 5, uses = "selected_c_button" },
    item_select = { physical = "R", sdl_button = 10, order = { "c_left", "c_down", "c_right" } },
    shield = { physical = "L", sdl_button = 9, virtual_mask = 0x0010 },
    n64_l = { physical = "-", sdl_button = 4, virtual_mask = 0x0020 },
    lens = { physical = "R3", sdl_button = 8, gesture = "tap" },
    mask = { physical = "R3", sdl_button = 8, gesture = "hold", hold_ms = 400 },
    navi_first_person = { physical = "dpad_right", sdl_button = 14, virtual_mask = 0x0008 },
    menu = { physical = "+" },
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

-- Anel no botão C usado pelo ZR. O host informa a posição final desenhada pelo
-- HUD (cosméticos e margens incluídos); "none" com HUD oculto ou sem gamepad.
local function draw_item_selection()
    local rect = ship.native.call("hud_selection", "")
    if not rect or rect == "none" then
        return
    end
    local x, y, side, alpha = rect:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
    if not x then
        return
    end
    local half = tonumber(side) / 2
    ship.hud.draw_ring(tonumber(x) + half, tonumber(y) + half, half + 3, 2, 1.0, 255, 255, 255, tonumber(alpha))
end

ship.events.on("game.ready", function()
    -- Zero desliga fallback por botão virtual: X é lido fisicamente no gamepad.
    -- 500 ms sem analógico direito antes de a câmera voltar a seguir Link ao andar.
    local configured, configure_error = ship.native.call("configure", "0," .. controls.free_camera.follow_delay_ms)
    if not configured then
        ship.log.warn("dynamic-movement: configuração Nintendo recusada: " .. tostring(configure_error))
        return
    end
    local status, status_error = ship.native.call("status", "")
    ship.log.info("dynamic-movement: " .. (status or tostring(status_error)))
    -- Prova de runtime do OOT-CORE-001 contra o VFS real.
    local probe, probe_error = ship.native.call("resource_runtime_probe", "")
    ship.log.info("core-001-probe: " .. (probe or ("falhou: " .. tostring(probe_error))))
    -- Prova do OOT-UNBOUND-002A: o próprio mod monta duas camadas sintéticas,
    -- lê o mesmo JSON da menor para a maior prioridade e desmonta ambas.
    local layers, layers_error = ship.native.call("layer_runtime_probe", "")
    ship.log.info("unbound-002a-layers: " .. (layers or ("falhou: " .. tostring(layers_error))))
    -- Prova do OOT-CORE-002: o próprio mod criou este catálogo ao carregar a DLL.
    local registry_probe, registry_error = ship.native.call("registry_probe", "")
    ship.log.info("core-002-registry: " .. (registry_probe or ("falhou: " .. tostring(registry_error))))
    ship.events.on("game.frame", update_movement)
    ship.events.on("hook.oot.hud.draw", draw_item_selection)
end)
