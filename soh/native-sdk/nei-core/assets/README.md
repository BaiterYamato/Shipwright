# Material do Spinner

`spinner-bronze-source.png` foi gerado com a ferramenta de imagem do Codex para substituir a textura de teste
colorida do Spinner do fork NEI. Pedido: material quadrado repetível, bronze/latão envelhecido, sulcos marrons
escuros, estilo de jogo 3D da era N64, legível a 32×32 pixels, sem letras e sem cores neon.

`spinner-bronze-preview.png` é a redução para 32×32. `spinner-bronze.otex` usa o cabeçalho OTEX RGBA16 32×32 do
recurso `objects/object_nei_spinner/sSpinnerTex` e apenas os pixels do novo material (RGBA5551, big endian).
`tools/build-nei-assets.py` verifica o cabeçalho e substitui somente esse recurso ao montar o arquivo local.
Geometria, ícones e código de comportamento do fork não são modificados.
