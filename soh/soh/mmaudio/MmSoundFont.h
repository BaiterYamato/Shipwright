#pragma once

namespace ShipLua {

// Diagnóstico da Fase 1 do port de áudio (handoff OOT-AUDIO-001): carrega um
// soundfont do MM com e sem o prefixo de namespace e registra quantas amostras
// cada caminho resolveu. Só escreve no log — não toca em nada do jogo.
//
// O carregador em si (LoadMmSoundFont) é interno por enquanto: expô-lo aqui
// exigiria arrastar z64.h/z64audio.h para dentro deste header, já que SoundFont
// é um typedef de struct anônimo e não pode ser declarado adiante. Quando a
// Fase 2 precisar dele, promova junto com os includes certos.
void ProbeMmSoundFonts();

// Toca a entrada de SFX de índice `sfxIndex` do soundfont `fontIndex` do MM,
// decodificando a amostra e misturando pelo caminho da Fase 1.
//
// Por que existe: o interpretador de sequência (Fase 2) é o caminho "certo",
// mas ainda não produz nota. As amostras de SFX, porém, estão no soundfont e
// são alcançáveis por índice — é o MESMO áudio, resolvido pelo mesmo soundfont
// do MM, só disparado direto em vez de pelo script. Não é substituto: é o som
// real, por um caminho mais curto.
bool MmAudio_PlayFontSfx(int fontIndex, int sfxIndex);

// Toca um sfx do MM por ID, decompondo banco e índice como o próprio MM faz:
// banco nos bits 12-14, índice nos bits 0-9. Cada banco tem seu soundfont.
// Usado pelo redirecionamento de voz — devolve false se a amostra não existir,
// para o chamador cair na voz nativa em vez de ficar mudo.
bool MmAudio_PlayVoiceSfx(int sfxId);

// Lista no log as entradas de SFX de um soundfont com o nome da amostra de cada
// uma. E como se descobre QUAL indice e qual som sem adivinhar: os ids do MM
// (NA_SE_*) sao constantes do decomp, mas o indice dentro do font e outra coisa.
void MmAudio_DumpSfxTable(int fontIndex, int first, int count);

} // namespace ShipLua
