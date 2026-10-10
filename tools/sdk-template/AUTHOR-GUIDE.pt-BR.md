[English](AUTHOR-GUIDE.md) · **Português (Brasil)**

# Compile e empacote um mod nativo OoT

## 1. Selecione o SDK correspondente

Use Windows x64, CMake 3.26+, Python 3.11+ para a validação estrutural do CLI incluído e a **versão exata do compilador MSVC em `compiler.version`** de [linkspan-sdk.json](linkspan-sdk.json). Use um Developer PowerShell x64. O config recusa outra versão do compilador, compilador diferente do MSVC ou destino diferente de Windows/x64. Os exemplos usam C++20 e Release `/MT` (`MSVC_RUNTIME_LIBRARY MultiThreaded`). Preserve as definições e opções de compilação do SDK.

Leia `ootLayoutId`, `nativeProviderAbi`, `hostSha256`, `configuration` e `compiler` em [linkspan-sdk.json](linkspan-sdk.json). Mantenha [checksums.sha256](checksums.sha256) junto do SDK. Compile fora do checkout do host e da instalação do jogo. O ZIP do SDK é um kit de desenvolvimento; somente o pacote resultante do seu mod vai em `mods/`.

## 2. Compile o probe de compatibilidade

```powershell
$sdk = (Resolve-Path .\LinkSpan-OoT-SDK).Path
cmake -S "$sdk/examples/provider-example" -B .\probe-build -A x64 "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build .\probe-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

Use [examples/provider-example/provider.cpp](examples/provider-example/provider.cpp) como ponto de partida mínimo. Seu [CMakeLists.txt](examples/provider-example/CMakeLists.txt) é:

```cmake
cmake_minimum_required(VERSION 3.26)
project(LinkSpanProviderExample LANGUAGES CXX)
find_package(LinkSpanOotSdk CONFIG REQUIRED)
add_library(linkspan_sdk_probe SHARED provider.cpp)
target_link_libraries(linkspan_sdk_probe PRIVATE LinkSpan::OotSdk)
set_target_properties(linkspan_sdk_probe PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(linkspan_sdk_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/mod/provider")
endforeach()
configure_file(manifest.toml "${CMAKE_BINARY_DIR}/mod/manifest.toml" COPYONLY)
configure_file(main.lua "${CMAKE_BINARY_DIR}/mod/main.lua" COPYONLY)
```

`find_package` localiza [LinkSpanOotSdkConfig.cmake](lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake). `LinkSpan::OotSdk` é um target INTERFACE: includes, definições de compilação, C++20 e opções do compilador, sem biblioteca de importação do host. Exporta `/utf-8`, `/Zc:preprocessor`, `/fp:precise` e `/fp:except-`. As bibliotecas padrão de C++/Windows continuam sendo responsabilidade do toolchain. Não vincule `soh`, o core, Lua nem libultraship. Para projetos antigos, [OotNativeSdk.cmake](OotNativeSdk.cmake) inclui a mesma configuração relocável.

## 3. Dê uma identidade própria ao mod

Copie [examples/provider-example/](examples/provider-example/README.md) para seu projeto. Renomeie em conjunto o target CMake, ID do manifesto, nome de exibição, caminho da DLL e pacote de saída. Mantenha manifesto e entrypoint Lua na raiz do mod; coloque a DLL no caminho relativo declarado por `[provider].win64`. O manifesto do probe é:

```toml
id = "example.linkspan_sdk_probe"
name = "Link-Span SDK Probe"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_sdk_probe.dll"
```

A faixa da API Lua do manifesto e a ABI do provider são contratos de versão separados. O manifesto do probe pede ABI 1.2; seu descriptor usa os macros atuais do header (1.3). Preserve este exemplo conferido ou selecione a ABI do manifesto exigida pelas funções usadas. O entrypoint [main.lua](examples/provider-example/main.lua) requer `ship` sem alterar gameplay.

## 4. Valide runtime e engine antes de usar ponteiros

O probe exporta `ShipNative_Query` com linkage C e `SHIP_NATIVE_CALL`, depois consulta engine por nome, versão e tamanho mínimo. Confere o layout ID completo e cada tamanho de struct do host:

```cpp
#include <cstring>
#include "shiplua/native/ship_native_abi.h"
#include "oot_engine.h"
#include "oot_layout_id.h"
#include "z64.h"

static_assert(sizeof(void*) == 8, "Windows x64 requerido");

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!instance) return SHIP_NATIVE_FAILURE;
    *instance = nullptr;
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || !runtime->get_service)
        return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION,
        sizeof(ShipOotEngineV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) != 0 ||
        engine->play_state_size != sizeof(PlayState) || engine->player_size != sizeof(Player) ||
        engine->save_context_size != sizeof(SaveContext)) return SHIP_NATIVE_UNSUPPORTED;
    // A prova é somente consultar o contrato. Nenhum ponteiro de gameplay é retido.
    return SHIP_NATIVE_OK;
}

static void SHIP_NATIVE_CALL Shutdown(void*) {}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor = {
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
```

Leia o [header da ABI](include/shiplua/native/ship_native_abi.h), o [header de engine](include/oot-native/oot_engine.h), o [header de layout gerado](include/oot_layout_id.h), o [contrato nativo](OOT-NATIVE-CONTRACT.pt-BR.md) e os [serviços](SERVICES.pt-BR.md) antes de ampliá-lo. Serviço ausente produz resultado nulo, não uma tabela utilizável. Preencha os campos `size` de entrada/saída exigidos pela struct específica. Consulte novamente os ponteiros de gameplay e use-os somente na thread e no lifetime documentados.

## 5. Inspecione e empacote

Use o executável correspondente real em `game/soh.exe` neste exemplo:

```powershell
python "$sdk/tools/shipmod.py" validate .\probe-build\mod
if ($LASTEXITCODE -ne 0) { throw 'Validation failed' }
python "$sdk/tools/shipmod.py" doctor .\probe-build\mod --host .\game\soh.exe
if ($LASTEXITCODE -ne 0) { throw 'Doctor failed' }
& "$sdk/tools/package-linkspan-mod.ps1" -Source .\probe-build\mod -OutputPath .\SDK-Probe.shipmod
```

O ZIP deve ter `manifest.toml` na raiz, sem diretório pai extra. O empacotador incluído aceita `.zip` ou `.shipmod`, recusa caminho de saída existente e cria entradas com `/`. Não use `Compress-Archive` nem `ZipFile.CreateFromDirectory` do Windows PowerShell 5.1 neste fluxo.

`validate` delega para `shiplua_manifest_validator` quando disponível; caso contrário, usa checagens estruturais básicas. O binário do validador C++ não está no SDK. `doctor <mod> --host <exe>` inspeciona exports/imports PE, ABI e fingerprints sem carregar a DLL. `doctor` sem mod se destina a um repositório completo de fontes. `shipmod.py` não tem comando `pack`: use o script PowerShell acima. `test` exige runner externo por `SHIPLUA_MOD_TEST_RUNNER`; esse binário não está incluído.

## 6. Instale e verifique no host

Feche o host antes de substituir pacotes. Faça backup do mod anterior e dos saves/configurações afetados por mudança de schema. Instale somente o ZIP/`.shipmod` do seu mod em `mods/`, ao lado do `soh.exe` correspondente. Confirme carga e init no log, exercite a função e depois verifique trocas de cena, save/load e shutdown quando pertinentes. Compilação e validação estrutural sozinhas não comprovam o comportamento no jogo. Para rollback, restaure o pacote anterior e o backup de save/configuração necessário.

Não retenha ponteiros emprestados de gameplay e callback no estado persistente do mod. Libere seus recursos registrados e desfaça suas próprias mutações no shutdown, inclusive após init parcial. DLLs nativas executam no processo do host; use providers de fontes confiáveis.

## Frameworks e exemplos

Core extensions usam `kind = "core_extension"` e `load_phase = "pre_game"`. Publicar serviços exige ABI 1.1; hooks nativos exigem ABI 1.2. Operações de símbolos/patches do escape hatch da ABI 1.3 também exigem core extension e `host_fingerprints` exatos do executável; consulte o [contrato nativo](OOT-NATIVE-CONTRACT.pt-BR.md).

O [DMR](examples/example/CMakeLists.txt) usa o hash do host do SDK no manifesto gerado, mas omite texturas do HUD. [Heart Seeds](examples/heart-seeds/CMakeLists.txt) consulta `linkspan.nei.items` e depende de `linkspan.nei` instalado na versão `>=0.3.0 <0.4.0`. Seu manifesto e CMake gerados fazem parte do SDK, e o script exige seus dois arquivos-fonte. Nem o header nem o build instalam o framework NEI por você.

Mais demos: [link-span-examples](https://github.com/BaiterYamato/link-span-examples). Leitura complementar: [guia do core](https://github.com/BaiterYamato/link-span/blob/main/docs/native-mods.md), [guia do host](https://github.com/BaiterYamato/Shipwright/blob/agent/OOT-UPSTREAM-001-78dc6d970-integration/docs/native-mods.md), [LICENSE](LICENSE) e [NOTICE.md](NOTICE.md).
