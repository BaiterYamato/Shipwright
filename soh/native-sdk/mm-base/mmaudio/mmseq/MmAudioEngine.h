#pragma once

// Motor de SFX do MM: liga o interpretador de sequência portado ao
// decodificador VADPCM da Fase 1 e à saída de áudio do host.
//
// API de tipos simples de propósito — é a fronteira descrita em
// MmAudioBridge.h. Quem chama daqui nunca vê um tipo do MM.

#include <cstdint>

namespace ShipLua {

// Carrega Soundfont_0 e Sequence_0 do mm.o2r, monta o contexto e dá start no
// player de SFX. Idempotente: pode ser chamada todo frame, o trabalho acontece
// uma vez. Devolve true quando o motor está pronto.
bool MmSeq_Init();

bool MmSeq_IsReady();

// True se o script da sequência saiu do bloco válido e o motor se desligou
// sozinho. Diagnóstico: distingue "não tocou" de "se perdeu no script".
bool MmSeq_PcEscaped();

// Canal em que o último sfx foi escrito, ou -1. Diagnóstico.
int MmSeq_LastChannel();

// Pico de notas simultâneas habilitadas pelo interpretador e total de amostras
// efetivamente misturadas. Zero notas = a sequência não produziu som; notas > 0
// com amostras = 0 significa que o render não achou a amostra.
void MmSeq_GetRenderStats(int* peakNotes, int* renderedSamples);

// Estado do script: canais habilitados, se o player segue vivo, e onde o pc
// está dentro da sequência. Canal 0 com player vivo significa que o script roda
// mas nunca liga canal — problema antes da nota.
void MmSeq_GetScriptStats(int* channelsOn, int* playerAlive, int* pcOffset);

// Estado do canal 0, que recebe o pedido: onde o script dele está, se está em
// delay, e o que as portas 0 (enable) e 1 (pronto) contêm. io0 voltando a -1
// significa que o script consumiu o pedido; permanecer 1 significa que nunca o
// leu.
void MmSeq_GetChannel0(int* pc, int* delay, int* io0, int* io1);

// waveId fora da faixa: quantos e o primeiro visto. Se contar > 0, a resolução
// de instrumento está devolvendo valor inválido.
void MmSeq_NoteBadWave(int waveId);
void MmSeq_GetBadWave(int* count, int* last);

// Onde o pc estava ao escapar (offset relativo ao início da sequência), quantos
// ticks rodaram antes, e o tamanho do bloco. Offset negativo ou >= seqSize logo
// no primeiro tick significa que o ponteiro nunca foi válido; um offset dentro
// da faixa após muitos ticks significa que o script andou e se perdeu.
void MmSeq_GetEscapeInfo(long long* offset, int* ticks, unsigned int* seqSize);

// Primeiros 8 bytes da sequência carregada, para conferir se são script mesmo.
void MmSeq_GetSeqHead(unsigned char* out8);

// Dispara um sfxId do MM escrevendo nas portas de channel IO da sequência,
// que é como o próprio MM toca seus efeitos.
bool MmSeq_PlaySfx(uint16_t sfxId);

// Avança a sequência e mistura o resultado no buffer de saída.
// Chamada da THREAD DE ÁUDIO. Formato do host: 32 kHz, estéreo, s16
// intercalado; `frames` é o número de quadros estéreo.
void MmSeq_RenderInto(int16_t* buffer, uint32_t frames);

} // namespace ShipLua
