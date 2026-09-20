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
