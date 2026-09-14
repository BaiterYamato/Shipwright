# Mic Ocarina 0.1.4

Mod nativo para o Link-Span/Shipwright OoT.

## Requisitos

- Host com o serviço `linkspan.oot.ocarina` v1 (overlay Link-Span MovementV2-OcarinaV1 ou posterior). Em hosts
  anteriores o mod não carrega.
- Microfone padrão do Windows liberado para aplicativos de área de trabalho
  (Configurações > Privacidade > Microfone).

## Uso

1. Coloque o ZIP inteiro na pasta `mods`.
2. Entre normalmente no modo de tocar ocarina.
3. Aperte **Start/+** para iniciar a captura do microfone padrão.
4. O aviso aparece no alto da tela e diferencia **SEM SINAL**, **ENTRADA OK**, **SOM RECEBIDO** e a nota detectada,
   por exemplo **NOTA A4 440 Hz**. A barra responde ao volume real capturado.
5. Cantarole a sequência relativa de uma música aceita naquele momento. Abaixo do aviso, o rolo de notas mostra cada
   nota cantarolada como uma barra, na altura relativa à primeira nota que marcou o tom. A barra fica branca enquanto
   a nota ainda não conta e verde quando entra na frase comparada com as músicas.
6. Aperte **B** para encerrar o modo de áudio. Guardar a ocarina também encerra e libera o dispositivo.

Cantarole a música inteira sem parar: cerca de um segundo de silêncio encerra a frase, e o rolo recomeça vazio. Uma
nota longa partida por vibrato ou respiração continua contando como uma nota só.

A detecção vai de 80 Hz, abaixo da voz mais grave, a 2400 Hz, acima do C7: dá para cantarolar ou assobiar em qualquer
tom, e a nota aparece na oitava certa.

Em controles Nintendo, o mod lê os botões físicos **+** e **B**. Sem gamepad, usa os botões virtuais Start e B.
A música reconhecida chega ao jogo como se tivesse sido tocada, com o efeito normal dela; músicas que o jogo não
aceita naquele momento são ignoradas.

## Com o Dynamic Movement Remake

Os dois mods rodam juntos. No perfil do Dynamic Movement, **B** também é o B do N64 e guarda a ocarina. Como ele
tira os botões C do analógico direito, tocar notas pelos botões fica limitado a A, D-pad direita (C-Up) e ZR (o C
selecionado com R); o microfone não depende disso.

## Origem

O código deriva do protótipo `MicOcarina.cpp` fornecido em `MicOcarina.rar`: detector YIN, filtro de ruído, comparação
por contorno relativo, junção de notas partidas e rolo de notas foram portados. O rolo, que no protótipo era uma
janela ImGui, é desenhado pelo HUD do Link-Span (`hud.draw`). Dependências diretas de 2Ship/MM e as músicas exclusivas
do MM foram removidas. O reconhecimento consulta as músicas e flags do OoT pelo serviço `linkspan.oot.ocarina` v1.
