local ship = require("ship")
local bodyChecked = false
ship.events.on("game.ready", function()
    ship.log.info("quad-probe ready: F8 normal, F9 detach, F10 transition, F11 hold")
end)
for _, mode in ipairs({ "normal", "detach", "transition", "hold" }) do
    local selected = mode
    local keys = { normal = "F8", detach = "F9", transition = "F10", hold = "F11" }
    ship.hotkeys.register(selected, { default = keys[selected], label = "Provar quads: " .. selected }, function()
        bodyChecked = false
        ship.log.info((ship.native.call("arm", selected)))
    end)
end
ship.events.on("game.frame", function()
    local report, err = ship.native.call("report", "")
    if not report then
        ship.log.error("quad-probe report falhou: " .. tostring(err))
        return
    end
    ship.log.info(report)
    if not bodyChecked and report:find("live=1", 1, true) then
        bodyChecked = true
        -- A recusa por lease precede a leitura de skeleton: {} nao instala corpo nem inclui assets.
        local ok, result = pcall(function() return ship.oot.player.set_body({}) end)
        ship.log.info("quad-lua body_blocked=" .. ((ok and result == false) and "1" or "0") .. " pcall=" .. tostring(ok))
    end
end)
