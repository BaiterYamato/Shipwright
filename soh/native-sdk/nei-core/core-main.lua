local ship = require("ship")

-- Coremod do Not Enough Items: a DLL publica linkspan.nei.items e cuida do save. O Lua só registra
-- os contadores quando mudam.
local last = nil
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 ~= 0 then
        return
    end
    local stats = ship.native.call("stats", "")
    if stats and stats ~= last then
        last = stats
        ship.log.info("nei-core: " .. stats)
    end
end)

-- Instrumento de teste do NEI-003, não aquisição de verdade: enche e esvazia a página do inventário
-- do NEI com os itens que o fork declara para ela, para dar para olhar o menu antes de o get-item do
-- fork existir (NEI-008). Sai daqui quando o coremod for empacotado para valer (NEI-016).
local function inv(args, rotulo)
    local text, failure = ship.native.call("inv_fill", args)
    ship.log.info("nei-core: " .. rotulo .. ": " .. (text or ("falhou: " .. tostring(failure))))
end
ship.hotkeys.register("inv_fill", { default = "K", label = "NEI: encher a página do inventário" }, function()
    inv("", "inv_fill")
end)
ship.hotkeys.register("inv_clear", { default = "M", label = "NEI: esvaziar a página do inventário" }, function()
    inv("clear", "inv_clear")
end)

-- Instrumento de teste do NEI-007: cada toque faz o get-item do próximo item do fork (texto, ícone e
-- modelo do nei-assets-core.o2r). Sai junto com os de cima no NEI-016.
local giveList = nil
local giveNext = 0
ship.hotkeys.register("give_next", { default = "N", label = "NEI: get-item do próximo item do fork" }, function()
    if not giveList then
        local list = ship.native.call("give", "") or ""
        ship.log.info("nei-core: itens do fork: " .. list)
        giveList = {}
        for name in list:gmatch("([%w_]+)%(0x") do
            giveList[#giveList + 1] = name
        end
    end
    if #giveList == 0 then
        return
    end
    giveNext = giveNext % #giveList + 1
    local text, failure = ship.native.call("give", giveList[giveNext])
    ship.log.info("nei-core: give " .. giveList[giveNext] .. ": " .. (text or ("falhou: " .. tostring(failure))))
end)

-- Instrumento de teste do NEI-006: cria e mata as invocações da Cane of Somaria pelas funções do fork e ensaia o
-- descarregamento. Sem tecla: só pelo console (shiplua_fire_hotkey linkspan.nei actors_spawn).
for _, action in ipairs({ "spawn", "kill", "probe" }) do
    ship.hotkeys.register("actors_" .. action, { default = "", label = "NEI: atores " .. action }, function()
        local text, failure = ship.native.call("actors", action)
        ship.log.info("nei-core: actors " .. (text or ("falhou: " .. tostring(failure))))
    end)
end

-- Instrumento de teste das ondas de itens (NEI-008..011): duas hotkeys sem tecla por item do fork. eq_ dá a posse
-- e equipa no C esquerdo (shiplua_fire_hotkey linkspan.nei eq_shovel); give_ faz o get-item com modelo e texto.
local forkItems = ship.native.call("give", "")
if forkItems then
    for name in forkItems:gmatch("([%w_]+)%(0x") do
        ship.hotkeys.register("eq_" .. name, { default = "", label = "NEI: equipar " .. name }, function()
            local text, failure = ship.native.call("equip", name)
            ship.log.info("nei-core: equip " .. (text or ("falhou: " .. tostring(failure))))
        end)
        ship.hotkeys.register("give_" .. name, { default = "", label = "NEI: get-item " .. name }, function()
            local text, failure = ship.native.call("give", name)
            local motivo = failure and (tostring(failure.code) .. " " .. tostring(failure.message)) or "?"
            ship.log.info("nei-core: give " .. name .. ": " .. (text or ("falhou: " .. motivo)))
        end)
    end
end
for _, kind in ipairs({ "", "tektite", "dodojr", "wolfos", "armos" }) do
    local hotkey = kind == "" and "test_enemy" or ("test_enemy_" .. kind)
    ship.hotkeys.register(hotkey, { default = "", label = "NEI: inimigo à frente (teste) " .. kind }, function()
        local text, failure = ship.native.call("enemy", kind)
        ship.log.info("nei-core: " .. (text or ("enemy falhou: " .. tostring(failure and failure.message))))
    end)
end
for _, level in ipairs({ "", "razor", "gilded" }) do
    local hotkey = level == "" and "test_upgrade" or ("test_upgrade_" .. level)
    ship.hotkeys.register(hotkey, { default = "", label = "NEI: nível da Kokiri Sword (teste) " .. level }, function()
        local text, failure = ship.native.call("upgrade", level)
        ship.log.info("nei-core: " .. (text or ("upgrade falhou: " .. tostring(failure and failure.message))))
    end)
end
ship.hotkeys.register("test_magic", { default = "", label = "NEI: medidor de magia cheio (teste)" }, function()
    local text, failure = ship.native.call("magic", "")
    ship.log.info("nei-core: " .. (text or ("magic falhou: " .. tostring(failure))))
end)
-- Instrumento das ondas: o stats na hora, sem esperar o próximo segundo (shiplua_fire_hotkey linkspan.nei stats_now).
ship.hotkeys.register("stats_now", { default = "", label = "NEI: stats agora (teste)" }, function()
    ship.log.info("nei-core: agora: " .. (ship.native.call("stats", "") or "?"))
end)
