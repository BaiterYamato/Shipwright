local ship = require("ship")

ship.log.info("unbound-core: framework loaded")

local function logLines(prefix, text)
    for line in string.gmatch(text, "[^\n]+") do
        ship.log.info(prefix .. line)
    end
end

local function callAndLog(name)
    local result, failure = ship.native.call(name, "")
    if result then
        logLines("unbound: ", result)
    else
        ship.log.info("unbound: " .. name .. " failed: " .. tostring(failure))
    end
end

-- Os archives dos mods já estão montados no game.ready: a base e o registro de cenas leem todas as
-- camadas. As tabelas de mensagens só existem depois, então o texto entra no primeiro frame.
ship.events.on("game.ready", function()
    callAndLog("ready")
end)

-- Base convertida, documentos transcodificados e salas do patch de atores: registra o que mudou.
local lastHeader = nil
local seenNotes = {}
local lastRooms = nil
local frames = 0
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames == 1 then
        callAndLog("apply_text")
    end
    if frames % 30 ~= 0 then
        return
    end
    local report = ship.native.call("unbound_report", "")
    if report then
        local header = {}
        for line in string.gmatch(report, "[^\n]+") do
            if string.sub(line, 1, 6) == "nota: " then
                if not seenNotes[line] then
                    seenNotes[line] = true
                    ship.log.info("unbound-json: " .. line)
                end
            else
                header[#header + 1] = line
            end
        end
        local text = table.concat(header, " | ")
        if text ~= lastHeader then
            lastHeader = text
            ship.log.info("unbound-json: " .. text)
        end
    end
    local rooms = ship.native.call("room_report", "")
    if rooms and rooms ~= lastRooms then
        lastRooms = rooms
        ship.log.info("unbound-rooms: " .. rooms)
    end
end)
