# Mic Ocarina 0.2.0

Mod independente para Link-Span / Shipwright OoT. Inclui a interface da ocarina inspirada no remake e a entrada por microfone.

## Interface

- Partitura em pergaminho, notas com os botões do controle e confirmação da música reconhecida.
- Songbook com as músicas aprendidas no save e suas sequências.
- Microfone: indicador de captura, nível de volume real, nota/frequência e histórico de afinação.
- A abertura do dispositivo ocorre em segundo plano, mantendo o jogo responsivo. O áudio é convertido do formato do microfone para o formato original do detector.
- Os textos exibidos no jogo estão em inglês.
- As opções ficam em Mods > Mic Ocarina > Ocarina & Audio.
- Open ocarina abre o instrumento que Link já possui; o painel informa o estado da captura e eventuais recusas.

## Controles Nintendo

Abra a ocarina normalmente, inclusive pelo D-pad esquerdo do Dynamic Movement Remake.

| Botão | Ação |
| --- | --- |
| L / R / A / Y / X | Notas D4 / F4 / A4 / B4 / D5 |
| ZL | Abrir/fechar songbook durante execução livre |
| + | Iniciar microfone; com captura ativa, reiniciar a frase |
| - | Alternar prática de áudio, sem ativar efeitos de músicas |
| B | Cancelar e guardar a ocarina |

A sequência de Zelda's Lullaby passa a ser **Y X A Y X A**. Os botões são lidos por posição física SDL; os glifos usam os nomes Nintendo das referências.

Na versão 0.2.1, o Y também é filtrado na lógica de cancelamento da tela, além da leitura de notas do áudio. Assim, o binding de ataque B usado pelo movimento dinâmico não fecha o instrumento ao tocar Y. B físico continua cancelando, e os bindings normais são restaurados ao sair dessa lógica.

Sem gamepad, permanecem os controles virtuais do jogo: A/C para notas, Z para songbook, Start para microfone/reset e L para prática. O HUD apresenta esses comandos. O menu também oferece ações para iniciar captura, praticar e reiniciar.

## Compatibilidade

- Pacote de extensão nativa ABI 1.3, limitado ao executável cujo SHA-256 está no manifest gerado pelo CMake.
- Compilar com OotNativeSdk.cmake e HOST_EXECUTABLE do mesmo host.
- Recursos próprios são montados pelo provider, sem substituir texturas globais de outros mods.
- Dynamic Movement Remake 0.2.13 cede R/X e seus atalhos à ocarina; mantém a correção da continuidade da câmera.
- O renderizador nativo continua executando reconhecimento, timers, diálogos e efeitos de músicas. Somente os comandos de desenho da tela musical são substituídos.
- Mods de mapeamento da ocarina de terceiros podem conflitar com a leitura dos mesmos botões. A opção de controles do remake pode ser desativada.
- Microfone padrão do Windows habilitado para aplicativos de desktop. A captura só começa após ação explícita; B ou guardar a ocarina libera o dispositivo.

## Reconhecimento por áudio

O detector existente continua usando YIN, filtro de ruído, contorno relativo e união de notas partidas por vibrato/respiração. Alcance de 80 a 2400 Hz. Cantarole ou assobie a sequência; aproximadamente um segundo de silêncio encerra a frase. Só músicas aceitas pelo jogo naquele momento são submetidas.

Reset limpa a frase e o histórico na thread de captura. Practice mantém o feedback, consumindo resultados sem enviar músicas ao jogo.

## Validação

CTest inclui o detector original, conversão real de áudio estéreo de 48 kHz, abertura lenta/cancelamento e regressões para mapeamento, preservação de estado do renderizador, diálogos, teclado, songbook, prática, reset e limite de desenho do HUD. A disponibilidade do dispositivo e a resposta de um controle físico exigem playtest com o hardware.

## Origem

O detector foi portado do protótipo MicOcarina.cpp fornecido anteriormente. A interface utiliza arte própria gerada por tools/generate-ocarina-hud-assets.ps1; as imagens de referência não são distribuídas no pacote.
