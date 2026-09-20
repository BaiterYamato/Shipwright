[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build\nei-core-native',
    [string]$Contract = 'soh\native-sdk\nei-core\fork\helpers.txt',
    [string]$Dumpbin
)

# Confere o aceite do NEI-004: "helpers compilam dentro da DLL e acessam o host somente pela ABI ou
# pelo escape hatch declarado". São três perguntas, e cada uma tem uma resposta verificável:
#
#   1. cada categoria do plano tem arquivo do fork na árvore sincronizada e símbolo definido no
#      objeto compilado (fork/helpers.txt é o contrato);
#   2. o relatório do gen_imports.py fechou sem erro, isto é, todo símbolo do host que os objetos do
#      fork pedem tem endereço resolvido pelo escape hatch;
#   3. a DLL importa só a CRT e o KERNEL32 — nada do soh.exe entra por tabela de importação do PE,
#      que é o que "somente pela ABI ou pelo escape hatch" quer dizer na prática.
#
# Roda depois de `cmake --build <BuildDirectory> --config Release`. Sai diferente de zero na primeira
# resposta que não fecha.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-Existing([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Label não encontrado: $Path" }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Find-Dumpbin {
    if ($Dumpbin) { return Resolve-Existing $Dumpbin 'dumpbin' }
    $roots = @("${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $found = Get-ChildItem -LiteralPath $root -Recurse -Filter 'dumpbin.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*Hostx64\x64*' } | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw 'dumpbin.exe não encontrado; informe -Dumpbin'
}

$build = Resolve-Existing $BuildDirectory 'diretório de build'
$contractPath = Resolve-Existing $Contract 'contrato dos helpers'
$object = Resolve-Existing (Join-Path $build 'nei_fork.dir\Release\player_unit.obj') 'objeto da unidade do fork'
$dll = Resolve-Existing (Join-Path $build 'mod\provider\linkspan_nei_core.dll') 'DLL do coremod'
$importReport = Resolve-Existing (Join-Path $build 'nei-fork\nei_host_imports.txt') 'relatório do escape hatch'
$forkHelpers = Join-Path $build 'nei-fork\fork\soh\mods\items\helpers'
$dumpbinPath = Find-Dumpbin

$linhas = Get-Content -LiteralPath $contractPath | ForEach-Object { ($_ -split '#')[0].Trim() } | Where-Object { $_ }
$categorias = foreach ($linha in $linhas) {
    $campos = $linha -split '\s*\|\s*'
    if ($campos.Count -ne 3) { throw "linha malformada no contrato: $linha" }
    [pscustomobject]@{ Categoria = $campos[0]; Arquivo = $campos[1]; Simbolo = $campos[2] }
}
if (-not $categorias) { throw 'contrato vazio' }

# 1. Cada categoria: arquivo presente na árvore do fork e símbolo definido no objeto.
# O dumpbin marca a seção como SECT<hex> quando o símbolo é definido ali; UNDEF é só referência.
# O número da seção é hexadecimal (SECTAFC), não decimal — por isso [0-9A-F], não \d.
$simbolos = & $dumpbinPath /symbols $object 2>$null
$definidos = [System.Collections.Generic.HashSet[string]]::new()
foreach ($linha in $simbolos) {
    if ($linha -match 'SECT[0-9A-F]+.*(External|Static)\s+\|\s+(\S+)') { [void]$definidos.Add($Matches[2]) }
}

$falhas = New-Object System.Collections.Generic.List[string]
foreach ($categoria in $categorias) {
    $arquivo = Join-Path $forkHelpers $categoria.Arquivo
    if (-not (Test-Path -LiteralPath $arquivo)) {
        $falhas.Add("$($categoria.Categoria): arquivo ausente na árvore do fork ($($categoria.Arquivo))")
        continue
    }
    if (-not $definidos.Contains($categoria.Simbolo)) {
        $falhas.Add("$($categoria.Categoria): $($categoria.Simbolo) não está definido em player_unit.obj")
        continue
    }
    "ok  {0,-28} {1,-24} {2}" -f $categoria.Categoria, $categoria.Arquivo, $categoria.Simbolo
}

# 2. Escape hatch sem erro.
$relatorio = Get-Content -LiteralPath $importReport
$erros = ($relatorio | Select-String -Pattern '^erros:\s*(\d+)' | Select-Object -First 1)
if (-not $erros) { $falhas.Add('relatório do escape hatch sem a linha de erros') }
elseif ([int]$erros.Matches[0].Groups[1].Value -ne 0) {
    $falhas.Add("escape hatch com $($erros.Matches[0].Groups[1].Value) símbolo(s) não resolvido(s)")
} else {
    $funcoes = ($relatorio | Select-String -Pattern '^fun\S+:\s*(\d+)' | Select-Object -First 1)
    $dados = ($relatorio | Select-String -Pattern '^dados:\s*(\d+)' | Select-Object -First 1)
    "ok  escape hatch: {0} funções e {1} dados resolvidos, 0 erros" -f `
        $funcoes.Matches[0].Groups[1].Value, $dados.Matches[0].Groups[1].Value
}

# 3. A DLL não importa nada além da CRT e do KERNEL32.
$permitidas = '^(MSVCP140|VCRUNTIME140(_1)?|api-ms-win-crt-[a-z0-9-]+|KERNEL32)\.dll$'
$dependentes = & $dumpbinPath /dependents $dll 2>$null |
    Select-String -Pattern '^\s{4}(\S+\.dll)\s*$' | ForEach-Object { $_.Matches[0].Groups[1].Value }
if (-not $dependentes) { $falhas.Add('dumpbin não listou dependências da DLL') }
$intrusas = @($dependentes | Where-Object { $_ -notmatch $permitidas })
if ($intrusas.Count) { $falhas.Add("a DLL importa além da CRT: $($intrusas -join ', ')") }
elseif ($dependentes) { "ok  DLL depende só de {0}" -f ($dependentes -join ', ') }

if ($falhas.Count) {
    ''
    'FALHOU:'
    $falhas | ForEach-Object { "  - $_" }
    exit 1
}
''
"{0} categorias conferidas; helpers compilados na DLL e acesso ao host só pelo escape hatch." -f $categorias.Count
