// Prova da Fase 2, passo 1 (handoff OOT-AUDIO-001).
//
// Esta unidade de tradução inclui SÓ os tipos de áudio do MM — nenhum header do
// OoT. É o contrato que todos os arquivos portados vão seguir, e existe para que
// uma quebra apareça no build, não na Fase 2 no meio de milhares de linhas.
//
// A tentativa anterior incluía este header junto com o z64audio.h do OoT, para
// "testar o pior caso". O compilador mostrou que o pior caso é simplesmente
// proibido: namespace isola tipo, mas macro é preprocessador e não respeita
// namespace. IS_SEQUENCE_CHANNEL_VALID, NO_LAYER, SEQ_NUM_CHANNELS e
// TATUMS_PER_BEAT existem nos dois decomps, e a redefinição vira erro.
//
// Daí o desenho: os dois mundos ficam em TUs separadas e conversam por uma API
// C de tipos simples. Mesma solução do mm_sfx_synth.h do fork skijer.

#include "soh/mmaudio/mmseq/MmAudioTypes.h"

namespace {

// Os tipos que o interpretador de sequência precisa. Se algum sumir ou parar de
// resolver ao regenerar o header, o build quebra aqui — barato — em vez de
// quebrar espalhado pelos arquivos portados.
static_assert(sizeof(mmsfx::AudioContext) > 0, "AudioContext");
static_assert(sizeof(mmsfx::SequencePlayer) > 0, "SequencePlayer");
static_assert(sizeof(mmsfx::SequenceChannel) > 0, "SequenceChannel");
static_assert(sizeof(mmsfx::SequenceLayer) > 0, "SequenceLayer");
static_assert(sizeof(mmsfx::Note) > 0, "Note");
static_assert(sizeof(mmsfx::NotePlaybackState) > 0, "NotePlaybackState");
static_assert(sizeof(mmsfx::NoteSampleState) > 0, "NoteSampleState");
static_assert(sizeof(mmsfx::AdsrState) > 0, "AdsrState");
static_assert(sizeof(mmsfx::Portamento) > 0, "Portamento");
static_assert(sizeof(mmsfx::VibratoState) > 0, "VibratoState");
static_assert(sizeof(mmsfx::EnvelopePoint) > 0, "EnvelopePoint");
static_assert(sizeof(mmsfx::SoundFont) > 0, "SoundFont");
static_assert(sizeof(mmsfx::Instrument) > 0, "Instrument");
static_assert(sizeof(mmsfx::Drum) > 0, "Drum");
static_assert(sizeof(mmsfx::TunedSample) > 0, "TunedSample");
static_assert(sizeof(mmsfx::Sample) > 0, "Sample");
static_assert(sizeof(mmsfx::AdpcmBook) > 0, "AdpcmBook");
static_assert(sizeof(mmsfx::AdpcmLoop) > 0, "AdpcmLoop");
static_assert(sizeof(mmsfx::SequenceData) > 0, "SequenceData");

// As structs de soundfont têm que casar byte a byte com as do OoT: é o que
// permite passar um SoundFont* carregado pelo ResourceMgr do SoH direto para o
// interpretador do MM, sem tradução. Verificado campo a campo no handoff; aqui
// fica a checagem que o compilador refaz a cada build.
//
// Os números vêm do layout de 64 bits (ponteiro de 8 bytes), não do N64.
static_assert(sizeof(mmsfx::TunedSample) == 16, "TunedSample: ponteiro + f32 + padding");
static_assert(sizeof(mmsfx::AdpcmBook) == 16, "AdpcmBook: 2 s32 + ponteiro");

} // namespace
