local ship = require("ship")

-- Demo do NEI: a DLL define o item, oferece e equipa. O Lua registra o estado quando muda.
local last = nil
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 60 ~= 0 then
        return
    end
    local stats, failure = ship.native.call("stats", "")
    local text = stats or ("falhou: " .. tostring(failure))
    if text ~= last then
        last = text
        ship.log.info("nei-demo: " .. text)
    end
end)

-- Itens do fork NEI (fase F): J dá o Roc's Feather e U o Roc's Cape, no C esquerdo.
local function give(id)
    local text, failure = ship.native.call("fork_give", id)
    ship.log.info("nei-demo: " .. (text or ("fork_give falhou: " .. tostring(failure))))
end
ship.hotkeys.register("fork_feather", { default = "J", label = "NEI: Roc's Feather" }, function()
    give("skijer.nei.rocs_feather")
end)
ship.hotkeys.register("fork_cape", { default = "U", label = "NEI: Roc's Cape" }, function()
    give("skijer.nei.rocs_cape")
end)
