local ship = require("ship")

-- A grade vem do registro nativo: itens de add-ons aparecem com ícone e posse reais.
local items = {}
local catalog_snapshot = nil
local power_snapshot = nil
local powers = {}
local equipment = {}
local equipment_snapshot = nil
local bottle_status = ""
local menu_status = "Open a save file and click an icon to enable or disable the item."
local refresh_catalog
local sensor_page = 1
local sensor_items = {}
local sensor_names = { [0] = "None" }
local function publish_sensor()
    if #sensor_items == 0 then return end
    local wishes = {}
    for value in (ship.native.call("sensor_wishes", "") or ""):gmatch("(%d+)\n") do
        wishes[#wishes + 1] = tonumber(value)
    end
    local pages = math.ceil(#sensor_items / 60)
    local widgets = {
        { id = "sensor_help", type = "text", label = "Pick up to five desired items, in priority order. Sensor works in randomizer saves." },
        { id = "sensor_cost", type = "text", label = "Costs one permanent Heart Container only after you confirm." },
        { id = "sensor_page", type = "text", label = "Item list: page " .. sensor_page .. " / " .. pages },
        { id = "sensor_previous", type = "action", label = "Previous items", on_activate = function()
            sensor_page = ((sensor_page - 2) % pages) + 1; publish_sensor() end },
        { id = "sensor_next", type = "action", label = "Next items", on_activate = function()
            sensor_page = (sensor_page % pages) + 1; publish_sensor() end },
    }
    for slot = 0, 4 do
        local selected = wishes[slot + 1] or 0
        local choices = { {value = 0, label = "None"} }
        local present = selected == 0
        for index = (sensor_page - 1) * 60 + 1, math.min(sensor_page * 60, #sensor_items) do
            local item = sensor_items[index]
            choices[#choices + 1] = { value = item.id, label = item.name }
            if item.id == selected then present = true end
        end
        if not present and sensor_names[selected] then
            choices[#choices + 1] = { value = selected, label = sensor_names[selected] }
        end
        pcall(ship.storage.set, "sensor.wish" .. slot, selected)
        widgets[#widgets + 1] = { id = "sensor_wish_" .. slot, type = "choice", label = "Wish " .. (slot + 1),
            storage_key = "sensor.wish" .. slot, default = selected, choices = choices,
            on_change = function(value)
                local result, failure = ship.native.call("sensor_select", slot .. ":" .. value)
                menu_status = result or ("Failed: " .. tostring(failure and failure.code or failure))
            end }
    end
    ship.menu.register({ id = "sensor", title = "Not Enough Items", sidebar = "Sheikah Sensor", columns = 1, widgets = widgets })
end

local function publish_menu()
    local widgets = {}
    for index, item in ipairs(items) do
        widgets[#widgets + 1] = {
            id = "item_" .. index, type = "action", label = item.name,
            icon_path = item.icon, selected = item.owned, column = ((index - 1) % 4) + 1,
            tooltip = item.name .. (item.owned and " - enabled; click to disable"
                                             or " - disabled; click to enable"),
            on_activate = function()
                local result, failure = ship.native.call("test_toggle", item.id)
                menu_status = result or ("Failed: " .. tostring(failure and failure.code or failure))
                ship.log.info("nei-core: toggle " .. item.id .. ": " .. menu_status)
                refresh_catalog(true)
            end,
        }
    end
    if #widgets == 0 then
        widgets[1] = { id = "empty", type = "text", label = "No items available in this session." }
    end
    local ok, failure = pcall(function()
        ship.menu.register({ id = "items", title = "Not Enough Items", sidebar = "NEI Items",
                             columns = 4, widgets = widgets })
        local power_widgets = {}
        for index, power in ipairs(powers) do
            power_widgets[#power_widgets + 1] = {
                id = "power_" .. index, type = "action", label = power.name,
                icon_path = power.icon, selected = power.owned, column = ((index - 1) % 4) + 1,
                tooltip = power.name .. " - grant or remove this ability without changing quest progress",
                on_activate = function()
                    local result, failure = ship.native.call("power_toggle", power.key)
                    menu_status = result or ("Failed: " .. tostring(failure and failure.code or failure))
                    refresh_catalog(true)
                end,
            }
        end
        if #power_widgets > 0 then
            ship.menu.register({ id = "powers", title = "Not Enough Items", sidebar = "Modes & Abilities",
                                 columns = 4, widgets = power_widgets })
        end
        local equipment_widgets = {}
        for index, piece in ipairs(equipment) do
            equipment_widgets[#equipment_widgets + 1] = {
                id = "equipment_" .. index, type = "action", label = piece.name, icon_path = piece.icon,
                selected = piece.owned, column = ((index - 1) % 3) + 1,
                tooltip = piece.name .. " - grant and equip, or remove; age requirements apply",
                on_activate = function()
                    local result, failure = ship.native.call("equipment_toggle", piece.key)
                    menu_status = result or ("Failed: " .. tostring(failure and failure.code or failure))
                    refresh_catalog(true)
                end,
            }
        end
        if #equipment_widgets > 0 then
            ship.menu.register({id = "equipment", title = "Not Enough Items", sidebar = "NEI Equipment",
                                columns = 3, widgets = equipment_widgets})
        end
        ship.menu.register({id = "bottles", title = "Not Enough Items", sidebar = "Bottles", columns = 1, widgets = {
            {id = "bottles_status", type = "text", label = bottle_status},
            {id = "bottles_help", type = "text", label = "Add bottles to two inventory wheels. First use preserves the bottles in slots 1 and 2."},
            {id = "bottles_bottomless_help", type = "text", label = "Bottomless Bottle uses slot 4. Disabling it keeps its current contents."},
            {id = "bottle_add", type = "action", label = "Add empty bottle", on_activate = function()
                menu_status = ship.native.call("bottles_action", "add") or "Bottle action failed."; refresh_catalog(true) end},
            {id = "bottomless_toggle", type = "action", label = "Enable / disable Bottomless Bottle", on_activate = function()
                menu_status = ship.native.call("bottles_action", "bottomless") or "Bottle action failed."; refresh_catalog(true) end},
        }})
        ship.menu.register({ id = "status", title = "Not Enough Items", sidebar = "Status",
                             columns = 1, widgets = {
                                 { id = "status_text", type = "text", label = menu_status },
                             } })
        publish_sensor()
    end)
    if not ok then ship.log.warn("nei-core: menu unavailable: " .. tostring(failure)) end
end

refresh_catalog = function(force)
    local catalog, failure = ship.native.call("catalog", "")
    if not catalog then
        ship.log.warn("nei-core: catalog unavailable: " .. tostring(failure and failure.code or failure))
        return
    end
    local power_catalog = ship.native.call("power_catalog", "") or ""
    local equipment_catalog = ship.native.call("equipment_catalog", "") or ""
    local bottles = ship.native.call("bottles_status", "") or ""
    if #sensor_items == 0 then
        for id, name in (ship.native.call("sensor_catalog", "") or ""):gmatch("(%d+)\t([^\t\n]+)\n") do
            local item = {id = tonumber(id), name = name}; sensor_items[#sensor_items + 1] = item; sensor_names[item.id] = name
        end
    end
    if catalog == catalog_snapshot and power_catalog == power_snapshot and equipment_catalog == equipment_snapshot and bottles == bottle_status and not force then return end
    catalog_snapshot = catalog
    power_snapshot = power_catalog
    equipment_snapshot = equipment_catalog
    bottle_status = bottles
    equipment = {}
    for key, name, icon, owned in equipment_catalog:gmatch("([^\t\n]+)\t([^\t\n]+)\t([^\t\n]*)\t([01])\n") do
        equipment[#equipment + 1] = {key = key, name = name, icon = icon, owned = owned == "1"}
    end
    powers = {}
    for key, id, name, icon, owned in power_catalog:gmatch("([^\t\n]+)\t([^\t\n]+)\t([^\t\n]+)\t([^\t\n]*)\t([01])\n") do
        powers[#powers + 1] = { key = key, id = id, name = name, icon = icon, owned = owned == "1" }
    end
    items = {}
    for id, name, icon, owned in catalog:gmatch("([^\t\n]+)\t([^\t\n]*)\t([^\t\n]*)\t([01])\n") do
        items[#items + 1] = { id = id, name = name, icon = icon, owned = owned == "1" }
    end
    publish_menu()
end

refresh_catalog()

-- O registro muda quando um add-on define/remove item. Uma atualização periódica basta.
-- A leitura do pipeline de Player e dos atores muda a cada passo do Link e fica fora do log;
-- ela continua na função nativa "stats".
local last = nil
local frames = 0

ship.events.on("game.frame", function()
    frames = frames + 1
    if frames % 180 == 0 then refresh_catalog() end
    if frames % 60 ~= 0 then
        return
    end
    local stats = ship.native.call("stats", "")
    if not stats then
        return
    end
    local summary = stats:gsub(" | pipeline: .*$", "")
    if summary ~= last then
        last = summary
        ship.log.info("nei-core: " .. summary)
        if #items == 0 and summary:find("fork: disabled", 1, true) then
            local reason = summary:match("fork: ([^|]+)") or "disabled"
            menu_status = "NEI unavailable: " .. reason ..
                          ". Make sure the package and soh.symbols match soh.exe."
            publish_menu()
        end
    end
end)
