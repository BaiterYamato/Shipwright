#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$HostRoot,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version,
    # Opcional: exe congelado do candidato, para não ler um output em rebuild concorrente.
    [string]$HostExecutable
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Arquivo obrigatório ausente: $Path" }
    return (Resolve-Path -LiteralPath $Path).Path
}
function Write-Text([string]$Path, [string]$Content) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}
function Hash-Text([string]$Content) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Content))) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Copy-Required([string]$Source, [string]$Relative) {
    $inputPath = Require-File $Source
    $destination = Join-Path $stage $Relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath $inputPath -Destination $destination
}
function Cache-Value([string]$Name) {
    $match = [regex]::Match($cacheText, '(?m)^' + [regex]::Escape($Name) + ':[^=]+=(.*)\r?$')
    if (-not $match.Success) { throw "Campo CMake obrigatório ausente: $Name" }
    return $match.Groups[1].Value.TrimEnd("`r")
}
function Compiler-Value([string]$Name) {
    $match = [regex]::Match($compilerText, 'set\(' + [regex]::Escape($Name) + '\s+"([^"]*)"\)')
    if (-not $match.Success) { throw "Metadado do compilador ausente: $Name" }
    return $match.Groups[1].Value
}

$HostRoot = (Resolve-Path -LiteralPath $HostRoot).Path.TrimEnd('\')
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path.TrimEnd('\')
$outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir).TrimEnd('\')
# OutDir é explícito. Nesta tarefa todas as execuções usam teste-saida sob o workspace.
if ($outputRoot -eq $HostRoot -or $outputRoot -eq $BuildDir) { throw 'OutDir deve ser uma pasta de artefatos separada, não a raiz das entradas.' }
$coreRoot = Join-Path $HostRoot 'extern/ship-lua'
$sdkFile = Require-File (Join-Path $BuildDir 'native-sdk/Release/OotNativeSdk.cmake')
$layoutFile = Require-File (Join-Path $BuildDir 'native-sdk/oot_layout_id.h')
$exportFile = Require-File (Join-Path $HostRoot 'soh/native-sdk/ExportSdk.cmake')
$layoutMatch = [regex]::Match([IO.File]::ReadAllText($layoutFile), '#define\s+LINKSPAN_OOT_LAYOUT_ID\s+"([0-9a-f]{64})"')
if (-not $layoutMatch.Success) { throw "LINKSPAN_OOT_LAYOUT_ID inválido: $layoutFile" }
$layoutId = $layoutMatch.Groups[1].Value
$sdkText = [IO.File]::ReadAllText($sdkFile)
$definitionsMatch = [regex]::Match($sdkText, 'set\(OOT_NATIVE_COMPILE_DEFINITIONS\s+\[==\[(.*?)\]==\]\)', [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $definitionsMatch.Success) { throw "OOT_NATIVE_COMPILE_DEFINITIONS ausente: $sdkFile" }
$definitions = $definitionsMatch.Groups[1].Value
if ($definitions -match '(?i)[a-z]:[/\\]|\$<|\$\{') { throw 'Defines não portáveis ou não avaliados no SDK Release.' }

# Confere a receita v2, os headers atuais e o toolchain contra o id já gerado.
# Não recalcula um novo id para encobrir drift e não configura/compila o host.
$exportText = [IO.File]::ReadAllText($exportFile)
if (-not $exportText.Contains('oot-native-v2;${CMAKE_SIZEOF_VOID_P};${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_GENERATOR_PLATFORM};${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_RELEASE}')) {
    throw 'Receita de layout desconhecida; revise o empacotador para este ExportSdk.cmake.'
}
$cacheText = [IO.File]::ReadAllText((Require-File (Join-Path $BuildDir 'CMakeCache.txt')))
$compilerFiles = @(Get-ChildItem -LiteralPath (Join-Path $BuildDir 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -File)
if ($compilerFiles.Count -ne 1) { throw 'Esperado exatamente um CMakeCXXCompiler.cmake no BuildDir.' }
$compilerText = [IO.File]::ReadAllText($compilerFiles[0].FullName)
$compilerId = Compiler-Value 'CMAKE_CXX_COMPILER_ID'
$compilerVersion = Compiler-Value 'CMAKE_CXX_COMPILER_VERSION'
$pointerSize = Compiler-Value 'CMAKE_CXX_SIZEOF_DATA_PTR'
$platform = Cache-Value 'CMAKE_GENERATOR_PLATFORM'
$flags = Cache-Value 'CMAKE_CXX_FLAGS'
$releaseFlags = Cache-Value 'CMAKE_CXX_FLAGS_RELEASE'
# O CMakeLists raiz sobrepõe a variável normal (não CACHE) antes de ExportSdk.
# O cache sozinho não representa os inputs do hash neste host.
$rootCmake = [IO.File]::ReadAllText((Require-File (Join-Path $HostRoot 'CMakeLists.txt')))
if (-not $rootCmake.Contains('set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")')) {
    throw 'Override Release da receita não reconhecido; exporte metadados de layout no host antes de adaptar.'
}
$releaseFlags = '-O2 -DNDEBUG'
if ($compilerId -ne 'MSVC' -or $pointerSize -ne '8' -or $platform -ne 'x64') { throw 'Este bundle exige SDK Release MSVC Windows x64.' }
$layoutHeaders = [string[]]@(
    foreach ($root in @((Join-Path $HostRoot 'soh/include'), (Join-Path $HostRoot 'soh/soh/native'), (Join-Path $coreRoot 'include/shiplua/native'))) {
        Get-ChildItem -LiteralPath $root -Filter '*.h' -Recurse -File | ForEach-Object { $_.FullName.Replace('\', '/') }
    }
)
[Array]::Sort($layoutHeaders, [StringComparer]::Ordinal)
$material = 'oot-native-v2;' + $pointerSize + ';' + $compilerId + ';' + $compilerVersion + ';' + $platform + ';' + $flags + ';' + $releaseFlags
foreach ($header in $layoutHeaders) { $material += Hash-Text ([IO.File]::ReadAllText($header).Replace("`r`n", "`n")) }
if ((Hash-Text $material) -cne $layoutId) { throw 'Headers/toolchain atuais divergem do oot_layout_id.h. Use fontes e SDK congelados do mesmo build.' }

$module = Require-File (Join-Path $coreRoot 'tools/LinkSpanPackaging.psm1')
Import-Module $module -Force
$hostCandidates = @((Join-Path $BuildDir 'x64/Release/soh.exe'), (Join-Path $BuildDir 'Release/soh.exe'), (Join-Path $HostRoot 'x64/Release/soh.exe'))
$hostExecutable = if ($HostExecutable) { Require-File $HostExecutable } else {
    $hostCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $hostExecutable) { throw 'soh.exe Release obrigatório para vincular o SDK ao host (sem incluir o exe no ZIP).' }
try { $hostBytes = [IO.File]::ReadAllBytes($hostExecutable) }
catch { throw "soh.exe não pode ser lido de forma estável (arquivo em uso/rebuild): $hostExecutable. Use -HostExecutable com a cópia congelada do candidato." }
if (-not ([Text.Encoding]::ASCII.GetString($hostBytes)).Contains($layoutId)) { throw 'soh.exe não contém o layout do SDK.' }
$hostSha = [Security.Cryptography.SHA256]::Create()
try { $hostHash = ([BitConverter]::ToString($hostSha.ComputeHash($hostBytes)) -replace '-', '').ToLowerInvariant() }
finally { $hostSha.Dispose(); $hostBytes = $null }
$zipName = 'LinkSpan-OoT-SDK-' + $Version + '-layout-' + $layoutId.Substring(0, 8) + '-Win64.zip'
$finalZip = Join-Path $outputRoot $zipName
if (Test-Path -LiteralPath $finalZip) { throw "Destino já existe: $finalZip" }
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$stage = Join-Path $outputRoot ('.linkspan-sdk-' + [guid]::NewGuid().ToString('N'))
$temporaryZip = $stage + '.zip'
$stagePrefix = $outputRoot + '\.linkspan-sdk-'
if (-not ([IO.Path]::GetFullPath($stage)).StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Stage fora de OutDir.' }
[IO.Directory]::CreateDirectory($stage) | Out-Null
try {
    # Raízes explícitas; nenhum include absoluto do OotNativeSdk.cmake é copiado.
    $roots = @(
        @{ source = (Join-Path $HostRoot 'soh/include'); dest = 'include/host/include' },
        @{ source = (Join-Path $HostRoot 'soh/soh/native'); dest = 'include/oot-native' },
        @{ source = (Join-Path $coreRoot 'include'); dest = 'include' },
        @{ source = (Join-Path $HostRoot 'libultraship/include'); dest = 'include/lus' },
        @{ source = (Join-Path $HostRoot 'soh'); dest = 'include/host' },
        @{ source = (Join-Path $HostRoot 'soh/native-sdk/nei-core/include'); dest = 'include' },
        @{ source = (Join-Path $BuildDir 'native-sdk'); dest = 'include' }
    )
    $queue = [Collections.Generic.Queue[string]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seeds = @((Join-Path $HostRoot 'soh/include/z64.h'), (Join-Path $coreRoot 'include/shiplua/native/ship_native_abi.h'),
        (Join-Path $HostRoot 'soh/native-sdk/nei-core/include/linkspan/nei/nei_items.h'), $layoutFile)
    $seeds += @(Get-ChildItem -LiteralPath (Join-Path $HostRoot 'soh/soh/native') -Filter 'oot_*.h' -File | ForEach-Object FullName)
    foreach ($seed in $seeds) { $queue.Enqueue((Require-File $seed)) }
    # Apenas headers do C/C++ e Windows SDK podem ficar por conta do toolchain.
    $systemHeaders = '^(algorithm|array|atomic|bit|cassert|cctype|cerrno|chrono|cmath|cstddef|cstdint|cstdio|cstdlib|cstring|filesystem|functional|iterator|limits|map|memory|mutex|new|optional|set|span|string|string_view|system_error|thread|tuple|type_traits|unordered_map|utility|vector|assert\.h|float\.h|limits\.h|math\.h|stdarg\.h|stdbool\.h|stddef\.h|stdint\.h|stdio\.h|stdlib\.h|string\.h|windows\.h)$'
    while ($queue.Count -gt 0) {
        $header = $queue.Dequeue()
        if (-not $seen.Add($header)) { continue }
        $mapping = $roots | Where-Object { $header.StartsWith($_.source.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) } |
            Sort-Object { $_.source.Length } -Descending | Select-Object -First 1
        if (-not $mapping) { throw "Header sem raiz portátil: $header" }
        $relative = $mapping.dest + '/' + $header.Substring($mapping.source.TrimEnd('\').Length + 1).Replace('\', '/')
        Copy-Required $header $relative
        $includes = [regex]::Matches([IO.File]::ReadAllText($header), '(?m)^\s*#\s*include\s*[<"]([^>"]+)[>"]')
        foreach ($inc in $includes) {
            $name = $inc.Groups[1].Value
            if ($name -match $systemHeaders) { continue }
            $search = @((Join-Path ([IO.Path]::GetDirectoryName($header)) $name))
            $search += @($roots | ForEach-Object { Join-Path $_.source $name })
            $dependency = $search | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
            if (-not $dependency) { throw "Include obrigatório '$name' não resolvido em $header" }
            $queue.Enqueue((Resolve-Path -LiteralPath $dependency).Path)
        }
    }

    $configTemplate = @'
# Configuração Release portátil. Nenhuma dependência de link com o host.
set(LinkSpanOotSdk_VERSION "@VERSION@")
set(LinkSpanOotSdk_LAYOUT_ID "@LAYOUT@")
set(LinkSpanOotSdk_HOST_SHA256 "@HOSTHASH@")
set(LinkSpanOotSdk_COMPILER_VERSION "@COMPILER@")
get_filename_component(_linkspan_sdk_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT MSVC OR NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "LinkSpan OoT SDK requer MSVC Windows x64")
endif()
if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL LinkSpanOotSdk_COMPILER_VERSION)
    message(FATAL_ERROR "Use MSVC ${LinkSpanOotSdk_COMPILER_VERSION}; SDK tem layout específico do compilador")
endif()
set(OOT_NATIVE_INCLUDE_DIRS
    "${_linkspan_sdk_root}/include"
    "${_linkspan_sdk_root}/include/host/include"
    "${_linkspan_sdk_root}/include/oot-native"
    "${_linkspan_sdk_root}/include/lus"
    "${_linkspan_sdk_root}/include/host")
set(OOT_NATIVE_COMPILE_DEFINITIONS [==[@DEFINITIONS@]==])
if(NOT TARGET LinkSpan::OotSdk)
    add_library(LinkSpan::OotSdk INTERFACE IMPORTED)
    set_target_properties(LinkSpan::OotSdk PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OOT_NATIVE_INCLUDE_DIRS}"
        INTERFACE_COMPILE_DEFINITIONS "${OOT_NATIVE_COMPILE_DEFINITIONS}"
        INTERFACE_COMPILE_FEATURES cxx_std_20
        INTERFACE_COMPILE_OPTIONS "/utf-8;/Zc:preprocessor;/fp:precise;/fp:except-")
endif()
unset(_linkspan_sdk_root)
'@
    $config = $configTemplate.Replace('@VERSION@', $Version).Replace('@LAYOUT@', $layoutId).Replace('@HOSTHASH@', $hostHash).Replace('@COMPILER@', $compilerVersion).Replace('@DEFINITIONS@', $definitions)
    Write-Text (Join-Path $stage 'lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake') ($config + "`n")
    # Ponte compatível com exemplos antigos, também relocável.
    Write-Text (Join-Path $stage 'OotNativeSdk.cmake') 'include("${CMAKE_CURRENT_LIST_DIR}/lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake")'

    foreach ($example in @('example', 'shovel-demo', 'item-demo')) {
        $exampleRoot = Join-Path $HostRoot ('soh/native-sdk/' + $example)
        foreach ($file in (Get-ChildItem -LiteralPath $exampleRoot -File | Where-Object { $_.Extension -in @('.cpp', '.h', '.lua', '.toml', '.md') })) {
            Copy-Required $file.FullName ('examples/' + $example + '/' + $file.Name)
        }
        $target = switch ($example) { 'example' { 'dynamic_movement_remake' } 'shovel-demo' { 'linkspan_shovel_demo' } 'item-demo' { 'linkspan_item_demo' } }
        $sources = switch ($example) { 'example' { 'provider.cpp package_assets.cpp' } 'shovel-demo' { 'shovel_demo.cpp' } 'item-demo' { 'item_demo.cpp' } }
        foreach ($source in $sources.Split(' ')) { Require-File (Join-Path $exampleRoot $source) | Out-Null }
        $exampleCmake = @'
cmake_minimum_required(VERSION 3.26)
project(LinkSpanOotExample LANGUAGES CXX)
find_package(LinkSpanOotSdk CONFIG REQUIRED)
add_library(@TARGET@ SHARED @SOURCES@)
target_link_libraries(@TARGET@ PRIVATE LinkSpan::OotSdk)
target_compile_definitions(@TARGET@ PRIVATE JUMP_VELOCITY=7.0)
set_target_properties(@TARGET@ PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mod/provider")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(@TARGET@ PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/mod/provider")
endforeach()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.lua")
    configure_file(main.lua "${CMAKE_BINARY_DIR}/mod/main.lua" COPYONLY)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/manifest.toml")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifest.toml" manifest_text)
    # Usado pelo DMR (ABI 1.3). Mesmo layout não basta para funções internas.
    file(WRITE "${CMAKE_BINARY_DIR}/mod/manifest.toml" "${manifest_text}\nhost_fingerprints = [\"${LinkSpanOotSdk_HOST_SHA256}\"]\n")
endif()
# Apenas prova de compilação para estes exemplos: assets do DMR não acompanham.
# Debug usa /MT para reproduzir o contrato Release; o gate é --config Release.
'@
        Write-Text (Join-Path $stage ('examples/' + $example + '/CMakeLists.txt')) ($exampleCmake.Replace('@TARGET@', $target).Replace('@SOURCES@', $sources) + "`n")
    }
    # Exemplo mínimo independente, com verificação de layout e sizeof reais.
    foreach ($file in @('CMakeLists.txt', 'provider.cpp', 'manifest.toml', 'main.lua', 'README.md')) {
        Copy-Required (Join-Path $PSScriptRoot ('sdk-template/provider-example/' + $file)) ('examples/provider-example/' + $file)
    }
    # Add-on de exemplo do NEI (Heart Seeds): o fonte do demo, com manifesto e CMake próprios do SDK.
    $heartRoot = Join-Path $HostRoot 'soh/native-sdk/nei-core/demo'
    foreach ($file in @('nei_demo.cpp', 'main.lua')) { Copy-Required (Join-Path $heartRoot $file) ('examples/heart-seeds/' + $file) }
    $heartPresent = $true
    $heartManifest = @'
id = "linkspan.nei-demo"
name = "Not Enough Items Demo"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_nei_demo.dll"

[dependencies]
"linkspan.nei" = ">=0.3.0 <0.4.0"
'@
    Write-Text (Join-Path $stage 'examples/heart-seeds/manifest.toml') ($heartManifest + "`n")
    $heartCmake = @'
cmake_minimum_required(VERSION 3.26)
project(LinkSpanNeiHeartSeeds LANGUAGES CXX)
find_package(LinkSpanOotSdk CONFIG REQUIRED)
add_library(linkspan_nei_demo SHARED nei_demo.cpp)
target_link_libraries(linkspan_nei_demo PRIVATE LinkSpan::OotSdk)
# nei_demo.cpp inclui "include/linkspan/nei/nei_items.h" a partir da raiz do SDK.
target_include_directories(linkspan_nei_demo PRIVATE "${LinkSpanOotSdk_DIR}/../../..")
set_target_properties(linkspan_nei_demo PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(linkspan_nei_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/mod/provider")
endforeach()
configure_file(manifest.toml "${CMAKE_BINARY_DIR}/mod/manifest.toml" COPYONLY)
configure_file(main.lua "${CMAKE_BINARY_DIR}/mod/main.lua" COPYONLY)
'@
    Write-Text (Join-Path $stage 'examples/heart-seeds/CMakeLists.txt') ($heartCmake + "`n")
    foreach ($file in @('writing-mods.md', 'writing-mods.en.md', 'native-providers.md', 'shipmod-cli.md', 'shipmod-cli.en.md', 'manifest-validator.md', 'manifest-validator.en.md', 'host-integration.md', 'host-integration.en.md')) {
        Copy-Required (Join-Path $coreRoot ('docs/' + $file)) ('docs/' + $file)
    }
    foreach ($dir in @('api', 'architecture')) {
        foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $coreRoot ('docs/' + $dir)) -Filter '*.md' -Recurse -File)) {
            Copy-Required $file.FullName ('docs/' + $dir + '/' + $file.Name)
        }
    }
    foreach ($file in @('0021-native-hooks.md', '0023-native-escape-hatch.md', '0024-native-crash-attribution.md')) {
        Copy-Required (Join-Path $coreRoot ('rfcs/' + $file)) ('docs/rfcs/' + $file)
    }
    foreach ($file in @('shipmod.py', 'LinkSpanPackaging.psm1')) { Copy-Required (Join-Path $coreRoot ('tools/' + $file)) ('tools/' + $file) }
    foreach ($file in @('package-linkspan-mod.ps1', 'linkspan-zip.ps1', 'scan-protected-content.py')) { Copy-Required (Join-Path $HostRoot ('tools/' + $file)) ('tools/' + $file) }
    foreach ($file in @('README.md', 'AUTHOR-GUIDE.pt-BR.md', 'AUTHOR-GUIDE.en.md', 'OOT-NATIVE-CONTRACT.md', 'NOTICE.md')) {
        Copy-Required (Join-Path $PSScriptRoot ('sdk-template/' + $file)) $file
    }
    Copy-Required (Join-Path $HostRoot 'soh/native-sdk/README.md') 'docs/oot-native-sdk-source.md'
    Copy-Required (Join-Path $HostRoot 'libultraship/LICENSE') 'licenses/libultraship-LICENSE.txt'
    # CC0 do trabalho do Link-Span; NOTICE.md (do template) separa os headers do Shipwright e do libultraship.
    Copy-Required (Join-Path $HostRoot 'docs/licensing/LICENSE-PACKAGE.txt') 'LICENSE'
    # Documentação histórica é preservada com aviso explícito sobre afirmação obsoleta.
    $nativeDoc = Join-Path $stage 'docs/native-providers.md'
    $historical = [IO.File]::ReadAllText($nativeDoc)
    Write-Text $nativeDoc ("> Nota deste bundle: o OoT RC4 já integra os serviços nativos. A frase abaixo sobre ausência de integração é histórica; consulte ../OOT-NATIVE-CONTRACT.md. Exemplos genéricos do core não acompanham este recorte.`n`n" + $historical)
    $metadata = [ordered]@{
        schemaVersion = 1; packageType = 'linkspan.oot.sdk'; version = $Version; game = 'oot'; platform = 'windows-x64'
        configuration = 'Release'; nativeProviderAbi = '1.3'; ootLayoutId = $layoutId
        hostSha256 = $hostHash; hostRequired = 'Shipwright 9.2.3 com overlay Link-Span correspondente'
        compiler = [ordered]@{ id = $compilerId; version = $compilerVersion; pointerSize = 8; runtime = 'MultiThreaded'; layoutHashFlags = $flags; layoutHashReleaseFlags = $releaseFlags }
        importLibraries = @(); headerCount = $seen.Count; heartSeedsPresent = [bool]$heartPresent
        limitations = @('Assets do DMR (texturas do HUD) não acompanham o SDK; o pacote do DMR traz os seus', 'item-demo é fonte de referência, sem manifesto', 'shipmod validate usa a checagem estrutural sem o validador C++', 'shipmod test requer o runner externo', 'Sem pacote nativo de conformidade completo; examples/provider-example é o probe mínimo')
    }
    Write-Text (Join-Path $stage 'linkspan-sdk.json') (($metadata | ConvertTo-Json -Depth 8) + "`n")
    # Módulo existente é útil como guarda ROM-free, não como empacotador de SDK.
    Assert-LinkSpanPackageIsRomFree -Path $stage | Out-Null
    $stageRoot = $stage.TrimEnd('\') + '\'
    $names = [string[]]@(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stageRoot.Length).Replace('\', '/') })
    [Array]::Sort($names, [StringComparer]::Ordinal)
    $hashLines = foreach ($name in $names) { (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $name }
    Write-Text (Join-Path $stage 'checksums.sha256') (($hashLines -join "`n") + "`n")
    . (Join-Path $HostRoot 'tools/linkspan-zip.ps1')
    New-LinkSpanZip -Source $stage -Destination $temporaryZip
    Move-Item -LiteralPath $temporaryZip -Destination $finalZip
    [ordered]@{ outputPath = $finalZip; sha256 = (Get-FileHash -LiteralPath $finalZip -Algorithm SHA256).Hash.ToLowerInvariant(); layoutId = $layoutId; hostSha256 = $hostHash; headers = $seen.Count; entries = $names.Count + 1 } | ConvertTo-Json
}
finally {
    # Nunca remove caminhos calculados sem conferir que são temporários em OutDir.
    foreach ($temporary in @($stage, $temporaryZip)) {
        $checked = [IO.Path]::GetFullPath($temporary)
        if (-not $checked.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Limpeza recusada: $checked" }
        if (Test-Path -LiteralPath $checked) { Remove-Item -LiteralPath $checked -Recurse -Force }
    }
}
