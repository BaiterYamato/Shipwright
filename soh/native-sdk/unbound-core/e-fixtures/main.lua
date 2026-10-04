local ship = require("ship")

-- Fixtures da fase E do Unbound (UNBOUND-007 a 011). A hotkey K viaja para a próxima entrada do roteiro;
-- o log registra chegada, posição, chão e atores vivos, e o estado das mensagens da fixture de texto.
local PLAN = table.concat({
    "linkspan_e/house/main",  -- 007/010: cena JSON própria, título, placas e exit por nome
    "linkspan_e/wide/main",   -- 008/009: colisão de 45 mil polígonos, x=z=45000, 600 atores
    "linkspan_e/cavern/main", -- 011: materialAnims (texCycle, scroll, cor por segmento)
    "linkspan_e/cavern_b/main",
    "linkspan_e/lab/main",    -- 011: um quad por receita de material (segmentos 8 a 13)
    "linkspan_e/lab_b/main",  -- 013: destino registrado da saída montada do lab
    "linkspan_e/many/main",   -- 008/009: 300 salas, 200 objetos, 100 portas, 60 caixas DynaPoly, água na sala 299
    "linkspan_e/edge_pos/main", -- 021: piso a 76 unidades de x=+1 048 576 e z=-1 048 576; chão nos índices 8192/32768
    "linkspan_e/edge_neg/main", -- 021: o mesmo no canto (-x, +z)
    "linkspan_e/span/main",   -- 021: malha de 81 920 unidades numa DL só; spawn em x=+40 000
    "linkspan_e/span/oeste",  -- 021: spawn em x=-40 000
    "linkspan_e/exits/main",  -- 023: 33 saídas; a faixa vermelha à frente usa exits[32] (vai à span, não à casa)
    "linkspan_e/r02/main",    -- 023: 65 540 vértices; offset 65 536 dá a volta (verde em W, acima do azul)
    "linkspan_e/m12_1024/main", -- 023: malha tipo 2 com 1 024 entradas (amarelo e magenta visíveis)
    "linkspan_e/m12_1025/main", -- 023: 1 025 entradas; a última (magenta) fica fora do SHAPE_SORT_MAX
    "linkspan_e/m17/main",    -- 024: 301 câmeras; fora das faixas (câmera 0)
    "linkspan_e/m17/c255",    -- 024: no centro da faixa da câmera 255 (vista de cima inclinada)
    "linkspan_e/m17/c256",    -- 024: câmera 256 (quase vertical); com 8 bits seria a 0
    "linkspan_e/m17/c300",    -- 024: câmera 300 (terceira pessoa); com 8 bits seria a 44
    "linkspan_e/m19/r63",     -- 024: água da room 63, na room 63 (agua=1)
    "linkspan_e/m19/r0_em_63", -- 024: a mesma caixa na room 0 (63 não é curinga: agua=0)
    "linkspan_e/m19/r64",     -- 024: água da room 64 na room 64 (agua=1)
    "linkspan_e/m19/r0_em_64", -- 024: na room 0 (64 não vira 0: agua=0)
    "linkspan_e/m19/r32766",  -- 024: água da room 32766 na room 32766 (agua=1)
    "linkspan_e/m19/r62_em_32766", -- 024: na room 62, o que 6 bits dariam (agua=0)
    "linkspan_e/m15/c64",     -- 026: 64 caixas grandes, a tabela DynaPoly cheia (dyna=64/64)
    "linkspan_e/m15/c65",     -- 026: 65; o Link cai sobre a 65ª, bgId 64 depois de a tabela dobrar (bg=64, 65/128)
    "linkspan_e/m15/p16380",  -- 026: 1 638 caixas, 16 380 polígonos: a lista fica em 16 384
    "linkspan_e/m15/p16390",  -- 026: 1 639 caixas, 16 390 polígonos: a lista cresce para 32 768
    "linkspan_e/m15/v16384",  -- 026: 2 048 caixas, 16 384 vértices: a lista de vértices fica em 16 384
    "linkspan_e/m15/v16392",  -- 026: 2 049 caixas, 16 392 vértices: cresce para 32 768
    "linkspan_e/m24_seq/main", -- 027: 8 193 texturas distintas num quadro (janelas 1x1 de um atlas)
    "linkspan_e/m24_slots/main", -- 027: A verde no slot 0, 8 192 chaves no slot 1; o painel de x positivo (à esquerda na tela) volta ao slot 0
    "linkspan_e/m09/o1020",   -- 029: 1 020 objetos na sala, o da caixa por último (objetos=1023 caixas=1)
    "linkspan_e/m09/o1021",   -- 029: 1 021, o banco fecha em 1 024 e a caixa nasce (objetos=1024 caixas=1)
    "linkspan_e/m09/o1022",   -- 029: 1 022, a última entrada (a da caixa) é descartada (objetos=1024 caixas=0)
    "linkspan_e/m08/a8100",   -- 029: 8 100 rúpias; a arena atual comporta 4 132 (as outras: Cannot allocate actor)
    "linkspan_e/m08/a8192",   -- 029: 8 192 rúpias; com arena suficiente, o total para em 8 192 (2 recusas pelo teto)
    -- linkspan_e/m08/a8200 fica fora: com mais de 8 192 atores na sala o host escreve além do buffer do oot.room.actors
    "linkspan_e/m18/main",    -- 031: 255 luzes na cena e 255 em cada sala; andando para a frente, três planos trocam A->B->A->B
    "ENTR_DODONGOS_CAVERN_ENTRANCE", -- RFC 0027 R09: delta da ddan, sala 0 de controle (mapa e bússola concedidos)
    "ENTR_DODONGOS_CAVERN_BOSS_DOOR", -- R09: o mesmo spawn, sala lógica 32 (além dos 19 minimapas e das 32 paletas)
    "linkspan_e/r08_neg/main",       -- R08: worldMapArea -1 vira 22 antes da pausa
    "linkspan_e/r08_high/main",      -- R08: 23 vira 22
    "linkspan_e/r08_none/main",      -- R08: 22 válido, sem moldura de área na pausa
    "linkspan_e/house/main",  -- retorno
}, ";")

local configured, failure = ship.native.call("configure", PLAN)
ship.log.info("unbound-e: " .. (configured or ("falhou: " .. tostring(failure))))

ship.hotkeys.register("next_fixture", { default = "K", label = "Unbound E: próxima fixture" }, function()
    local result = ship.native.call("next", "")
    ship.log.info("unbound-e: " .. tostring(result))
end)

-- Só teste (UNBOUND-013): J deixa o Link adulto com a Epona e a ocarina e volta ao lab, que tem `horse`.
ship.hotkeys.register("adult_epona", { default = "J", label = "Unbound E: adulto com Epona no lab" }, function()
    local result = ship.native.call("adult_epona", "linkspan_e/lab/main")
    ship.log.info("unbound-e: " .. tostring(result))
end)

-- Só teste (UNBOUND-013): U traz a Epona para o lado do Link, para o A virar "Ride" numa sequência automatizada.
ship.hotkeys.register("horse_here", { default = "U", label = "Unbound E: Epona ao lado do Link" }, function()
    local result = ship.native.call("horse_here", "")
    ship.log.info("unbound-e: " .. tostring(result))
end)

-- O framework aplica o texto no primeiro frame; a sonda olha a tabela logo depois.
local frames = 0
ship.events.on("game.frame", function()
    frames = frames + 1
    if frames == 5 then
        ship.log.info("unbound-e texto: " .. tostring(ship.native.call("text_probe", "")))
    end
    local event = ship.native.call("update", "")
    if event and event ~= "idle" then
        ship.log.info("unbound-e: " .. event)
    end
end)
