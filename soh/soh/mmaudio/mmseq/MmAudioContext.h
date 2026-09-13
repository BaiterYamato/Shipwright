#pragma once

// Contexto e cola do interpretador de áudio do MM rodando dentro do OoT.
//
// REGRA (ver MmAudioTypes.h): nada aqui pode incluir header do OoT. Macro não
// respeita namespace, e os dois decomps definem as mesmas macros de áudio.

#include "soh/mmaudio/mmseq/MmAudioTypes.h"

namespace mmsfx {

// A instância que o código portado referencia como `gAudioCtx`. No MM é uma
// global do jogo; aqui é nossa, viva só enquanto o motor de SFX estiver ligado.
extern AudioContext gAudioCtx;

// Troca de bytes para dados big-endian vindos do o2r. No MM vem do
// <ship/utils/binarytools/endianness.h> do LUS; redefinido aqui para não
// arrastar header externo para dentro do namespace.
#define BE16SWAP(x) ((s16)(((((u16)(x)) >> 8) & 0xFF) | ((((u16)(x)) & 0xFF) << 8)))

#define BE16SWAP_CONST(x) BE16SWAP(x)

// Utilitárias do macros.h do MM. Copiadas em vez de incluídas pelo mesmo motivo
// de sempre: o header do MM arrasta o jogo inteiro junto.
#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))
#define SQ(x) ((x) * (x))
#define CLAMP_MAX(x, max) ((x) > (max) ? (max) : (x))

// No MM isto vira __attribute__((aligned(x))), que o MSVC não aceita nessa
// posição. Fica vazio: o alinhamento existia para o DMA do N64, e aqui nada
// faz DMA — quem carrega recurso é o ResourceMgr e quem sintetiza é o
// decodificador da Fase 1.
#define ALIGNED(x)

// ---------------------------------------------------------------------------
// Símbolos que vivem nos outros arquivos portados. Declarados aqui para que
// cada .cpp compile sozinho; a definição chega quando o arquivo dono for
// portado (ordem no handoff OOT-AUDIO-001, Fase 2).
// ---------------------------------------------------------------------------

// de seqplayer.cpp
void AudioScript_SequencePlayerDisable(SequencePlayer* seqPlayer);

// de data.cpp
extern f32 gBendPitchOneOctaveFrequencies[];
extern f32 gBendPitchTwoSemitonesFrequencies[];
extern s16* gWaveSamples[];

// ---------------------------------------------------------------------------
// Shims de heap.c e load.c.
//
// Os dois arquivos NÃO são portados: seqplayer.c e playback.c usam apenas 12
// símbolos deles, todos rasos (medição no handoff OOT-AUDIO-001). Quem carrega
// recurso aqui é o ResourceMgr do SoH, então as checagens de carregamento são
// sempre "pronto" e os marcadores de status são no-op.
// ---------------------------------------------------------------------------

// C permite atribuir void* a qualquer ponteiro; C++ não. O decomp faz isso nas
// alocações, e são código gerado que não queremos editar à mão. Este proxy
// converte sozinho no destino, mantendo os arquivos portados idênticos ao
// upstream.
struct AutoPtr {
    void* raw;
    template <typename T> operator T*() const {
        return static_cast<T*>(raw);
    }
    // O decomp testa o resultado da alocação direto em condicional.
    explicit operator bool() const {
        return raw != nullptr;
    }
};

s32 AudioLoad_IsFontLoadComplete(s32 fontId);
s32 AudioLoad_IsSeqLoadComplete(s32 seqId);
void AudioLoad_SetFontLoadStatus(s32 fontId, s32 status);
void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 status);
AutoPtr AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id);
AutoPtr AudioHeap_AllocZeroed(AudioAllocPool* pool, u32 size);
AutoPtr AudioHeap_AllocDmaMemory(AudioAllocPool* pool, u32 size);

// Os cinco restantes dos 12. Todos do caminho de carregamento assíncrono do
// N64, que aqui não existe: o recurso chega pronto pelo ResourceMgr antes de
// qualquer nota tocar.
s32 AudioLoad_SyncInitSeqPlayer(s32 playerIndex, s32 seqId, s32 arg2);
s32 AudioLoad_SlowLoadSample(s32 fontId, s32 instId, s8* isDone);
s32 AudioLoad_SlowLoadSeq(s32 seqId, u8* ramAddr, s8* isDone);
void AudioLoad_ScriptLoad(s32 tableType, s32 id, s8* isDone);
void AudioHeap_LoadFilter(s16* filter, s32 lowPassCutoff, s32 highPassCutoff);

// Ponteiro de função customizada da sequência (opcode 0xBE). No MM é global
// do jogo; aqui é nossa. O seqplayer só a usa como variável temporária entre
// buscar a função na tabela do contexto e chamá-la.
extern AudioCustomSeqFunction gAudioCustomSeqFunction;

// De thread.c, que não é portado: gerador pseudoaleatório usado para variação
// de nota. Implementado localmente.
u32 AudioThread_NextRandom(void);

// Do 2S2H: permite ao usuário trocar músicas do jogo. Não temos o editor de
// áudio dele nem queremos — o shim responde "sem substituição".
s32 AudioEditor_GetReplacementSeq(s32 seqId);

// Do LUS. O seqplayer do 2S2H lê uma CVar de volume; devolvemos o padrão.
f32 CVarGetFloat(const char* name, f32 defaultValue);

// Ponte com o ResourceMgr do SoH, implementada em MmAudioContext.cpp sobre a
// API C de MmAudioBridge.h.
struct OggOpusFile;
SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path);
SequenceData ResourceMgr_LoadSeqByName(const char* path);
void aOPUSFree(struct OggOpusFile* opusFile);

// Tabelas de caminho por id. Preenchidas por MmAudio_InitPathTables antes de
// qualquer coisa tocar.
extern char** gFontMap;
extern char** gSequenceMap;
void MmAudio_InitPathTables(void);

// Relata um waveId fora da faixa de gWaveSamples. Implementada no motor: o
// decomp nunca precisou disto porque o valor vinha da ROM; aqui pode vir de um
// lookup de instrumento que falhou, e sem o aviso o sintoma seria so um crash.
void MmSeq_ReportBadWaveId(s32 waveId);

} // namespace mmsfx
