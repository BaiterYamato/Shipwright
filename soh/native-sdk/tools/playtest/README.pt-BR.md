# Ferramenta de playtest do Link-Span OoT

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
C++ puro não liga código de socket nem do jogo. Python usa somente `mcp` e a biblioteca padrão; os testes usam
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
