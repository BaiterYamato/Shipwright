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

#endif
