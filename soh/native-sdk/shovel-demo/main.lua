local ship = require("ship")
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 180 == 0 then
        local stats, failure = ship.native.call("stats", "")
        ship.log.info("shovel-demo: " .. (stats or ("failed: " .. tostring(failure))))
    end
end)
