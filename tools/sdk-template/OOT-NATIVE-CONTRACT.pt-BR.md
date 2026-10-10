[English](OOT-NATIVE-CONTRACT.md) · **Português (Brasil)**

# Contrato de provider nativo OoT

## Fronteira e versionamento

A fronteira do provider é a ABI C versionada de [ship_native_abi.h](include/shiplua/native/ship_native_abi.h). Este header declara `SHIP_NATIVE_ABI_MAJOR = 1u` e `SHIP_NATIVE_ABI_MINOR = 3u`. Não expõe ABI C++ estável nem exige vincular bibliotecas de importação do host/core/Lua. O pacote OoT ainda exige layout e opções de compilação MSVC/x64 exatos porque mods podem converter ponteiros opacos para tipos do host. ABI do provider, versões de serviços, faixa da API Lua e layout ID são verificações separadas.

Exporte `ShipNative_Query` com linkage C, `SHIP_NATIVE_EXPORT` e `SHIP_NATIVE_CALL` (`__cdecl` no Windows). Retorna um `const ShipNativeDescriptor*` com `size`, `abi_major`, `abi_minor`, `init` e `shutdown`. Mantenha o descriptor válido, como o descriptor estático do probe. Query, construtores de DLL e `DllMain` já executam código nativo; esta ABI não é uma sandbox.

## Inicialização e consulta de serviços

`init(const ShipNativeRuntime*, void**)` executa por instância, mesmo se o SO reutilizar uma biblioteca mapeada. Valide tamanho e ABI do runtime antes de acessar campos. Inicialize a saída da instância para permitir limpeza parcial segura. `shutdown(void*)` pode ocorrer após init parcial e deve aceitar instância nula.

`get_service(context, name, version, minimum_size)` retorna nulo para serviço ausente/incompatível. A tabela pertence ao host/provider que a fornece e pode ser retida até o shutdown deste provider; não a chame depois. Peça a versão precisa e o `sizeof` da tabela dessa versão, confira `size` e os membros que vai chamar. Preencha `size` nas structs versionadas de argumento/resultado. A presença do header não comprova que o serviço esteja disponível no host em execução ou que o coremod fornecedor esteja instalado.

`register_function` só é permitido no init e dá um nome local ao mod a uma função nativa de requisição/resposta. `register_service` publica no init a tabela de uma core extension (ABI 1.1); ela deve viver até shutdown. Providers não expõem ponteiros pelo Lua. Ponteiros de requisição/writer valem durante a chamada; `ShipNativeWriteFn` copia bytes sem assumir ownership.

## Layout ID e tamanhos de structs

[oot_layout_id.h](include/oot_layout_id.h) define `LINKSPAN_OOT_LAYOUT_ID`; [linkspan-sdk.json](linkspan-sdk.json) registra o valor completo em `ootLayoutId`. Não use somente o sufixo de oito caracteres do nome do arquivo para verificar compatibilidade.

Antes de converter ponteiros de engine, compare `ShipOotEngineV1.layout_id` com o macro completo e compare `play_state_size`, `player_size` e `save_context_size` com `sizeof(PlayState)`, `sizeof(Player)` e `sizeof(SaveContext)` dos [headers do host incluídos](include/host/include/z64.h). As tabelas de movement também trazem `layout_id`. Consulte a [implementação do probe](examples/provider-example/provider.cpp).

A receita atual `oot-native-v2` calcula o hash do conjunto ordenado de headers públicos de `soh/include` do host, `soh/soh/native` do host e `include/shiplua/native` do core, além de tamanho de ponteiro, ID/versão do compilador, plataforma do gerador e flags C++/Release. O CRLF dos headers é normalizado para LF antes do hash. O script confere o ID já gerado contra essas entradas em vez de gerar outro para esconder drift. Leia os valores do bundle real; este documento não fixa hash de uma release específica. Recompile e reempacote com um novo SDK correspondente quando essas entradas de compatibilidade mudarem.

## Regras de thread, ponteiro e buffer

Serviços OoT de gameplay e hooks nativos exigem a thread do jogo, salvo contexto diferente explícito no header. Ponteiros de engine não sobrevivem a frames/cenas. Payloads de hook e ponteiros de enumeração valem durante o callback. O lifetime maior da tabela de serviço não estende o lifetime dos resultados. Nunca deduza ownership a partir de `void*`; consulte cada header. Buffers de saída do chamador continuam pertencendo ao chamador. Use consultas de tamanho com nulo/capacidade zero somente onde a função específica documenta esse uso.

**Exceção de recursos:** `ShipOotJsonTranscodeFn` em [oot_resources.h](include/oot-native/oot_resources.h) executa na thread que carrega o recurso, possivelmente num pool. Proteja o estado do provider; não chame serviços exclusivos da thread do jogo nesse contexto. O header permite explicitamente ler camadas com `read_file_layers` para transcodificar. Writer e argumentos do callback continuam limitados à chamada.

**Escopo de draw:** operações de matriz/lista de render e desenho de esqueleto exigem escopo de draw permitido. Display lists nativas e a geometria referenciada devem permanecer válidas até o fim do frame. Ponteiros de identidade `LightInfo` das luzes servem para comparação, não escrita direta. Edições de tabelas de texto devem ocorrer fora da exibição de uma caixa de texto. Siga as [regras de cada serviço](SERVICES.pt-BR.md).

## Hooks e ciclo de vida

A ABI 1.2 acrescenta `register_hook`, `unregister_hook`, `declare_hook_point` e `dispatch_hook`. Nomes, versões, modos permitidos e payloads dos pontos vêm de [oot_hooks.h](include/oot-native/oot_hooks.h). Ele define pontos de hook, não uma tabela de serviço `oot.hooks`.

Selecione exatamente um entre `SHIP_NATIVE_HOOK_OBSERVE`, `SHIP_NATIVE_HOOK_TRANSFORM` e `SHIP_NATIVE_HOOK_REPLACE`. OBSERVE não deve escrever no payload. A ordem é observadores BEFORE, cadeia de transform, replace/original e observadores AFTER. Prioridade maior executa antes; empates seguem a ordem de registro. Só um REPLACE pode ocupar o ponto. `call_original` só é não nulo para REPLACE e pode ser chamado no máximo uma vez. `original_context` é opaco. Três retornos consecutivos de falha desativam o hook; `UNSUPPORTED` é recusa, não falha. Um transform que falha restaura o payload, mas o armazenamento apontado pode exigir restauração explícita: o header de room-actors exige que transforms com falha deixem `entries` intacto.

Core extensions só declaram pontos no init; somente o dono os despacha. O loader remove hooks do mod antes do shutdown. Consumidores de frameworks descarregam antes do provider de seus serviços conforme as dependências. Declare sua dependência de framework e desfaça suas próprias mutações de gameplay/CVars. Remova tipos de ator/item/cena registrados e libere os handles de serviço que possui conforme os headers.

## ABI 1.3 e fingerprints do executável

`get_host_fingerprint` está disponível a qualquer provider: copia o SHA-256 do executável em 64 bytes hexadecimais sem terminador NUL. Saída nula e capacidade zero consultam o tamanho. `resolve_symbol`, `install_patch` e `remove_patch` exigem core extension com ABI `"1.3"` no manifesto cujo `[provider].host_fingerprints` contenha o hash do executável em execução; caso contrário, retornam `UNSUPPORTED`. Somente layout correspondente não basta.

Nomes de símbolo são o nome único de função ou `file.c!Function` para nomes static repetidos. Nome ausente/ambíguo retorna `UNSUPPORTED`. Patches têm como alvo o executável, devolvem trampolim para o original e recusam segundo patch no mesmo endereço com `LIMIT`. Símbolos do host devem corresponder ao executável exato. Patches de init ativam em conjunto depois do retorno do init: não dependa deles no init nem instale patches no shutdown. O loader remove patches antes do shutdown; falha de remoção pode colocar a DLL em quarentena para evitar descarregar código ainda usado. O SDK não inclui executável nem arquivo de símbolos.

## Status e opções de compilação

A ABI define `SHIP_NATIVE_OK = 0u`, `SHIP_NATIVE_INVALID_ARGUMENT = 1u`, `SHIP_NATIVE_UNSUPPORTED = 2u`, `SHIP_NATIVE_FAILURE = 3u` e `SHIP_NATIVE_LIMIT = 4u`. As condições precisas dependem de cada operação.

Use o config Release e o toolchain MSVC Windows x64 correspondente. Preserve C++20, `/MT`, `/utf-8`, `/Zc:preprocessor`, `/fp:precise`, `/fp:except-` e as definições exportadas. Consulte [AUTHOR-GUIDE.pt-BR.md](AUTHOR-GUIDE.pt-BR.md) para um exemplo conferido de build/manifesto e [SERVICES.pt-BR.md](SERVICES.pt-BR.md) para cada serviço/versão declarados. Estes documentos descrevem o contrato dos fontes; não certificam DLL compilada nem sessão de gameplay.
