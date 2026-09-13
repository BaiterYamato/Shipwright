#pragma once

// Ponte entre o interpretador de áudio do MM e o resto do host.
//
// Esta é a fronteira que a Fase 2 provou ser obrigatória: namespace isola
// tipos, mas macro é preprocessador e os dois decomps definem as mesmas macros
// de áudio. Logo os dois mundos não podem se encontrar numa unidade de
// tradução — só através de uma API de tipos simples, como esta.
//
//   MmAudioBridge.cpp   inclui os headers do OoT, NUNCA os do MM
//   mmseq/*.cpp         incluem os headers do MM, NUNCA os do OoT
//
// É o mesmo desenho do mm_sfx_synth.h do fork skijer, e agora sei por quê.

#include <cstdint>

extern "C" {

// Carrega um soundfont do mm.o2r. Devolve um SoundFont* opaco — o chamador do
// lado do MM faz o cast, que é seguro porque as structs de soundfont são
// binário-compatíveis entre os dois decomps (verificado no handoff
// OOT-AUDIO-001 e travado por static_assert em MmAudioTypesCheck.cpp).
void* MmBridge_LoadSoundFont(const char* pathWithinMm);

// Carrega uma sequência do mm.o2r. Devolve SequenceData* opaco.
void* MmBridge_LoadSequence(const char* pathWithinMm);

} // extern "C"
