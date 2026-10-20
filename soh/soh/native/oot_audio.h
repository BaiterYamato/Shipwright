#ifndef LINKSPAN_OOT_AUDIO_H
#define LINKSPAN_OOT_AUDIO_H

#include <shiplua/native/ship_native_abi.h>

/* oot.audio.mix v1 (HOST-SLIM-001, RFC 0029): TRANSFORM ou OBSERVE, payload ShipOotAudioMixHookV1. Exceção à regra
 * dos hooks: roda na THREAD DE ÁUDIO, em AudioPlayer_Play, depois da mixagem do jogo e antes do dispositivo.
 * `samples` tem `frames` quadros estéreo s16 intercalados a 32 kHz. O TRANSFORM soma ou troca amostras no lugar,
 * sem alocar, sem bloquear e sem chamar serviços do host (todos são da thread do jogo). Ordem: OBSERVE BEFORE,
 * TRANSFORM por prioridade, OBSERVE AFTER. TRANSFORM que não devolve OK tem o buffer restaurado; três falhas
 * seguidas desativam o hook no frame seguinte do jogo. REPLACE não existe neste ponto.
 * Unregister e unload do mod só retornam depois que a thread de áudio saiu do callback: não espere a thread do
 * jogo dentro dele. */
#define LINKSPAN_OOT_HOOK_AUDIO_MIX "oot.audio.mix"
#define LINKSPAN_OOT_HOOK_AUDIO_MIX_VERSION 1u
#define LINKSPAN_OOT_AUDIO_SAMPLE_RATE 32000u
#define LINKSPAN_OOT_AUDIO_CHANNELS 2u
/* AudioPlayer_Play recebe até 3 blocos de SAMPLES_HIGH (560) quadros. */
#define LINKSPAN_OOT_AUDIO_MAX_FRAMES 1680u

typedef struct ShipOotAudioMixHookV1 {
    uint32_t size;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frames;
    int16_t* samples;
} ShipOotAudioMixHookV1;

#endif
