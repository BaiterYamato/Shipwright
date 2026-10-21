#ifndef LINKSPAN_OOT_CAMERA_H
#define LINKSPAN_OOT_CAMERA_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_CAMERA_SERVICE "linkspan.oot.camera"
#define LINKSPAN_OOT_CAMERA_VERSION 1u
#define LINKSPAN_OOT_CAMERA_MAX_OWNER 128u

typedef struct ShipOotCameraViewV1 {
    uint32_t size;
    float eye[3];
    float at[3];
    float fov;
} ShipOotCameraViewV1;

/* Câmera de gameplay com dono único, disponível apenas na thread do jogo.
 *
 * acquire cria uma subcâmera do jogo a partir da vista atual, a ativa e põe a
 * principal em espera. Só funciona em gameplay com a câmera principal ativa: em
 * cutscene ou com outro dono, retorna LIMIT. O token perde a posse sozinho na
 * troca de cena ou quando o jogo ativa outra câmera (cutscene); is_owned diz se
 * ainda vale. set_view vale até a próxima chamada e é reaplicada a cada frame.
 * get_view lê a câmera ativa, de qualquer dono. Quem adquire deve liberar no
 * shutdown; release de um token que perdeu a posse devolve OK. */
typedef struct ShipOotCameraV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire)(const char* owner, uint64_t* token);
    ShipNativeStatus(SHIP_NATIVE_CALL* release)(uint64_t token);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_view)(uint64_t token, const ShipOotCameraViewV1* view);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_view)(ShipOotCameraViewV1* view);
    uint8_t(SHIP_NATIVE_CALL* is_owned)(uint64_t token);
} ShipOotCameraV1;

/* Mesmo árbitro V1/V2/Lua; roll em ângulo binário, epoch por cena.
 * Release revogado é idempotente; não restaurar outra geração/câmera. */
#define LINKSPAN_OOT_CAMERA_VERSION_2 2u

typedef struct ShipOotCameraViewV2 {
    uint32_t size;
    float eye[3];
    float at[3];
    float fov;
    int16_t roll; /* ângulo binário assinado; 0x4000 = 90 graus */
    uint16_t reserved;
} ShipOotCameraViewV2;

typedef struct ShipOotCameraLeaseV2 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t scene_epoch;
    uint64_t camera_generation;
    uint8_t owned;
    uint8_t reserved[7];
} ShipOotCameraLeaseV2;

typedef struct ShipOotCameraV2 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire)(const char* owner, uint64_t* token);
    ShipNativeStatus(SHIP_NATIVE_CALL* release)(uint64_t token);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_view)(uint64_t token, const ShipOotCameraViewV1* view);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_view)(ShipOotCameraViewV1* view);
    uint8_t(SHIP_NATIVE_CALL* is_owned)(uint64_t token);
    const char* layout_id;
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire_ex)(const char* owner, uint64_t* token, ShipOotCameraLeaseV2* lease);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_view_ex)(uint64_t token, const ShipOotCameraViewV2* view);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_view_ex)(ShipOotCameraViewV2* view);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_lease)(uint64_t token, ShipOotCameraLeaseV2* lease);
} ShipOotCameraV2;

#endif
