-- game.ready confirma carga, mas nao garante Player. O harness arma apos o boot.
local ship = require("ship")
local frames = 0
local function report()
    ship.log.info((ship.native.call("report", "")))
end
ship.events.on("game.ready", function()
    ship.log.info("movement-probe ready: aguardando hotkey begin ou transition")
    report()
end)
ship.hotkeys.register("begin", { default = "F8", label = "Provar movimento e camera" }, function()
    ship.log.info((ship.native.call("arm", "normal")))
end)
ship.hotkeys.register("transition", { default = "F9", label = "Provar leases na troca de cena" }, function()
    ship.log.info((ship.native.call("arm", "transition")))
end)
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 10 == 0 then report() end
end)
