local ship = require("ship")
local frames = 0
local function command(value)
    ship.log.info((ship.native.call("command", value)))
end
local actions = {
    { "begin", "normal", "Iniciar prova do corpo", "F6" },
    { "transition", "transition", "Iniciar prova de cena do corpo", "F7" },
    { "detach", "detach", "Soltar corpo nativo", "F8" },
    { "reattach", "reattach", "Recolocar corpo nativo", "F9" },
    { "finish", "finish", "Finalizar contrato observado", "F10" },
    { "ocarina", "ocarina", "Provar animacao com ocarina", "F11" },
}
for _, item in ipairs(actions) do
    local id, request, label = item[1], item[2], item[3]
    ship.hotkeys.register(id, { default = item[4], label = label }, function() command(request) end)
end
ship.hotkeys.register("lua_body", { default = "F12", label = "Provar exclusao de corpo Lua" }, function()
    local accepted = ship.oot.player.set_body {
        skeleton = "objects/object_link_boy/gLinkAdultSkel",
        anims = { parado = "objects/gameplay_keep/gPlayerAnim_link_normal_wait" },
    }
    ship.log.info("body-probe lua_set_body=" .. tostring(accepted))
    command(accepted == false and "lua_false" or "lua_true")
end)
ship.events.on("game.ready", function()
    ship.log.info("body-probe ready: aguarda begin/transition em gameplay adulto estavel")
end)
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 10 == 0 then ship.log.info((ship.native.call("report", ""))) end
end)
