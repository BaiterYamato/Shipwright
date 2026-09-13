# Mic Ocarina 0.1.1

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
4. O aviso **MICROFONE ATIVO — B: SAIR** aparece no alto da tela enquanto o dispositivo estiver capturando.
5. Cantarole a sequência relativa de uma música aceita naquele momento.
6. Aperte **B** para encerrar o modo de áudio. Guardar a ocarina também encerra e libera o dispositivo.

Em controles Nintendo, o mod lê os botões físicos **+** e **B**. Sem gamepad, usa os botões virtuais Start e B.
A música reconhecida chega ao jogo como se tivesse sido tocada, com o efeito normal dela; músicas que o jogo não
aceita naquele momento são ignoradas.

## Com o Dynamic Movement Remake

Os dois mods rodam juntos. No perfil do Dynamic Movement, **B** também é o B do N64 e guarda a ocarina. Como ele
tira os botões C do analógico direito, tocar notas pelos botões fica limitado a A, D-pad direita (C-Up) e ZR (o C
selecionado com R); o microfone não depende disso.

## Origem

O código deriva do protótipo `MicOcarina.cpp` fornecido em `MicOcarina.rar`: detector YIN, filtro de ruído e comparação
por contorno relativo foram portados. Dependências diretas de 2Ship/MM, músicas exclusivas do MM e a janela ImGui foram
removidas. O reconhecimento consulta as músicas e flags do OoT pelo serviço `linkspan.oot.ocarina` v1.
