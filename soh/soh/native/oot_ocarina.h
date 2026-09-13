#ifndef LINKSPAN_OOT_OCARINA_H
#define LINKSPAN_OOT_OCARINA_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_OCARINA_SERVICE "linkspan.oot.ocarina"
#define LINKSPAN_OOT_OCARINA_VERSION 1u
#define LINKSPAN_OOT_OCARINA_MAX_NOTES 8u

/* Serviço para mods de instrumento (OOT-MIC-001), só na thread do jogo.
 *
 * is_active e get_available_song_flags refletem o último update de input da
 *   ocarina: ativo enquanto o jogo espera uma música, com um bit por
 *   OcarinaSongId aceito (0=Minuet ... 11=Storms, 12=Scarecrow).
 * get_song_count: músicas consultáveis a partir do id 0 (12 fixas; 13 depois de
 *   gravada a música do Espantalho).
 * get_song_pattern: índices de nota do jogo (0=A/D4, 1=C-Down/F4, 2=C-Right/A4,
 *   3=C-Left/B4, 4=C-Up/D5). notes=NULL com capacity=0 consulta o tamanho;
 *   capacidade menor retorna SHIP_NATIVE_LIMIT.
 * submit_song: música aceita neste momento. O jogo a recebe no próximo update da
 *   ocarina como se tivesse sido tocada e aplica o efeito nativo; a entrega é
 *   descartada se a ocarina fechar antes. */
typedef struct ShipOotOcarinaV1 {
    uint32_t size;
    uint8_t(SHIP_NATIVE_CALL* is_active)(void);
    uint16_t(SHIP_NATIVE_CALL* get_available_song_flags)(void);
    uint8_t(SHIP_NATIVE_CALL* get_song_count)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_song_pattern)(uint8_t song, uint8_t* notes, uint32_t capacity,
                                                        uint32_t* output_count);
    ShipNativeStatus(SHIP_NATIVE_CALL* submit_song)(uint8_t song);
} ShipOotOcarinaV1;

#endif
