# Ferramenta de playtest do Link-Span OoT

## Camadas do VFS e evidência RC11 (0.2.1, protocolo v2)

`vfs_layers(paths, max_bytes=8388608)` consulta `linkspan.oot.resources` v2 na thread do jogo, no próximo callback
`oot.input.pad`, inclusive no título. Retorna `frame`, `bytes_hashed`, `instrumentation`, `complete` e `observations`.
Cada observação contém `path`, `layers`, `hostStatus`, `hasFile` e `complete`; cada camada tem `{archive, sha256}`.
A ordem é **da menor para a maior prioridade**: a última camada tem prioridade máxima. O SHA-256 cobre os bytes do
callback, inclusive NUL binário e arquivo vazio; não é o hash do contêiner nem o `content_hash` de 64 bits do host.
Archives com transform entregam bytes transformados. O identificador do archive é copiado literalmente e pode incluir
`#linkspan-mount-N` em montagens com prefixo.

Limites por chamada: 1–8 caminhos distintos, 1–1024 bytes UTF-8 por caminho, sem NUL, até 32 camadas no total,
1024 bytes por identificador de archive e 1–8388608 bytes **processados pelo hash do observador**. O padrão é 8 MiB.
Ao exceder um limite, o status é 4 e a lista parcial daquele caminho é descartada. Bytes processados antes de um erro
posterior também consomem o orçamento. O host carrega todas as camadas antes dos callbacks: essa API não permite
limitar suas alocações, descompressão, I/O ou duração do frame. O observador usa buffers fixos e um hash reutilizável
BCrypt preparado antes do socket; não acrescenta JSON nem alocação explícita de heap ao callback de frame. A thread
TCP serializa o resultado. Nenhum ponteiro dos dados do callback é retido; permanece apenas a tabela estática do serviço.
O protocolo usa lease, ticket pendente e polling por `result`. Cancelamento, destruição de cena, desconexão, troca
forçada do dono e shutdown invalidam tickets VFS. Falhas não repetem automaticamente a consulta.

Status do host: 0 OK, 1 INVALID_ARGUMENT, 2 UNSUPPORTED, 3 FAILURE e 4 LIMIT. **O status 2 mistura arquivo ausente
com VFS indisponível**. Ele permanece como `complete=false`, `layers=[]`, acompanhado da sondagem `hasFile`.
Lista vazia não significa ausência comprovada. `complete=true` confirma a enumeração por callback, mas o próprio host
pode omitir camadas que não conseguiu carregar. Recibos diagnósticos não devem aprovar automaticamente o gate.

Para gravar o recibo, use `game_vfs_snapshot(case, phase, paths, max_bytes=8388608)`:

```python
game_vfs_snapshot("alias-A-B", "after-ready", ["private/A/file.json", "B/file.json"])
# Faça o unload pretendido e confirme sua conclusão externamente.
game_vfs_snapshot("alias-A-B", "after-unload", ["private/A/file.json", "B/file.json"])
game_close()
```

Confirme `game.ready` e unload no log real antes de declarar a fase. A fase vem do chamador; a ferramenta não dispara
nem certifica esses eventos. `case` aceita 1–80 letras ASCII, números, `_` ou `-`. Cada `(phase,path)` só pode ocorrer
uma vez por caso; lotes com caminhos diferentes podem ser acumulados.
O coletor grava `<case>-rc11-vfs-1.pending.json` na pasta da sessão e retorna `status=pending_post_log`.
Esse rascunho tem `postLogSha256=null` e **não é evidência final do gate**. Após o processo encerrar, `game_close`
copia o log fechado e grava `<case>-rc11-vfs-1.json` com `schema="rc11-vfs-1"`, `case`, `instrumentation`, `exeSha256`,
`postLogSha256`, `observations` e `complete`. A resposta inclui os caminhos em `vfs_snapshots`. O `.post.log` adjacente
contém os bytes exatos usados no hash. O hash do exe vem da medição feita pelo launcher, sem atestar a imagem em memória.
A revisão do observador é um SHA-256 calculado pelo CMake sobre os fontes nomeados; `mcp_revision` identifica o arquivo
do gravador de snapshots. Não são commits Git nem hashes da DLL. Recompile o agente; versões v2 anteriores recusam a operação.

A finalização exige o `game-final.log` real; nenhum hash de log é inventado. Falhas preservam os rascunhos.
Para recuperar offline, chame `linkspan_playtest.vfs.finalize_vfs_snapshots(session)` com sessão encerrada que forneça
`directory`, `metadata["closed"]` e `process.poll()`. Repetir a finalização confere o log congelado e recusa log
alterado ou recibo final conflitante. Caminhos ausentes/indisponíveis continuam diagnósticos no recibo final, inclusive
após unload. Consultar A/B comprova origens e bytes visíveis; regras de alias e donos precisam da fixture e do log.
Ainda é necessário compilar com MSVC e validar ready → consulta → unload → consulta → close no jogo.
Os testes Python usam agente simulado.

A ferramenta do SDK controla a porta 0 pelo hook `oot.input.pad` e oferece um agente local ao servidor MCP Python.
Exige o hook e o carregador `--linkspan-tool` da RFC 0032, ABI 1.4, Windows x64 e SDK exportado do mesmo build do host.
Mantenha a ferramenta fora de `mods/` e dos pacotes de release. Sua carga não deve ligar o motor alargado.

Compile em um Developer PowerShell com MSVC e CMake 3.26 ou superior, usando SDK e exe do **mesmo build**:

```powershell
cmake -S agent -B agent/build -G "Visual Studio 17 2022" -A x64 `
  -DOOT_NATIVE_SDK=D:/host/build/x64/native-sdk/Release/OotNativeSdk.cmake `
  -DHOST_EXECUTABLE=D:/host/build/x64/Release/soh.exe
cmake --build agent/build --config Release
ctest --test-dir agent/build -C Release --output-on-failure
python -m pip install ./mcp
python -m unittest discover -s mcp/tests -p "test_*.py"
```

O pacote gerado fica em `agent/build/mod/`: manifesto, `main.lua` vazio gerado e
`provider/linkspan_playtest.dll`. O CMake acrescenta `provider.host_layout_id`, sem fingerprint do exe. Recompile quando
o layout do host mudar. O JSON nlohmann vem de `OOT_NATIVE_INCLUDE_DIRS`; C++20 e `/utf-8` estão habilitados. O teste
C++ puro liga BCrypt do Windows para SHA-256, sem código de socket nem do jogo. Python usa somente `mcp` e a biblioteca padrão; os testes usam
`unittest` (o pytest também os coleta).

Defina `LINKSPAN_PLAYTEST_AGENT` para o pacote gerado quando usar o Python instalado. Na execução pelo código do SDK,
o padrão é `agent/build/mod`. `LINKSPAN_PLAYTEST_PORT` tem padrão 47811. O agente escuta somente em `127.0.0.1`, recusa
abrir o socket sem `LINKSPAN_PLAYTEST_TOKEN` e permite uma conexão controladora por vez. `game_launch` gera um token
aleatório por sessão; não coloque tokens na configuração MCP.

Exemplo para mesclar no `.mcp.json` do Claude:

```json
{
  "mcpServers": {
    "linkspan-playtest": {
      "command": "C:/Program Files/Python314/Scripts/linkspan-playtest-mcp.exe",
      "env": { "LINKSPAN_PLAYTEST_AGENT": "D:/sdk/tools/playtest/agent/build/mod" }
    }
  }
}
```

Exemplo para o `config.toml` do Codex:

```toml
[mcp_servers.linkspan-playtest]
command = 'C:\Program Files\Python314\Scripts\linkspan-playtest-mcp.exe'

[mcp_servers.linkspan-playtest.env]
LINKSPAN_PLAYTEST_AGENT = 'D:\sdk\tools\playtest\agent\build\mod'
```

Use o launcher do ambiente Python em que instalou o pacote. Também é possível executar
`python -m linkspan_playtest.server` com `mcp/` como diretório atual ou no `PYTHONPATH`.

| Ferramenta | Comportamento |
| --- | --- |
| `game_launch(runtime, console=false)` | Abre `runtime/soh.exe` com CWD no runtime e espera autenticação por até 90 s. Retorna PID, SHA-256, log e pasta de evidências. |
| `game_attach()` | Retoma a última sessão aberta ou a pasta indicada em `LINKSPAN_PLAYTEST_SESSION`. Não toma o lease de outro controlador. |
| `game_close()` | Envia `WM_CLOSE`, aguarda até 60 s, restaura CVars e retorna exit code e linhas de erro/crítico. |
| `game_state()` | Retrato do último callback; campos não aplicáveis vêm `null`. |
| `input(steps, wait=true)` | Enfileira 1–600 passos de 1–3600 leituras de controle. Aguarda o frame posterior ao último passo. |
| `wait_for(condition, timeout=30)` | Consulta localmente até satisfazer comparações unidas por `and`. |
| `screenshot(label="capture")` | Retorna PNG, caminho e `focus_used`. O rótulo aceita letras ASCII, números, `_` e `-`. |
| `console(command)` | Despacha um comando Sail; exige sessão com console. Sucesso confirma despacho, não a conclusão da transição. |
| `log(lines=100, pattern=null)` | Fim do log da sessão e stderr capturado, com filtro opcional de texto literal. |

Sequência comprovada (27/10/2026, cerca de 26 s): `game_launch(console=true)`, consultar até `game_state == "title"`,
repetir `input([{"buttons":["START"],"frames":3},{"frames":40}])` até `file_select`, repetir o mesmo com `A` até `play`,
`wait_for('game_state == "play" and textbox == false')`, `input([{"stick":[0,127],"frames":90}])`, comparar `position`,
`screenshot`, `console('entrance 205')`, `wait_for('scene != <antiga>')` e `game_close`.

Cada passo aceita `buttons`, `stick: [x,y]`, `cstick: [x,y]`, `frames` e `mode` (`replace`, padrão, ou `merge`). Eixos
vão de -128 a 127. Botões: `A B Z L R START CUP CDOWN CLEFT CRIGHT DUP DDOWN DLEFT DRIGHT`. Merge soma botões com OR e
troca apenas eixos não nulos; replace ignora o controle físico durante o passo. O controle físico volta quando a fila
termina. Insira passos neutros entre pressões repetidas. Cancelamento, release, tomada forçada ou queda do dono deixam
a próxima leitura neutra. Uma queda do socket nunca repete automaticamente um pedido de entrada.

Campos do retrato: `frame`, `game_state`, `scene`, `room`, `entrance`, `position.{x,y,z}`, `rotation.{x,y,z}`, `age`
(0 adulto, 1 criança), `health`, `max_health` (ambos em dezesseis avos de coração), `magic`, `rupees`, `pause`, `textbox`,
`queue_size`, `sail`, `game_mode` e `file_num` (do `gSaveContext`). Frame conta leituras do controle; rotação usa
unidades de ângulo do jogo. O estado vem do `GameState::destroy` atual (o host zera `init` depois de rodá-lo): `title`
cobre o logo, a abertura e a demo do título, que é um `PlayState` com `game_mode == 1`; estado sem símbolo no catálogo
vira `other`. Entrada antes do primeiro callback é recusada. Condições aceitam `== != < <= > >=`, `and`,
números, strings, booleanos e null JSON sobre os campos documentados, sem executar código recebido.

As evidências vão para `LINKSPAN_PLAYTEST_EVIDENCE` ou
`D:\Desenvolvimento\ship-lua-worktrees\logs\playtest`, em pastas únicas com data/hora. Incluem SHA do exe, comandos,
PNGs, stderr e log final. `session.json` contém o token temporário para attach: mantenha a pasta privada e não publique
esse arquivo. O fechamento concluído remove o token. Se o MCP sair, o jogo fica aberto e pode ser retomado; as CVars
são restauradas depois de o jogo encerrar.

Com `console=true`, o servidor escuta na porta local 43384 do Sail e muda temporariamente `CVars.gRemote.Sail` no
`shipofharkinian.json`. Depois restaura somente esses valores, preservando outras configurações salvas pelo jogo.
O Sail também liga a correção de voidout do Dark Link no host; o retrato informa `sail: true`. Esse campo registra o
modo solicitado, sem comprovar conexão Sail. O padrão `console=false` não abre o Sail nem altera suas CVars.

A captura tenta `PrintWindow(PW_RENDERFULLCONTENT)` sem ativar a janela. Falha ou imagem toda preta aciona `BitBlt`
com foco, informa `focus_used: true` e tenta devolver o foco à janela anterior. Se o Windows recusar o foco, há erro
explícito. Em 27/10/2026 o `PrintWindow` capturou a janela DX11 em segundo plano sem foco; janela minimizada e frame
legitimamente preto não foram testados. A ferramenta não
comprova controle físico, microfone nem giroscópio.

O CTest `oot_native_playtest_tool` compila contra o SDK exportado, executa os testes C++ puros e os testes Python com
`unittest`. Esses testes não substituem a prova em título → seleção de arquivo → gameplay → transição → captura.

## v2 — PLAYTEST-MCP-002 (0.2.0)

As nove ferramentas anteriores mantêm assinaturas e comportamento. A v2 acrescenta:

| Ferramenta | Contrato |
| --- | --- |
| `scene_map(offset=0, limit=128)` | Cena viva: spawns, entradas locais, exits, salas e transition actors com posição, `rotY`, parâmetros, salas e plano de cruzamento. |
| `scene_overview(y_min=null, y_max=null, bbox=null, include_actors=false, include_ceiling=false, stride=1, limit=128, max_pages=32, size=960, ...)` | Planta PNG vista de cima da colisão estática, mais um resumo em texto: chão em cinza pela altura, paredes, água, Link com seta do yaw, planos de transição com as salas de cada lado e atores. |
| `actors(radius=null, category=null, offset=0, limit=128)` | Atores vivos, ordenados por distância 3D, categoria/id/nome/posição/sala. Categorias numéricas 0..11; NPC=4, prop=6, porta=10. |
| `teleport(x,y,z,yaw=null,timeout=3)` | Verifica chão/parede e Player; escreve posição atual, anterior e home, zera velocidade, espera dois updates e bgcheck. |
| `camera_state()` | `eye`, `at`, yaw/pitch/distância, `input_yaw` real, modo, frame e epoch. |
| `camera_set(mode="free",yaw=null,pitch=null,distance=null,eye=null,at=null)` | Use yaw+pitch+distance, opcionalmente at, ou eye+at. `mode="normal"` libera a câmera. |
| `walk_to(x,z,tolerance=20,timeout=15)` | Controle por pad read em linha reta, compensando o yaw de input. Para em parede, queda, timeout ou 1,5 s sem se aproximar do alvo. Retorna progresso. |

Ângulos das ferramentas novas usam **graus**: yaw 0 olha +Z, +90 olha +X; pitch negativo olha para baixo.
`eye` e `at` de entrada são listas `[x,y,z]`; as posições retornadas são objetos `{x,y,z}`.
`rotY`/rotation do mapa e rotation de `game_state` continuam em unidades binárias do jogo (16384 = 90 graus).
`camera_state.yaw` é a direção visual eye→at; `input_yaw` é `Camera_GetInputDirYaw`, usado por `walk_to`.

O mapa usa `PlayState` e o cabeçalho alternativo que o jogo realmente executou. O `PlayState` não guarda contagens
de spawns/entradas/exits; o host as captura dos comandos de cena executados e as serve pelo `linkspan.oot.scene_query`
v1 (`soh/native-sdk/include/oot_scene_query.h`, exportado com o SDK). Esse header é um contrato de serviço versionado
fora dos globs do layout id, então acrescentá-lo não invalida mods compilados; o consumidor ainda confere `layout_id`
e o stride antes de indexar. Num host sem o serviço o agente devolve `complete=false`, listas desconhecidas `null` e
aviso explícito; salas, transições e spawn selecionado continuam disponíveis. Não procura nem abre O2Rs.

`scene_map.entrance_index` é a entrada global; `current_entrance` é o índice local de `setupEntranceList` (`curSpawn`).
Spawns e entradas são listas diferentes: `entrances[i].spawn` indexa `spawns`. Exits são índices da tabela global,
sem inventar coordenadas que não existem nessa lista. Salas são identificadas por id e estado atual/anterior,
sem geometria de piso inferida. `counts` dá totais; avance `offset += limit` enquanto `has_more=true`.
`complete` indica disponibilidade das listas, independentemente da paginação. Refaça páginas se `scene_epoch` mudar.

`transitions[].crossing` traz normal unitária, eixo (`x`, `y`, `z` ou `oblique_xz`) e salas dos sinais negativo/positivo.
O sinal é `dot(Link - transition.position, normal)`. EnHoll horizontal usa normal `(sin(rotY),0,cos(rotY))`;
tipos verticais 1/2/5 usam Y. Tipos 2/5 têm lado 0 acima; tipo 1 tem destino fixo no lado 1.
Tipo 0 antecipa a sala oposta durante o carregamento; tipo 3 depende de switch. Os horizontais 4/6 só trocam a sala
com `50 < |sinal| < 100`, então atravesse o plano em vez de parar nele. `Door_Shutter` usa a semântica
local-Z verificada no host. Outros tipos retornam `semantics="orientation_only"` e salas de sinal `null`, pois
orientação geométrica não prova sua regra de ativação. Os parâmetros permanecem disponíveis para inspeção.

`scene_overview` lê `colCtx.colHeader` dentro do callback do jogo e pagina os polígonos com cursores brutos (até 4096
polígonos lidos e 128 devolvidos por página); o MCP junta até `max_pages` páginas do mesmo `scene_epoch` e desenha o
PNG só com a biblioteca padrão. Chão, parede e teto seguem os limiares de normal do `z_bgcheck.c`. A faixa Y padrão é
Link-200..Link+300 e fica fixa desde a primeira página; estreite-a em mapas com vários andares ou passe
`bbox=[x_min,z_min,x_max,z_max]`. Polígonos não têm sala, então as salas aparecem só por transições, atores e água. A
colisão dinâmica (`DynaPoly`, plataformas) não é desenhada. A imagem fica na pasta da sessão e volta como conteúdo de
imagem do MCP; `partial=true` e `next_offsets` indicam que faltam páginas. Não calcula rota nem prova que uma área é
transitável. Em jogo, Kokiri Forest (1692 polígonos) levou 12 páginas e menos de 1 s.

`actors` aceita raio 0 (só coincidentes), não aceita negativos e usa limite 1..128. Nomes vêm de
`tables/actor_table.h`; tipos dinâmicos podem ter `name=null`. A varredura protege contra ciclos e limita 4096 atores,
informando `truncated`. Páginas representam instantes diferentes; atores podem nascer/desaparecer entre elas.

Teleporte recusa pausa/texto, cutscene, caminhada automática, transição, porta, rolamento, água, escalada e Player
incompatível, inclusive na Epona; não troca action function. Exige Player no chão. Sonda de chão começa em `y+50`, exige chão a até
200 unidades de `y`, normal Y >= 0,5 e destino sem parede. Ajusta Y para chão+1, zera o impulso de esteira e
reinicia a altura de queda no destino. Só `status="grounded"` confirma
posição após bgcheck; `refused`, `timeout`, `scene_changed` ou `unsupported` são resultados distintos. `mutated=true`
informa que houve escrita/controle mesmo quando a confirmação posterior falha; não há rollback nem repetição.
Recusa antes de a operação assumir o pad preserva a fila de `input` pendente.

Câmera usa `linkspan.oot.camera` com posse exclusiva e reaplicação pelo host. Perde a posse em cutscene/troca de
cena; cancel/release, desconexão e shutdown a liberam. A limpeza pendente roda na mesma seção crítica do pedido
seguinte, então um cancel seguido de `camera_set` novo não solta a vista nova. `camera_set` confirma aceitação pelo serviço; consulte
`camera_state` no frame seguinte para verificar a vista efetiva. Não escreve na memória da câmera.
`walk_to` usa input substitutivo neutro entre frames, verifica parede/chão e espera carregamento de sala; a espera
não conta no prazo de 1,5 s, e um pedido que chega durante a carga começa quando ela acaba. Troca de cena/Player
interrompe. Ser empurrado de lado (esteira, ator em movimento) não conta como progresso. Não calcula rotas, não abre portas nem resolve obstáculos. `arrived`, `blocked`,
`refused`, `timeout` e `scene_changed` incluem posição/progresso; sucesso instantâneo não elimina inércia residual.

O retrato ganha `scene_epoch`, `grounded`, `controllable`, `blocked_reason` e `camera`; `wait_for` aceita esses
escalares e `camera.yaw`, `camera.input_yaw`, `camera.mode` (exceto o texto `blocked_reason`).

### Protocolo JSON v2

JSON por linha mantém `{id,op,...}` e `{id,ok,...}`. `hello` do MCP envia `protocol_version:2`; resposta inclui
`protocol_version:2`, `agent_version:"0.2.0"`, `layout_id` e capabilities. O MCP novo recusa agentes sem v2,
incluindo em attach, sem esperar 90 s por uma incompatibilidade permanente. O agente novo aceita hello v1 sem
esse campo para clientes antigos; se informado, o número deve ser inteiro 2.

Operações novas são executadas na thread do jogo. A thread TCP devolve `{ticket,status:"pending"}`; consulte
`{id,op:"result",ticket}` até `{status:"done",result:{...}}`. Há uma operação pendente por vez; conflito retorna
`ok:false` com busy. Último resultado vive até novo pedido/cancelamento. O MCP já faz o polling e registra pedido
e resultado nas evidências. `input` fica busy enquanto uma operação v2 está pendente. Cancelamento, mudança de dono
e desconexão invalidam o ticket e deixam input neutro; limpeza da câmera acontece no próximo callback do jogo.
Sem callback, o cliente cancela após timeout+5 s. Não há replay de mutações em falha de socket.

Argumentos desconhecidos/internos, booleanos no lugar de números, não finitos, timeout fora de 0,1..120 s,
coordenadas além de ±10.000.000, pitch fora de -89..89 e combinações inválidas de câmera são recusados.
Os testes CMake incluem `playtest_gameplay_tests` (serviços simulados + servidor TCP real, com as regressões da revisão
adversarial), além dos testes puros do protocolo/fila/lease. A prova em jogo (Kokiri Forest, pelo EnHoll das salas 0
e 1 e de volta, só com as ferramentas novas) passou no host `2E11252A`.
