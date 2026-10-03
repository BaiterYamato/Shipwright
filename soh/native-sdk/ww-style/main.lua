local ship = require("ship")

-- Wind Waker Style: o menu do fork como páginas declarativas (ship.menu) e a configuração da DLL por snapshot.
-- Os valores vivem só no ship.storage deste mod; a DLL recebe "chave=valor" por linha a cada mudança.
-- Percentuais ficam em 0..100 no storage (o menu mostra "%") e vão divididos por 100.

local settings = {} -- { key, default (unidade do storage), scale }
local groups = {}   -- grupo de reset -> chaves

local function track(key, default, scale, group)
    settings[#settings + 1] = { key = key, default = default, scale = scale }
    if group then
        groups[group] = groups[group] or {}
        table.insert(groups[group], key)
    end
end

local push -- definido depois das páginas

local function merge(widget, extra)
    for k, v in pairs(extra or {}) do widget[k] = v end
    return widget
end

local function check(key, label, default, tooltip, extra)
    track(key, default, nil, extra and extra.group)
    return merge({ id = key:gsub("%.", "_"), type = "checkbox", label = label, storage_key = key, default = default,
                   tooltip = tooltip, on_change = function() push() end }, extra)
end

local function number(key, label, default, min, max, step, format, tooltip, extra)
    track(key, default + 0.0, 1, extra and extra.group)
    return merge({ id = key:gsub("%.", "_"), type = "slider_number", label = label, storage_key = key,
                   default = default + 0.0, min = min + 0.0, max = max + 0.0, step = step, format = format,
                   tooltip = tooltip, on_change = function() push() end }, extra)
end

-- Faixa e padrão na unidade da DLL (0..1, 0..2...); o slider mostra 0..100 %.
local function percent(key, label, default, min, max, tooltip, extra)
    track(key, default * 100.0, 0.01, extra and extra.group)
    return merge({ id = key:gsub("%.", "_"), type = "slider_number", label = label, storage_key = key,
                   default = default * 100.0, min = min * 100.0, max = max * 100.0, step = 1.0, format = "%.0f%%",
                   tooltip = tooltip, on_change = function() push() end }, extra)
end

local function integer(key, label, default, min, max, tooltip, extra)
    track(key, default, 1, extra and extra.group)
    return merge({ id = key:gsub("%.", "_"), type = "slider_int", label = label, storage_key = key,
                   default = default, min = min, max = max, step = 1, tooltip = tooltip,
                   on_change = function() push() end }, extra)
end

local function header(id, label, extra)
    return merge({ id = id, type = "separator", label = label }, extra)
end

local function reset(id, label, group, tooltip, extra)
    return merge({ id = id, type = "action", label = label, tooltip = tooltip, on_activate = function()
        for _, key in ipairs(groups[group] or {}) do
            for _, setting in ipairs(settings) do
                if setting.key == key then ship.storage.set(key, setting.default) end
            end
        end
        push()
    end }, extra)
end

local function when(key, value) return { key = key, equals = value } end

local cel = when("cel.enabled", true)
local casting = when("lights.enabled", true)
local shadows = when("shadows.enabled", true)
local sky = when("sky.enabled", true)

local pages = {
    {
        id = "cel", title = "Wind Waker Style", sidebar = "Cel Shading", columns = 2,
        widgets = {
            check("cel.enabled", "Enable Cel Shading", true,
                "Re-lights actors and objects with a single dominant light and a soft Wind Waker-style ramp. Only " ..
                "affects objects, not the static scene. Pairs well with cel-shaded texture packs."),
            header("cel_options", "Options", { visible_if = cel }),
            reset("cel_reset", "Reset All to Defaults", "cel",
                "Resets all the Cel Shading sliders below to their default values.", { visible_if = cel }),
            percent("cel.ramp_center", "Ramp Center", 0.5, 0.0, 1.0,
                "Where the dark-to-light transition sits. Higher = more of the surface stays in shadow.",
                { visible_if = cel, group = "cel" }),
            number("cel.ramp_softness", "Ramp Softness", 0.02, 0.01, 0.2, 0.01, "%.2f",
                "Width of the transition band. Low = a hard cel edge; high = a softer gradient.",
                { visible_if = cel, group = "cel" }),
            percent("cel.highlight_intensity", "Highlight Intensity", 0.6, 0.0, 2.0,
                "Brightness of the lit side. Higher = brighter highlights.", { visible_if = cel, group = "cel" }),
            percent("cel.shadow_intensity", "Shadow Intensity", 0.6, 0.0, 1.0,
                "How dark the shadow side gets. 0% = no shadow (flat), 100% = full shadow down to ambient.",
                { visible_if = cel, group = "cel" }),
            number("cel.point_light_range", "Point Light Range (x)", 1.5, 1.0, 4.0, 0.1, "%.1f",
                "Extends how far a point light can remain an object's key light, as a multiplier on its actual " ..
                "radius (key selection only - the game's real lighting is unchanged). Raise it so an orbiting fairy " ..
                "keeps lighting nearby objects even when it swings to its far side. 1x = the light's literal range.",
                { visible_if = cel, group = "cel" }),
            check("cel.use_navi_light", "Use Navi as a Light Source", true,
                "Let Navi count as a candidate key light for cel shading. Navi blinks on/off and orbits Link, so " ..
                "leaving this on makes the lighting on nearby objects shift around with her. Turn it off to ignore " ..
                "Navi and keep the key light steady (the sun/moon or a torch wins instead).",
                { visible_if = cel, group = "cel" }),
            number("cel.transition_time", "Transition Time (s)", 1.0, 0.1, 6.0, 0.1, "%.1f",
                "How long the key light takes to ease from one source to another. Higher = slower, more deliberate " ..
                "travel between the sun and a fairy/torch.", { visible_if = cel, group = "cel" }),
            header("cel_debug", "Debug", { visible_if = cel, column = 2 }),
            check("debug.cel_light_sources", "Light Source Viewer", false,
                "Draws a debug ray from each actor for every candidate light (coloured by the light, longer when " ..
                "stronger), a cyan range ring around each point light, and a bold magenta needle down the chosen " ..
                "key light, so you can see which light is winning and where the key points.",
                { visible_if = cel, column = 2 }),
            check("debug.cel_highlight_bands", "Highlight Lit Objects", false,
                "Renders every cel-shaded object as flat white on the lit side and flat black in shadow (the " ..
                "texture is discarded), so it is obvious which draws are being relit.",
                { visible_if = cel, column = 2 }),
        },
    },
    {
        id = "lights", title = "Wind Waker Style", sidebar = "Lights", columns = 2,
        widgets = {
            header("lights_misc", "Misc"),
            check("lights.hide_vanilla_glow", "Hide Vanilla Torch Glow", true,
                "Hides the original flat, billboarded, flickering glow circle the game draws over torches and other " ..
                "glow lights (it clashes with the cast pools). Applies while Light Casting is on."),
            check("lights.improve_flame_flicker", "Improve Flame Flicker", true,
                "Replaces the game's fast, jagged per-frame torch/flame flicker with a slow, organic Wind Waker " ..
                "flicker. Applied at the source, so it affects the vanilla scene lighting and Cel Shading even when " ..
                "Light Casting is off."),
            number("lights.flicker_speed", "Flicker Speed (x)", 1.0, 0.1, 3.0, 0.01, "%.2f",
                "How often flames pick a new brightness for the Wind Waker flicker. Higher = faster; lower = a " ..
                "lazier flame.", { enabled_if = when("lights.improve_flame_flicker", true), group = "lights" }),
            percent("lights.navi_saturation", "Navi's Light Tint", 0.2, 0.0, 1.0,
                "Tints Navi's light toward her current colour (yellow on enemies, and so on). Applied at the " ..
                "source, so it tints her cast pool, the objects she lights under Cel Shading, and the vanilla " ..
                "lighting alike. 0% = white.", { group = "lights" }),
            header("lights_casting", "Light Casting"),
            check("lights.enabled", "Enable Light Casting", false,
                "Casts a pool of light from each point light (torch, fairy, ...) onto the surrounding world " ..
                "geometry, Wind Waker-style. Affects only the static world, not actors/objects (lit by Cel Shading)."),
            check("lights.ww_default_movement", "Use Wind Waker default movement", true,
                "Pins the pool's tumble and size pulse to the authentic Wind Waker rates. Turn off to set Rotation " ..
                "Speed and Size Flicker yourself.", { visible_if = casting }),
            number("lights.rotation_speed", "Rotation Speed (x)", 1.0, 0.0, 3.0, 0.01, "%.2f",
                "Speed of the Wind Waker two-axis tumble that animates the pool's faceted edges. 1.0 = authentic; " ..
                "0 = static.", { visible_if = casting, enabled_if = when("lights.ww_default_movement", false),
                                 group = "lights" }),
            number("lights.size_flicker", "Size Flicker", 1.0, 0.0, 3.0, 0.01, "%.2f",
                "Depth of the Wind Waker size pulse - the pool's dominant flicker. 1.0 = authentic (~5%); 0 = " ..
                "steady. (Navi is excluded - she isn't a flame.)",
                { visible_if = casting, enabled_if = when("lights.ww_default_movement", false), group = "lights" }),
            number("lights.sphere_size", "Cast Size (x)", 0.5, 0.1, 4.0, 0.01, "%.2f",
                "Size of each light's cast pool, as a multiplier on the light's radius.",
                { visible_if = casting, group = "lights" }),
            percent("lights.intensity", "Light Intensity", 0.2, 0.0, 2.0, "Brightness of the cast light pools.",
                { visible_if = casting, group = "lights" }),
            check("lights.use_navi_light", "Enable Navi Light Casting", true,
                "Also cast a pool from Link's fairy (Navi). Navi darts around quickly, so her pool moves a lot.",
                { visible_if = casting }),
            number("lights.navi_sphere_size", "Navi Cast Size (x)", 0.75, 0.1, 4.0, 0.01, "%.2f",
                "Navi's pool size, separate from the main Cast Size.",
                { visible_if = casting, enabled_if = when("lights.use_navi_light", true), group = "lights" }),
            percent("lights.navi_intensity", "Navi Light Intensity", 0.2, 0.0, 2.0,
                "Navi's pool brightness, separate from the main Light Intensity.",
                { visible_if = casting, enabled_if = when("lights.use_navi_light", true), group = "lights" }),
            check("lights.other_fairy_lights", "Enable Other Fairy Light Casting", false,
                "Makes non-Navi fairies emit light (they don't in vanilla): the Kokiri Forest fairies and the " ..
                "healing fairies found out in the world. They then light nearby objects via Cel Shading too.",
                { visible_if = casting }),
            number("lights.wild_fairy_sphere_size", "Other Fairy Cast Size (x)", 0.75, 0.1, 4.0, 0.01, "%.2f",
                "Pool size for non-Navi fairies, separate from torches and Navi.",
                { visible_if = casting, enabled_if = when("lights.other_fairy_lights", true), group = "lights" }),
            percent("lights.wild_fairy_intensity", "Other Fairy Intensity", 0.2, 0.0, 2.0,
                "Pool brightness for non-Navi fairies, separate from the main Light Intensity.",
                { visible_if = casting, enabled_if = when("lights.other_fairy_lights", true), group = "lights" }),
            reset("lights_reset", "Reset Sliders to Defaults", "lights",
                "Resets all the Lights sliders to their default values."),
            header("lights_deku", "Deku Stick", { column = 2 }),
            check("lights.deku_stick_light", "Enable Deku Stick Light Casting", true,
                "Makes a lit, held Deku stick a real light source at its burning tip (it isn't in vanilla). Like a " ..
                "torch it lights nearby objects via Cel Shading and casts their shadows, and - with Light Casting " ..
                "on - casts its own pool on the world.", { column = 2 }),
            number("lights.deku_stick_sphere_size", "Deku Stick Cast Size (x)", 0.5, 0.1, 4.0, 0.01, "%.2f",
                "The held Deku stick's pool size, separate from torches.",
                { column = 2, visible_if = casting, enabled_if = when("lights.deku_stick_light", true),
                  group = "lights" }),
            header("lights_debug", "Debug", { column = 2, visible_if = casting }),
            check("debug.light_spheres", "Show Light Spheres", false,
                "Overlays a translucent faceted shell of each light's icosphere - the volume used for its cast pool.",
                { column = 2, visible_if = casting }),
        },
    },
    {
        id = "shadows", title = "Wind Waker Style", sidebar = "Actor Shadows", columns = 2,
        widgets = {
            check("shadows.enabled", "Enable Actor Shadows", false,
                "Replaces the vanilla actor shadows with a shape-based drop shadow for each actor: its own " ..
                "silhouette cast from the single key light Cel Shading picks, wrapped onto the real ground so it " ..
                "follows slopes and bumps. Works whether or not Cel Shading itself is on."),
            check("shadows.suppress_vanilla", "Suppress Vanilla Shadows", true,
                "Hide the original game's actor shadows (Link's feet, the NPC/enemy circles, the horse shadow, the " ..
                "sign and snake-statue texture shadows) so only the new shape shadows show.",
                { visible_if = shadows }),
            header("shadows_options", "Options", { visible_if = shadows }),
            reset("shadows_reset", "Reset All to Defaults", "shadows",
                "Resets all the Actor Shadows sliders below to their default values.", { visible_if = shadows }),
            percent("shadows.opacity", "Opacity", 0.2, 0.0, 1.0,
                "How dark the shadow's core is. 0 = invisible; higher = darker.",
                { visible_if = shadows, group = "shadows" }),
            integer("shadows.edge_softness", "Edge Softness", 0, 0, 2,
                "Smooths the shadow's outline. 0 = hard edge; 1 = one lighter step; 2 = a finer ramp plus a " ..
                "slightly wider fringe.", { visible_if = shadows, group = "shadows" }),
            number("shadows.length", "Length", 0.2, 0.0, 1.0, 0.01, "%.2f",
                "How long the shadow may get. Lower = always short and steep; higher = lets a low light stretch the " ..
                "shadow out further.", { visible_if = shadows, group = "shadows" }),
            number("shadows.slab_depth", "Slab Depth", 8.0, 5.0, 200.0, 1.0, "%.0f",
                "How far below the feet the shadow conforms to the ground. Higher = follows ground that dips " ..
                "further, but past a ledge the shadow creeps further down the drop.",
                { visible_if = shadows, group = "shadows" }),
            number("shadows.slab_rise", "Slab Rise", 8.0, 0.0, 120.0, 1.0, "%.0f",
                "How far ABOVE the feet the shadow can climb onto rising ground. Too high starts to catch the " ..
                "actor's own lower legs.", { visible_if = shadows, group = "shadows" }),
            integer("shadows.max_distance", "Render Distance", 550, 300, 5000,
                "Performance: actors farther than this from the camera get no shape shadow. Lower to gain frames " ..
                "in crowded scenes.", { visible_if = shadows, group = "shadows" }),
            header("shadows_debug", "Debug", { visible_if = shadows, column = 2 }),
            check("debug.shadow_volume", "Show Shadow Volume", false,
                "Draws the actual 3D shadow volume translucently: black top/bottom caps, blue side walls.",
                { visible_if = shadows, column = 2 }),
        },
    },
    {
        id = "sky", title = "Wind Waker Style", sidebar = "Sky", columns = 2,
        widgets = {
            check("sky.enabled", "Use Sky", false,
                "Replaces the overworld sky with a Wind Waker-style one: a gradient sky dome, drifting puffy clouds " ..
                "with a wispy horizon cloud band, and a twinkling night starfield."),
            header("sky_horizon", "Horizon", { visible_if = sky }),
            number("sky.horizon_height", "Horizon Height", -408.0, -2000.0, 2000.0, 1.0, "%.0f",
                "Raises or lowers the sky's horizon line - the gradient's haze boundary and the horizon cloud band " ..
                "move together.", { visible_if = sky }),
            percent("sky.horizon_parallax", "Horizon Parallax", 0.75, 0.0, 1.5,
                "How much the sky horizon sinks as the camera climbs. 0% = it follows the camera; 100% = it stays " ..
                "at a fixed world height.", { visible_if = sky }),
            header("sky_gradient", "Sky Gradient", { visible_if = sky }),
            check("sky.gradient.enabled", "Replace Sky Texture", true,
                "Replaces the sky texture with a smooth Wind Waker-style gradient that shifts with the time of day.",
                { visible_if = sky }),
            percent("sky.gradient.brightness", "Gradient Brightness", 1.0, 0.5, 1.5,
                "Overall brightness of the sky gradient.",
                { visible_if = sky, enabled_if = when("sky.gradient.enabled", true) }),
            header("sky_clouds", "Clouds", { visible_if = sky }),
            check("sky.clouds.enabled", "Enable Clouds", true,
                "Drifting Wind Waker-style puffy clouds across the sky, plus the wispy cloud band around the horizon.",
                { visible_if = sky }),
            percent("sky.clouds.opacity", "Cloud Opacity", 0.85, 0.0, 1.0, "How opaque the clouds are.",
                { visible_if = sky, enabled_if = when("sky.clouds.enabled", true) }),
            percent("sky.clouds.coverage", "Coverage", 0.3, 0.0, 1.0,
                "How much of the sky the clouds fill - from a few scattered clouds up to fully overcast.",
                { visible_if = sky, enabled_if = when("sky.clouds.enabled", true) }),
            number("sky.clouds.drift_speed", "Drift Speed (x)", 1.0, 0.0, 4.0, 0.1, "%.1f",
                "How fast the clouds drift across the sky on the wind. 1x is Wind Waker's own speed.",
                { visible_if = sky, enabled_if = when("sky.clouds.enabled", true) }),
            header("sky_stars", "Stars", { visible_if = sky, column = 2 }),
            check("sky.stars.enabled", "Enable Stars", true,
                "A Wind Waker-style twinkling starfield over the night sky, fading in at dusk and out at dawn.",
                { visible_if = sky, column = 2 }),
            integer("sky.stars.count", "Star Count", 1000, 50, 1000,
                "Maximum number of stars at full night. Wind Waker uses 1000.",
                { visible_if = sky, column = 2, enabled_if = when("sky.stars.enabled", true) }),
            percent("sky.stars.brightness", "Star Brightness", 1.0, 0.0, 2.0, "Overall star brightness.",
                { visible_if = sky, column = 2, enabled_if = when("sky.stars.enabled", true) }),
            number("sky.stars.twinkle_speed", "Twinkle Speed (x)", 1.0, 0.1, 5.0, 0.1, "%.1f",
                "How fast the stars pulse. 1x is Wind Waker's rate - about ten seconds per cycle.",
                { visible_if = sky, column = 2, enabled_if = when("sky.stars.enabled", true) }),
            header("sky_wisps", "Wind Wisps", { visible_if = sky, column = 2 }),
            check("sky.wisps.enabled", "Enable Wind Wisps", true,
                "Wind Waker's white wind streaks curling through the sky - occasionally pulling a full loop-de-loop.",
                { visible_if = sky, column = 2 }),
            number("sky.wisps.amount", "Wisp Amount (x)", 1.0, 0.5, 10.0, 0.1, "%.1f",
                "How many wisps ride the wind. 1x is Wind Waker's own count.",
                { visible_if = sky, column = 2, enabled_if = when("sky.wisps.enabled", true) }),
            number("sky.wisps.speed", "Wisp Speed (x)", 1.0, 0.25, 1.5, 0.01, "%.2f",
                "How fast the wisps fly. 1x is Wind Waker's own speed.",
                { visible_if = sky, column = 2, enabled_if = when("sky.wisps.enabled", true) }),
            header("sky_debug", "Debug", { visible_if = sky, column = 2 }),
            check("debug.sky_split", "Split-Screen Compare", false,
                "Draws the Wind Waker sky only on the left half of the screen, leaving the original sky visible on " ..
                "the right.", { visible_if = sky, column = 2 }),
        },
    },
}

-- Chave nunca gravada deixa o predicado visible_if/enabled_if falso mesmo com o padrão verdadeiro; grava os padrões
-- uma vez para o menu abrir coerente.
for _, setting in ipairs(settings) do
    if ship.storage.get(setting.key) == nil then ship.storage.set(setting.key, setting.default) end
end

-- O locale numérico do SoH é o do sistema (pt-BR usa vírgula); a DLL lê sempre ponto.
local function format_number(value)
    return (string.format("%.6g", value):gsub(",", "."))
end

local function snapshot()
    local lines = {}
    for _, setting in ipairs(settings) do
        local value = ship.storage.get(setting.key)
        if value == nil then value = setting.default end
        if type(value) == "boolean" then
            lines[#lines + 1] = setting.key .. "=" .. (value and "1" or "0")
        elseif type(value) == "number" then
            lines[#lines + 1] = setting.key .. "=" .. format_number(value * (setting.scale or 1))
        end
    end
    return table.concat(lines, "\n")
end

local pending = false

push = function()
    local result, failure = ship.native.call("configure", snapshot())
    pending = result == nil
    if pending then ship.log.warn("ww-style: configure failed: " .. tostring(failure)) end
end

for _, page in ipairs(pages) do
    for _, widget in ipairs(page.widgets) do widget.group = nil end
    local ok, failure = pcall(function() return ship.menu.register(page) end)
    if not ok then ship.log.warn("ww-style: page " .. page.id .. " has no menu: " .. tostring(failure)) end
end

push()

-- Se a DLL ainda não aceitou a configuração no load, tenta de novo no primeiro frame.
ship.events.on("game.frame", function()
    if pending then push() end
end)

ship.hotkeys.register("ww_style_stats", { label = "Wind Waker Style: log stats" }, function()
    local stats, failure = ship.native.call("stats", "")
    ship.log.info("ww-style: " .. (stats or ("failed: " .. tostring(failure))))
end)
