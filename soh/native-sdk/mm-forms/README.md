# MM Forms — casca fora do RC

Mod nativo `linkspan.mm.forms`, separado do NEI. Exige o MM Base da tarefa A,
ABI 1.4 e o SDK do mesmo layout do host. Nenhum asset do MM acompanha a casca.

A DLL contém a máquina Goron, bola, socos, defesa, tambores e coreografia da
máscara extraídos do host. A spec do exemplo `goron-form` foi incorporada ao
`main.lua`; a tecla G pede entrada/saída. Sem os recursos, a entrada é recusada.

Chamadas pelo `ship.native.call` deste próprio mod:

- `configure(spec)`: spec de corpo, animações, segmentos e modelos da forma;
- `set_enabled({enabled=bool})`: ativa ou restaura o corpo;
- `play_mask_on_animation({})`: inicia a entrada e devolve `frames`;
- `play_body_animation({name, mode, speed})`: animação própria, inclusive `mask_off`;
- `set_body_segment({segment, path})`: muda uma textura/segmento;
- `status({})`: estado da casca, sempre com `gameplay_validated=false`.

O host conserva somente o corpo genérico Lua e os serviços/hooks da F2.
O mapa Goron (`base=0x6800`, `count=0x100`, `offset=0xC0`) é instalado/limpo
pelo serviço `linkspan.mm.audio`, cujo hook mantém a voz vanilla se faltar áudio.

Configure com CMake 3.26+, MSVC e `-DOOT_NATIVE_SDK=<OotNativeSdk.cmake>`
`-DHOST_EXECUTABLE=<soh.exe>`. `MM_BASE_INCLUDE_DIR` aponta por padrão para
`../mm-base`, onde deve existir `mm_audio.h`. Compile com
`cmake --build <build> --config Release` e teste com `ctest --test-dir <build> -C Release --output-on-failure`.

O fingerprint continua necessário para `ResourceMgr_LoadAnimByName`, único símbolo privado.
Engine/VFS e interpolação usam serviços genéricos. Não há patches/detours nem ligação com soh, shiplua ou Lua
na DLL. O teste Lua, quando disponível, é um executável separado.

Falta prova em jogo: ordem ação/animação/root motion/colisores; enquadramento,
flash e áudio; recursos reais; troca de cena, dano, água, save/load e unload.
O timbre 7 dos tambores conserva a limitação já documentada no port original
(índice do MM entregue à ocarina do OoT). Não foi comprovado com áudio real.
