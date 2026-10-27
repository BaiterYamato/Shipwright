local ship = require("ship")

-- Casca fora do RC. Os caminhos abaixo exigem o archive montado pelo MM Base.
-- Origem da spec: link-span-examples/lua/goron-form (0.1.41).
local GORON_BODY = {
    skeleton = "mm/objects/object_link_goron/gLinkGoronSkel",
    anims = {
        idle = "mm/objects/gameplay_keep/gPlayerAnim_pg_wait",
        walk = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_walk_free",
        run = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_run_free",
        -- A seleção padrão do host usa esta animação enquanto o Goron está
        -- sem chão sob os pés; é o header de pulo normal do Player do MM.
        jump = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_jump",
        fall = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_fall",
        land = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_landing_free",
        damage = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_damage_run_free",
        -- Tabela Goron de MM: os dois passos alternados sao reutilizados em
        -- sentido inverso para descer escada/vinha; os starts/finais ficam
        -- declarados para a spec poder representar o conjunto completo.
        climb_start_a = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_startA",
        climb_start_b = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_startB",
        climb_up_l = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_upL",
        climb_up_r = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_upR",
        climb_down_l = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_upL",
        climb_down_r = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_upR",
        climb_wait = "mm/objects/gameplay_keep/gPlayerAnim_link_normal_jump_climb_wait",
        climb_end_a_l = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_endAL",
        climb_end_a_r = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_endAR",
        climb_end_b_l = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_endBL",
        climb_end_b_r = "mm/objects/gameplay_keep/gPlayerAnim_pg_climb_endBR",
        door_open_left = "mm/objects/gameplay_keep/gPlayerAnim_pg_doorA_open",
        door_open_right = "mm/objects/gameplay_keep/gPlayerAnim_pg_doorB_open",
        chest_open = "mm/objects/gameplay_keep/gPlayerAnim_pg_Tbox_open",
        curl = "mm/objects/gameplay_keep/gPlayerAnim_pg_maru_change",
        roll_enter = "mm/objects/gameplay_keep/gPlayerAnim_pg_maru_change",
        roll_exit = "mm/objects/gameplay_keep/gPlayerAnim_pg_maru_change",
        mask_off = "mm/objects/gameplay_keep/gPlayerAnim_pg_maskoffstart",
        punch_a = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchA",
        punch_b = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchB",
        punch_c = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchC",
        -- Recuperações reais da tabela sMeleeAttackAnimInfo de MM. O bridge
        -- as escolhe automaticamente ao fim de cada punch (a variante `R`
        -- quando há movimento), sem expor controle de física para Lua.
        punch_a_end = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchAend",
        punch_b_end = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchBend",
        punch_c_end = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchCend",
        punch_a_end_run = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchAendR",
        punch_b_end_run = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchBendR",
        punch_c_end_run = "mm/objects/gameplay_keep/gPlayerAnim_pg_punchCendR",
        -- Tambores Goron: a entrada e a saída usam o mesmo `gakkistart`,
        -- com a saída tocada ao contrário; `gakkiplay` permanece em loop
        -- enquanto o OoT está no modo de ocarina. As cinco variantes são a
        -- tabela D_8085D714 do Player de MM (A, esquerda, baixo, cima, direita).
        gakki_start = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkistart",
        gakki_wait = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiwait",
        gakki_play = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiplay",
        gakki_play_a = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiplayA",
        gakki_play_l = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiplayL",
        gakki_play_d = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiplayD",
        gakki_play_u = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiplayU",
        gakki_play_r = "mm/objects/gameplay_keep/gPlayerAnim_pg_gakkiplayR",
    },
    -- Descer NÃO usa mais `reverse_anims`. O OoT já desce a escada tocando a
    -- mesma animação com playSpeed negativa (z_player.c:13218), e o host passou
    -- a espelhar essa velocidade no corpo externo — inverter aqui por cima
    -- cancelaria o sinal e a descida voltaria a subir.
    reverse_anims = {},
    -- A cabeça Goron lê o segmento N64 0x08; sem ele o corpo inteiro aparece,
    -- mas os olhos ficam transparentes. O host carrega este resource antes do
    -- skeleton e o preserva para qualquer mod que troque a forma.
    segments = {
        ["8"] = "mm/objects/object_link_goron/gLinkGoronEyesOpenTex",
    },
    -- Enquanto o Player do OoT está em Player_Action_Roll, o host troca o
    -- skeleton ereto por esta display list enrolada e a gira a cada frame.
    models = {
        roll = "mm/objects/object_link_goron/gLinkGoronCurledDL",
        -- Geometria opaca dos espinhos. O bridge so a desenha apos 60 frames
        -- de A mantido durante o roll; os efeitos de energia continuam em um
        -- passe separado, como no Player original de MM.
        roll_spikes = "mm/objects/object_link_goron/object_link_goron_DL_00C540",
        -- Os dois passes translúcidos do carregamento (grt_01/grt_02). O host
        -- instala o TwoTexScroll no segmento 0x08 antes de os desenhar.
        roll_energy_1 = "mm/objects/object_link_goron/object_link_goron_DL_0127B0",
        roll_energy_2 = "mm/objects/object_link_goron/object_link_goron_DL_0134D0",
        -- Efeito vermelho translúcido de impacto dos socos A/B/C. O bridge o
        -- prende à mão/cintura correta apenas nas janelas de dano de MM.
        punch_effect = "mm/objects/object_link_goron/gLinkGoronGoronPunchEffectDL",
        -- O MM desenha estes seis display lists no torso durante as animações
        -- gakki. Eles não pertencem à malha base e por isso são modelos
        -- separados, anexados pelo callback de limb do bridge.
        gakki_container = "mm/objects/object_link_goron/object_link_goron_DL_00FC18",
        gakki_piece_1 = "mm/objects/object_link_goron/object_link_goron_DL_010590",
        gakki_piece_2 = "mm/objects/object_link_goron/object_link_goron_DL_010368",
        gakki_piece_3 = "mm/objects/object_link_goron/object_link_goron_DL_010140",
        gakki_piece_4 = "mm/objects/object_link_goron/object_link_goron_DL_00FF18",
        gakki_piece_5 = "mm/objects/object_link_goron/object_link_goron_DL_00FCF0",
    },
    -- A defesa do Goron é outro skeleton de quatro membros e usa AnimationHeader
    -- normal (não LinkAnimationHeader). O bridge a mostra enquanto R está
    -- mantido, sem substituir a lógica de bloqueio que o Player do OoT já
    -- tiver disponível.
    shield = {
        skeleton = "mm/objects/object_link_goron/gLinkGoronShieldingSkel",
        animation = "mm/objects/object_link_goron/gLinkGoronShieldingAnim",
    },
    -- Deslocamento vertical do corpo, EM UNIDADES DE MUNDO (o host divide pela
    -- escala antes de gravar em shape.yOffset, e Actor_Draw multiplica de volta
    -- — ver ShipLuaBootstrap.cpp, CustomBodyActorUpdate).
    --
    -- CUIDADO ao calibrar: o skeleton Goron tem cerca de 60 unidades de altura,
    -- então valores muito negativos enterram o corpo inteiro sob o piso e ele
    -- some da tela — indistinguível de "não desenhou". Uma calibração anterior
    -- chegou a -210 às cegas e deixou a forma invisível.
    --
    -- Valor calibrado VISUALMENTE em jogo: -10. O comentário do host afirma que
    -- o corpo ereto flutuaria ~30 unidades sem compensação, mas na prática -30
    -- já deixava o Goron afundado no piso — ou seja, o root do skeleton
    -- convertido já pousa quase alinhado e só precisa de um ajuste fino. Confie
    -- no teste em jogo, não naquela suposição.
    ground_offset = -10.0,
    roll_offset = 12.0,
    -- Goron nao nada: em agua funda, enrola, afunda e volta pelo void-out
    -- nativo. A regra e opt-in da spec para nao afetar outros corpos.
    water_void = true,
    -- MM Goron nunca agarra bordas; o gate é exclusivo deste corpo e não
    -- reutiliza o estado global de Crowd Control do OoT.
    block_ledge_grab = true,
    default_anim = "idle",
}

local transformed = false
local busy = false
local generation = 0

local function abilities(on)
    -- LACUNA: comprovar a reversão destas primitivas também no unload individual do mod.
    ship.oot.player.set_damage_immunity("fire", on)
    ship.oot.player.set_weight(on and "heavy" or "normal")
    ship.player.set_speed_multiplier(on and 1.15 or 1.0)
end

local function cancel()
    generation = generation + 1
    busy = false
    transformed = false
    ship.native.call("set_enabled", { enabled = false })
    abilities(false)
end

local configured, err = ship.native.call("configure", GORON_BODY)
if not configured then
    ship.log.warn("MM Forms: spec recusada: " .. tostring(err))
    return
end

ship.hotkeys.register("goron_form", { default = "G", label = "Forma Goron (casca)" }, function()
    if busy then
        cancel()
        return
    end
    generation = generation + 1
    local current = generation
    busy = true
    if transformed then
        local removal = ship.native.call("play_body_animation", { name = "mask_off", mode = "once", speed = 1.0 })
        local frames = removal and removal.frames or 0
        ship.timer.after(frames, function()
            if generation ~= current then return end
            ship.native.call("set_enabled", { enabled = false })
            transformed = false
            abilities(false)
            ship.timer.after(13, function() if generation == current then busy = false end end)
        end)
    else
        local transition = ship.native.call("play_mask_on_animation", {})
        if not transition or transition.frames <= 0 then
            busy = false
            ship.log.warn("MM Forms: entrada recusada; confira os assets e o estado do jogador")
            return
        end
        ship.timer.after(transition.frames, function()
            if generation ~= current then return end
            local enabled = ship.native.call("set_enabled", { enabled = true })
            if not enabled then
                cancel()
                ship.log.warn("MM Forms: corpo indisponível; estado restaurado")
                return
            end
            transformed = true
            abilities(true)
            ship.timer.after(13, function() if generation == current then busy = false end end)
        end)
    end
end)

-- Cancelar timers da transição antes da cena deixar de existir.
ship.events.on("scene.enter", function()
    generation = generation + 1
    busy = false
end)
ship.events.on("game.shutdown", cancel)
ship.log.info("MM Forms: casca carregada, sem validação em jogo")
