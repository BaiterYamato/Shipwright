#ifndef LINKSPAN_MM_AUDIO_H
#define LINKSPAN_MM_AUDIO_H
#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_MM_AUDIO_SERVICE "linkspan.mm.audio"
#define LINKSPAN_MM_AUDIO_VERSION 1u

/* Tabela do mod MM Base, sem tipos do jogo; todas as chamadas na thread do jogo.
 * Consumidores declaram dependência obrigatória de linkspan.mm.base e descartam
 * a tabela antes de seu unload. Ausência de assets retorna UNSUPPORTED.
 * O mapa contém política fornecida pelo consumidor: [base, base+count) -> +offset.
 * Não há mapa instalado por padrão. clear_voice_map também é obrigatório ao sair
 * de uma forma. O hook consome a voz apenas depois de enfileirar a amostra.
 * Consulta paginada: output=NULL/capacity=0 informa o total; index é o offset.
 * Campos de saída exigem size >= sizeof da versão, sem reter ponteiros do host. */
typedef struct ShipMmSfxEntryV1 {
    uint32_t size;
    uint32_t index;
    uint32_t sample_bytes;
    uint32_t codec;
    float tuning;
    uint32_t resolved;
} ShipMmSfxEntryV1;

typedef struct ShipMmAudioStatusV1 {
    uint32_t size;
    uint32_t archive_mounted;
    uint32_t loader_available;
    uint32_t pending;
    uint32_t voice_map_active;
    uint32_t sequence_ready;
    uint32_t aliases_accepted;
    uint32_t aliases_rejected;
} ShipMmAudioStatusV1;

typedef struct ShipMmAudioV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* play_sample)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* play_font_sfx)(uint32_t font, uint32_t index);
    ShipNativeStatus(SHIP_NATIVE_CALL* play_sfx_id)(uint32_t mm_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_voice_map)(uint32_t base, uint32_t count, int32_t offset);
    ShipNativeStatus(SHIP_NATIVE_CALL* clear_voice_map)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* query_sfx_table)(uint32_t font, uint32_t index, ShipMmSfxEntryV1* output,
                                                        uint32_t capacity, uint32_t* total, uint32_t* written);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_status)(ShipMmAudioStatusV1* output);
} ShipMmAudioV1;
#endif
