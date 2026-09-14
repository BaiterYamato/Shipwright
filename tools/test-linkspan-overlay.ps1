[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Overlay,

    # Instalação oficial limpa com oot.o2r legítimo; é copiada, nunca alterada.
    [Parameter(Mandatory = $true)][string]$Base,

    # Pasta nova onde a cópia recebe o overlay.
    [Parameter(Mandatory = $true)][string]$Smoke,

    [Parameter(Mandatory = $true)][string]$Evidence,

    # Pasta scripts da skill soh-runtime-playtest (prepare, finish e capture-runtime-*.ps1).
    [Parameter(Mandatory = $true)][string]$PlaytestScripts,

    # Vazio usa o soh/soh/ShipLuaBootstrap.cpp deste repositório; no PowerShell 5.1, o $PSScriptRoot ainda está vazio dentro do param().
    [string]$BootstrapFile,

    # Mods que o usuário já tinha antes do overlay; precisam sair intactos.
    [string[]]$PreinstalledMods = @(),

    [string]$ExpectedSha256,

    [int]$DurationSeconds = 170,

    # Contagem exigida em "ShipLua carregou N mod(s)". Com o miniz trocado no link, o host abre e passa
    # nos testes ROM-free, mas carrega 0 mods sem nenhuma linha de rejeição.
    [int]$ExpectedMods = -1,

    # Liga gDeveloperTools.ResourceLogging na cópia e exige comandos de cena em XML no log. As factories
    # de cena imprimem com tinyxml2::XMLPrinter; com o header do tinyxml2 diferente da lib ligada, o host
    # caía no primeiro carregamento de cena.
    [switch]$ResourceLogging,

    # Sem capturas: com alguém usando o PC, a janela do jogo fica atrás e a captura pega outro aplicativo.
    [switch]$NoCapture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $BootstrapFile) { $BootstrapFile = Join-Path $PSScriptRoot '..\soh\soh\ShipLuaBootstrap.cpp' }
$failures = New-Object System.Collections.Generic.List[string]

if (Get-Process soh -ErrorAction SilentlyContinue) { throw 'Já existe um soh.exe em execução.' }
if (Test-Path -LiteralPath $Smoke) { throw "A pasta da prova já existe: $Smoke" }
foreach ($script in 'prepare-runtime-session.ps1', 'finish-runtime-session.ps1', 'capture-runtime-window.ps1') {
    if (-not (Test-Path -LiteralPath (Join-Path $PlaytestScripts $script) -PathType Leaf)) {
        throw "Script da skill soh-runtime-playtest não encontrado: $script"
    }
}
New-Item -ItemType Directory -Force $Evidence | Out-Null

robocopy $Base $Smoke /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy falhou: $LASTEXITCODE" }
# Caminhos absolutos: o host resolve ./mods contra o diretório de trabalho do processo.
$smokeRoot = (Resolve-Path -LiteralPath $Smoke).Path.TrimEnd('\') + '\'
$Smoke = $smokeRoot.TrimEnd('\')

foreach ($mod in $PreinstalledMods) { Copy-Item -LiteralPath $mod -Destination (Join-Path $Smoke 'mods') }
New-Item -ItemType Directory -Force (Join-Path $Smoke 'Save') | Out-Null
Set-Content -LiteralPath (Join-Path $Smoke 'Save\linkspan-preserve-marker.txt') -Value 'save do usuario' -Encoding ASCII

function Get-Snapshot {
    param([string]$Root)
    $map = @{}
    Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
        $map[$_.FullName.Substring($Root.Length).Replace('\', '/')] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    return $map
}
$before = Get-Snapshot -Root $smokeRoot
"antes do overlay: $($before.Count) arquivos"

Add-Type -AssemblyName System.IO.Compression.FileSystem
$overlayEntries = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Overlay).Path)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName.Contains('\')) { throw "Entrada com barra invertida: $($entry.FullName)" }
        $target = [System.IO.Path]::GetFullPath((Join-Path $smokeRoot $entry.FullName))
        if (-not $target.StartsWith($smokeRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "Entrada fora da pasta: $($entry.FullName)" }
        if ($entry.FullName.EndsWith('/')) { New-Item -ItemType Directory -Force $target | Out-Null; continue }
        [void]$overlayEntries.Add($entry.FullName)
        New-Item -ItemType Directory -Force ([System.IO.Path]::GetDirectoryName($target)) | Out-Null
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
    }
}
finally { $zip.Dispose() }
"overlay extraído: $($overlayEntries.Count) arquivos"

$checksumCount = 0
foreach ($line in [System.IO.File]::ReadAllLines((Join-Path $Smoke 'checksums.sha256'))) {
    if (-not $line) { continue }
    $parts = $line -split '  ', 2
    $checksumCount++
    $actual = (Get-FileHash -LiteralPath (Join-Path $Smoke $parts[1]) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $parts[0]) { $failures.Add("checksum divergente: $($parts[1])") }
}
"checksums.sha256: $checksumCount entradas conferidas"

$replaced = @($before.Keys | Where-Object { $overlayEntries.Contains($_) } | Sort-Object)
$preserved = 0
foreach ($key in $before.Keys) {
    if ($overlayEntries.Contains($key)) { continue }
    $path = Join-Path $Smoke $key
    if (-not (Test-Path -LiteralPath $path)) { $failures.Add("sumiu: $key"); continue }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $before[$key]) { $failures.Add("alterado: $key"); continue }
    $preserved++
}
"preservados byte a byte: $preserved | substituídos pelo overlay: $($replaced -join ', ')"

if ($ResourceLogging) {
    # Edição textual: o ConvertTo-Json do PowerShell 5.1 regravaria 1.0 como 1 e mudaria o tipo das CVars float.
    $configPath = Join-Path $Smoke 'shipofharkinian.json'
    $json = '{}'
    if (Test-Path -LiteralPath $configPath) { $json = [System.IO.File]::ReadAllText($configPath) }
    $insertMember = {
        param([string]$Text, [string]$Opening, [string]$Member)
        $re = New-Object System.Text.RegularExpressions.Regex ('(' + $Opening + '\s*\{)(\s*)(\}?)')
        if (-not $re.IsMatch($Text)) { return $null }
        $re.Replace($Text, [System.Text.RegularExpressions.MatchEvaluator] {
                param($m)
                if ($m.Groups[3].Value) { $m.Groups[1].Value + $Member + '}' }
                else { $m.Groups[1].Value + $Member + ',' + $m.Groups[2].Value }
            }, 1)
    }
    if ($json -match '"ResourceLogging"\s*:\s*\d+') { $json = $json -replace '("ResourceLogging"\s*:\s*)\d+', '${1}1' }
    else {
        $updated = & $insertMember $json '"gDeveloperTools"\s*:' '"ResourceLogging": 1'
        if ($null -eq $updated) { $updated = & $insertMember $json '"CVars"\s*:' '"gDeveloperTools": {"ResourceLogging": 1}' }
        if ($null -eq $updated) { $updated = & $insertMember $json '^\s*' '"CVars": {"gDeveloperTools": {"ResourceLogging": 1}}' }
        $json = $updated
    }
    if (($json | ConvertFrom-Json).CVars.gDeveloperTools.ResourceLogging -ne 1) { throw 'Não consegui ligar ResourceLogging na cópia.' }
    [System.IO.File]::WriteAllText($configPath, $json, (New-Object System.Text.UTF8Encoding $false))
    'gDeveloperTools.ResourceLogging ligado na cópia'
}

$exe = Join-Path $Smoke 'soh.exe'
$activeLog = Join-Path $Smoke 'logs\Ship of Harkinian.log'
$sessionId = 'linkspan-overlay-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$trackPaths = @((Join-Path $Smoke 'oot.o2r')) + @(Get-ChildItem -LiteralPath (Join-Path $Smoke 'mods') -File | ForEach-Object { $_.FullName })
$prepare = @{
    ExePath = $exe
    LogPath = $activeLog
    ArchiveDirectory = $Evidence
    BootstrapFile = (Resolve-Path -LiteralPath $BootstrapFile).Path
    ExpectedBootstrapMarkers = @('ShipLua inicializando', 'ShipLua inicializado')
    SessionId = $sessionId
    TrackPaths = $trackPaths
}
if ($ExpectedSha256) { $prepare.ExpectedSha256 = $ExpectedSha256 }
& (Join-Path $PlaytestScripts 'prepare-runtime-session.ps1') @prepare
$manifest = Join-Path $Evidence "runtime-session-$sessionId.json"
if (-not (Test-Path -LiteralPath $manifest)) { throw "Manifesto não gerado: $manifest" }

$process = Start-Process -FilePath $exe -WorkingDirectory $Smoke -PassThru
$start = Get-Date
$exitedEarly = $false
$checkpoints = @(50, 100, 150)
if ($NoCapture) { $checkpoints = @(30, 60, 90, 120, 150) }
foreach ($t in $checkpoints) {
    $wait = $t - [int]((Get-Date) - $start).TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Seconds $wait }
    if ($process.HasExited) { $exitedEarly = $true; $failures.Add("soh.exe saiu antes de ${t}s (exit=$($process.ExitCode))"); break }
    if (-not $NoCapture) {
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PlaytestScripts 'capture-runtime-window.ps1') -ProcessId $process.Id -ExpectedExePath $exe -OutputPath (Join-Path $Evidence "captura-${t}s.png")
    }
}
if (-not $exitedEarly) {
    $wait = $DurationSeconds - [int]((Get-Date) - $start).TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Seconds $wait }
    if ($process.HasExited) {
        $failures.Add("soh.exe saiu antes de ${DurationSeconds}s (exit=$($process.ExitCode))")
    }
    else {
        "soh.exe vivo aos ${DurationSeconds}s"
        [void]$process.CloseMainWindow()
        if ($process.WaitForExit(15000)) { "fechado pela janela (exit=$($process.ExitCode))" }
        else { Stop-Process -Id $process.Id -Force; 'não fechou em 15 s; encerrado à força' }
    }
}
Start-Sleep -Seconds 2

# O finish só chama exit quando falha; zera o código para o 1 de sucesso do robocopy não virar falha.
$global:LASTEXITCODE = 0
& (Join-Path $PlaytestScripts 'finish-runtime-session.ps1') -ManifestPath $manifest -RequireTrackedUnchanged -Strict
if ($LASTEXITCODE -ne 0) { $failures.Add("finish-runtime-session exit=$LASTEXITCODE") }

"--- log ($activeLog)"
if (Test-Path -LiteralPath $activeLog) {
    Select-String -LiteralPath $activeLog -Encoding UTF8 -Pattern 'Starting Ship|ShipLua inicializ|carregou|rejeit|recus|loaded mod|\[error\]|\[critical\]|exception' |
        Select-Object -First 60 | ForEach-Object { $_.Line }
    $loadedLine = Select-String -LiteralPath $activeLog -Encoding UTF8 -Pattern 'carregou (\d+) mod' | Select-Object -First 1
    $loadedCount = -1
    if ($loadedLine) { $loadedCount = [int]$loadedLine.Matches[0].Groups[1].Value }
    "mods carregados: $loadedCount"
    if ($ExpectedMods -ge 0 -and $loadedCount -ne $ExpectedMods) { $failures.Add("carregou $loadedCount mod(s); esperado $ExpectedMods") }
    if (Select-String -LiteralPath $activeLog -Encoding UTF8 -Pattern 'rejeitou o mod' -Quiet) { $failures.Add('o log registra mod rejeitado') }
    if (Select-String -LiteralPath $activeLog -Encoding UTF8 -Pattern '\[critical\] Exception:' -Quiet) { $failures.Add('o log registra exceção do CrashHandler') }
    if ($ResourceLogging) {
        $xmlLines = @(Select-String -LiteralPath $activeLog -Encoding UTF8 -Pattern '\[info\] \S+: <Set[A-Za-z]+')
        "comandos de cena em XML no log: $($xmlLines.Count)"
        if (-not $xmlLines.Count) { $failures.Add('ResourceLogging ligado, mas o log não tem comandos de cena em XML') }
    }
}
else { $failures.Add('log ativo ausente') }

'--- resultado'
if ($failures.Count) { $failures | ForEach-Object { "FALHA: $_" }; exit 1 }
'PROVA OK'
