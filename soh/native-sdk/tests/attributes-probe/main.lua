local ship = require("ship")

-- Registra no log do SoH só quando o valor ou o breakdown mudam, para a prova em jogo comparar linhas.
local ATTRIBUTE = "linkspan.test.attributes.power"
local last

ship.timer.every(20, function()
    local value, err = ship.attributes.value(ATTRIBUTE)
    local parts = {}
    for _, entry in ipairs(ship.attributes.breakdown(ATTRIBUTE) or {}) do
        parts[#parts + 1] = entry.modifier .. "@" .. (entry.item or "-")
    end
    local line = ("attributes-probe: power=%s breakdown=%s"):format(tostring(value or err), table.concat(parts, ","))
    if line ~= last then
        last = line
        ship.log.info(line)
    end
end)
