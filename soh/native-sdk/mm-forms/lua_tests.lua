local callback, timers, events = nil, {}, {}
local allow_transition, enables, disabled = false, 0, 0
local ship = {
    native = { call = function(name, request)
        if name == "configure" then
            assert(type(request.segments["8"]) == "string")
            assert(request.models.roll and request.shield.skeleton)
            return { configured = true }
        end
        if name == "play_mask_on_animation" then return allow_transition and { frames = 123 } or nil end
        if name == "set_enabled" then
            if request.enabled then enables = enables + 1 else disabled = disabled + 1 end
            return { active = request.enabled }
        end
        error("chamada inesperada: " .. name)
    end },
    hotkeys = { register = function(_, _, fn) callback = fn end },
    timer = { after = function(frames, fn) assert(frames >= 0); timers[#timers + 1] = fn end },
    events = { on = function(name, fn) events[name] = fn end },
    log = { info = function() end, warn = function() end },
    oot = { player = { set_damage_immunity = function() end, set_weight = function() end } },
    player = { set_speed_multiplier = function() end },
}
package.preload.ship = function() return ship end
dofile(main_path)
assert(callback and events["scene.enter"] and events["game.shutdown"])
callback() -- Sem assets, a entrada falha e não ativa habilidades.
assert(enables == 0 and #timers == 0)
allow_transition = true
callback()
assert(#timers == 1)
callback() -- Cancelar invalida o timer que ainda estava em voo.
timers[1]()
assert(enables == 0 and disabled == 1)
callback()
events["scene.enter"]()
timers[2]() -- Uma cena nova também invalida o timer antigo.
assert(enables == 0)
events["game.shutdown"]()
assert(disabled == 2)
print("MM Forms Lua: spec, ausência de assets, cancelamento e timer entre cenas OK")
