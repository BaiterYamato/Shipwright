local ship = require("ship")

-- Perfil canônico do mod para controles Nintendo. Índices SDL físicos no Switch
-- Pro: B=0, A=1, Y=2, X=3, "-"=4, R3=8, L=9, R=10 e D-pad cima/baixo/esquerda/
-- direita=11/12/13/14; ZL e ZR são os eixos 4 e 5.
local controls = {
    profile = "nintendo",
    move = { physical = "left_stick" },
    free_camera = { physical = "right_stick", invert_y = false, follow_delay_ms = 500 },
    first_person = { physical = "right_stick", invert_y = false, left_stick = "move" },
    jump = { physical = "X", sdl_button = 3 },
    context = { physical = "A", sdl_button = 1, virtual_mask = 0x8000 },
    roll = { physical = "A", condition = "moving" },
    sprint = { physical = "A", condition = "hold_after_roll" },
    sword = { physical = "Y", sdl_button = 2, virtual_mask = 0x4000 },
    cancel = { physical = "B", sdl_button = 0, virtual_mask = 0x4000 },
    target = { physical = "ZL" },
    shield = { physical = "ZL", sdl_axis = 4, virtual_mask = 0x0010, sword_priority = true },
    equipped_item = { physical = "ZR", sdl_axis = 5, uses = "selected_c_button" },
    item_menu = { physical = "R", sdl_button = 10, gesture = "hold", choose = "right_stick_x",
                  order = { "c_left", "c_down", "c_right" } },
    n64_l = { physical = "-", sdl_button = 4, virtual_mask = 0x0020 },
    lens = { physical = "R3", sdl_button = 8, gesture = "tap" },
    mask = { physical = "R3", sdl_button = 8, gesture = "hold", hold_ms = 400 },
    navi_first_person = { physical = "dpad_right", sdl_button = 14, virtual_mask = 0x0008 },
    ocarina = { physical = "dpad_left", sdl_button = 13, gesture = "tap" },
    gear = { physical = "dpad_up", sdl_button = 11, tap = "toggle_last", hold = "menu_right_stick", hold_ms = 400 },
    boots = { physical = "dpad_down", sdl_button = 12, tap = "toggle_kokiri", hold = "menu_right_stick", hold_ms = 400 },
    hud = { item_slots = "equipped_only", dpad = { up = "tunic", down = "boots", left = "ocarina", right = "navi" } },
    menu = { physical = "+" },
}

-- Ícones vanilla de icon_item_static (RGBA32 32x32) por valor de equipamento 1..3.
local EQUIP_ICONS = {
    tunic = {
        "textures/icon_item_static/gItemIconTunicKokiriTex",
        "textures/icon_item_static/gItemIconTunicGoronTex",
        "textures/icon_item_static/gItemIconTunicZoraTex",
    },
    boots = {
        "textures/icon_item_static/gItemIconBootsKokiriTex",
        "textures/icon_item_static/gItemIconBootsIronTex",
        "textures/icon_item_static/gItemIconBootsHoverTex",
    },
}

-- Ícone de cada item que cabe num botão C, na ordem de gItemIcons (z_inventory.c).
local ITEM_ICONS = {
    [0] = "textures/icon_item_static/gItemIconDekuStickTex",
    [1] = "textures/icon_item_static/gItemIconDekuNutTex",
    [2] = "textures/icon_item_static/gItemIconBombTex",
    [3] = "textures/icon_item_static/gItemIconBowTex",
    [4] = "textures/icon_item_static/gItemIconArrowFireTex",
    [5] = "textures/icon_item_static/gItemIconDinsFireTex",
    [6] = "textures/icon_item_static/gItemIconSlingshotTex",
    [7] = "textures/icon_item_static/gItemIconOcarinaFairyTex",
    [8] = "textures/icon_item_static/gItemIconOcarinaOfTimeTex",
    [9] = "textures/icon_item_static/gItemIconBombchuTex",
    [10] = "textures/icon_item_static/gItemIconHookshotTex",
    [11] = "textures/icon_item_static/gItemIconLongshotTex",
    [12] = "textures/icon_item_static/gItemIconArrowIceTex",
    [13] = "textures/icon_item_static/gItemIconFaroresWindTex",
    [14] = "textures/icon_item_static/gItemIconBoomerangTex",
    [15] = "textures/icon_item_static/gItemIconLensOfTruthTex",
    [16] = "textures/icon_item_static/gItemIconMagicBeanTex",
    [17] = "textures/icon_item_static/gItemIconHammerTex",
    [18] = "textures/icon_item_static/gItemIconArrowLightTex",
    [19] = "textures/icon_item_static/gItemIconNayrusLoveTex",
    [20] = "textures/icon_item_static/gItemIconBottleEmptyTex",
    [21] = "textures/icon_item_static/gItemIconBottlePotionRedTex",
    [22] = "textures/icon_item_static/gItemIconBottlePotionGreenTex",
    [23] = "textures/icon_item_static/gItemIconBottlePotionBlueTex",
    [24] = "textures/icon_item_static/gItemIconBottleFairyTex",
    [25] = "textures/icon_item_static/gItemIconBottleFishTex",
    [26] = "textures/icon_item_static/gItemIconBottleMilkFullTex",
    [27] = "textures/icon_item_static/gItemIconBottleRutosLetterTex",
    [28] = "textures/icon_item_static/gItemIconBottleBlueFireTex",
    [29] = "textures/icon_item_static/gItemIconBottleBugTex",
    [30] = "textures/icon_item_static/gItemIconBottleBigPoeTex",
    [31] = "textures/icon_item_static/gItemIconBottleMilkHalfTex",
    [32] = "textures/icon_item_static/gItemIconBottlePoeTex",
    [33] = "textures/icon_item_static/gItemIconWeirdEggTex",
    [34] = "textures/icon_item_static/gItemIconChickenTex",
    [35] = "textures/icon_item_static/gItemIconZeldasLetterTex",
    [36] = "textures/icon_item_static/gItemIconMaskKeatonTex",
    [37] = "textures/icon_item_static/gItemIconMaskSkullTex",
    [38] = "textures/icon_item_static/gItemIconMaskSpookyTex",
    [39] = "textures/icon_item_static/gItemIconMaskBunnyHoodTex",
    [40] = "textures/icon_item_static/gItemIconMaskGoronTex",
    [41] = "textures/icon_item_static/gItemIconMaskZoraTex",
    [42] = "textures/icon_item_static/gItemIconMaskGerudoTex",
    [43] = "textures/icon_item_static/gItemIconMaskTruthTex",
    [44] = "textures/icon_item_static/gItemIconSoldOutTex",
    [45] = "textures/icon_item_static/gItemIconPocketEggTex",
    [46] = "textures/icon_item_static/gItemIconPocketCuccoTex",
    [47] = "textures/icon_item_static/gItemIconCojiroTex",
    [48] = "textures/icon_item_static/gItemIconOddMushroomTex",
    [49] = "textures/icon_item_static/gItemIconOddPotionTex",
    [50] = "textures/icon_item_static/gItemIconPoachersSawTex",
    [51] = "textures/icon_item_static/gItemIconBrokenGoronsSwordTex",
    [52] = "textures/icon_item_static/gItemIconPrescriptionTex",
    [53] = "textures/icon_item_static/gItemIconEyeballFrogTex",
    [54] = "textures/icon_item_static/gItemIconEyeDropsTex",
    [55] = "textures/icon_item_static/gItemIconClaimCheckTex",
    [56] = "textures/icon_item_static/gItemIconBowFireTex",
    [57] = "textures/icon_item_static/gItemIconBowIceTex",
    [58] = "textures/icon_item_static/gItemIconBowLightTex",
}

local last_state = nil

local HUD_TEXTURES = "textures/baiteryamato/dynamic_movement_remake/"
-- Dedicated solid textures keep this panel independent of the engine's meter tile.
local function selector_fill(name, x, y, width, height, alpha)
    local offset = 0
    while offset < width do
        local part = math.min(64, width - offset)
        ship.hud.draw_icon(HUD_TEXTURES .. name, x + offset, y, part, height, { alpha = alpha })
        offset = offset + part
    end
end

local function selection_corners(x, y, side, alpha)
    local arm, line = 5, 1
    for _, corner in ipairs({ {x, y}, {x + side - arm, y},
        {x, y + side - line}, {x + side - arm, y + side - line} }) do
        selector_fill("gSelectorGoldTex", corner[1], corner[2], arm, line, alpha)
    end
    for _, corner in ipairs({ {x, y}, {x + side - line, y},
        {x, y + side - arm}, {x + side - line, y + side - arm} }) do
        selector_fill("gSelectorGoldTex", corner[1], corner[2], line, arm, alpha)
    end
end

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

-- Anel no botão C equipado no ZR. O host informa a posição final desenhada pelo
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
    selection_corners(tonumber(x) - 2, tonumber(y) - 2, tonumber(side) + 4, tonumber(alpha))
end

-- Ícone da Navi: PNG convertido por tools/convert-png-to-hud-icon.ps1 (o PNG fica fora do git,
-- que ignora *.png em soh/). O provider monta a pasta assets/ do pacote no configure.
local NAVI_ICON = "textures/baiteryamato/dynamic_movement_remake/gNaviIconTex"

-- D-pad do HUD nativo com as funções do mod: traje em cima, botas embaixo, ocarina na
-- esquerda e Navi na direita. O host desenha o fundo e informa a posição de cada direção.
local function draw_dpad()
    local state = ship.native.call("hud_dpad", "")
    if not state or state == "none" then
        return
    end
    local parts = {}
    for part in state:gmatch("[^;]+") do
        parts[#parts + 1] = part
    end
    local tunic, boots, ocarina = (parts[5] or ""):match("^(%d+),(%d+),(%d+)$")
    if #parts ~= 5 or not tunic then
        return
    end
    local icons = {
        EQUIP_ICONS.tunic[tonumber(tunic)],
        EQUIP_ICONS.boots[tonumber(boots)],
        ITEM_ICONS[tonumber(ocarina)],
        NAVI_ICON,
    }
    for index = 1, 4 do
        local x, y, side, alpha = parts[index]:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
        if x and icons[index] and tonumber(alpha) > 0 then
            ship.hud.draw_icon(icons[index], tonumber(x), tonumber(y), tonumber(side), tonumber(side),
                { alpha = tonumber(alpha) })
        end
    end
end

-- One quiet strip with stable slots, warm selection, and no segmented rings.
local selector_cursor = nil
local function draw_icon_row(icons, selected, hint)
    local size, gap, y = 32, 6, 186
    local width = #icons * size + (#icons - 1) * gap
    local left = math.floor(160 - width / 2)
    selector_fill("gSelectorPanelTex", left - 6, y - 6, width + 12, 46, 205)
    for index, icon in ipairs(icons) do
        local x = left + (index - 1) * (size + gap)
        local active = index == selected
        selector_fill(active and "gSelectorActiveTex" or "gSelectorSlotTex", x, y, size, size,
            active and 240 or 180)
        if icon ~= "" then
            local icon_size = active and 28 or 26
            local inset = (size - icon_size) / 2
            ship.hud.draw_icon(icon, x + inset, y + inset, icon_size, icon_size,
                { alpha = active and 255 or 220 })
        else
            selector_fill("gSelectorMutedTex", x + 12, y + 15, 8, 2, 95)
        end
        if active then selection_corners(x, y, size, 255) end
    end
    if selected then
        local target = left + (selected - 1) * (size + gap)
        selector_cursor = selector_cursor and selector_cursor + (target - selector_cursor) * 0.45 or target
        selector_fill("gSelectorGoldTex", math.floor(selector_cursor) + 7, y + size + 2, size - 14, 1, 255)
    end
    if hint then
        ship.hud.draw_icon(HUD_TEXTURES .. "gSelectorRTex", 153, 228, 14, 8, { alpha = 220 })
    end
end

-- Menu de traje/botas aberto com o D-pad cima ou baixo segurado.
local function draw_quick_swap()
    local state = ship.native.call("hud_quick_swap", "")
    if not state or state == "none" then
        return
    end
    local kind, selected, list = state:match("^(%a+);(%d);([%d,]+)$")
    local paths = kind and EQUIP_ICONS[kind]
    if not paths then
        return
    end
    local icons, selected_index = {}, nil
    for value in list:gmatch("%d") do
        icons[#icons + 1] = paths[tonumber(value)]
        if value == selected then
            selected_index = #icons
        end
    end
    draw_icon_row(icons, selected_index)
end

-- Menu de itens aberto com o R segurado: um ícone por botão C com item.
local function draw_item_menu()
    local state = ship.native.call("hud_item_menu", "")
    if not state or state == "none" then
        return
    end
    local selected, list = state:match("^(%d);(.*)$")
    if not selected then
        return
    end
    local icons, selected_index = {}, nil
    for entry in list:gmatch("[^\t]+") do
        local button, _, icon = entry:match("^(%d):(%d+):(.*)$")
        if not button then return end
        icons[#icons + 1] = icon
        if button == selected then
            selected_index = #icons
        end
    end
    draw_icon_row(icons, selected_index, "Release R to select")
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
    ship.events.on("hook.oot.hud.draw", function()
        draw_dpad()
        draw_item_selection()
        draw_quick_swap()
        draw_item_menu()
    end)
end)
