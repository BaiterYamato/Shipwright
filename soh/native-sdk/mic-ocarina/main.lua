local ship = require("ship")

local last_state = nil

local function update()
    local state, failure = ship.native.call("update", "")
    if not state then
        state = "erro: " .. tostring(failure)
    end
    if state ~= last_state then
        ship.log.info("mic-ocarina: " .. state)
        last_state = state
    end
end

ship.events.on("game.ready", function()
    local status, failure = ship.native.call("status", "")
    ship.log.info("mic-ocarina: " .. (status or tostring(failure)))
    ship.events.on("game.frame", update)
end)
