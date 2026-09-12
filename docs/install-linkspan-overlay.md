# Instalar o Link-Span sobre o Shipwright 9.2.3

O pacote Windows é um overlay ZIP. Ele foi desenhado para ser extraído na raiz
de uma instalação nova da [release Shipwright 9.2.3](https://github.com/HarbourMasters/Shipwright/releases/tag/9.2.3),
substituindo os arquivos do host que precisam conhecer o Link-Span.

## O que o overlay altera

- substitui `soh.exe` pelo host compilado com o Link-Span;
- substitui `soh.o2r` pela versão gerada junto com esse executável;
- adiciona a pasta `mods` e, quando solicitado no empacotamento, um mod de teste;
- adiciona metadados, instruções e checksums do pacote.

O overlay não inclui nem substitui `oot.o2r`, ROMs, saves ou arquivos pessoais.
`soh.exe` e `soh.o2r` devem sempre ser distribuídos e instalados como o mesmo
par. Misturar o executável Link-Span com o `soh.o2r` oficial causa falha durante
o carregamento da interface.

## Instalação para o jogador

1. Extraia `SoH-Ackbar-Delta-Win64.zip` da release 9.2.3 em uma pasta nova.
2. Execute o Shipwright uma vez e conclua a geração normal do seu `oot.o2r`.
3. Feche o jogo.
4. Extraia `LinkSpan-Shipwright-9.2.3-OOT-CORE-002-RegistryV1-Win64-overlay.zip`
   nessa mesma pasta.
5. Confirme a substituição de `soh.exe` e `soh.o2r`.
6. Coloque mods `.zip` ou `.shipmod` diretamente em `mods` e execute `soh.exe`.

O pacote do mod precisa ter `manifest.toml` na raiz. Um mod nativo pode carregar
uma DLL própria declarada no manifesto; por isso ele tem a mesma capacidade de
acesso ao processo e o mesmo risco de crash que um mod nativo tradicional.
Core extensions usam `kind = "core_extension"`, `load_phase = "pre_game"` e
provider ABI 1.1. Elas carregam antes dos mods comuns e podem publicar serviços
versionados para outros ZIPs sem recompilar novamente o Shipwright.

## Gerar o overlay

```powershell
pwsh -File tools/package-linkspan-overlay.ps1 `
  -HostExecutable x64/Release/soh.exe `
  -HostResources build/native-runtime/soh.o2r `
  -ExampleMod build/distribution/Dynamic-Movement-Remake-0.2.4.zip `
  -OutputPath build/distribution/LinkSpan-Shipwright-9.2.3-OOT-CORE-002-RegistryV1-Win64-overlay.zip
```

O script cria o ZIP por um diretório temporário único, gera
`linkspan-overlay.json` e `checksums.sha256`, e não lê nem empacota `oot.o2r`.
