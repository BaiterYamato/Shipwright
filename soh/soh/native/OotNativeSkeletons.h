#pragma once

#include <cstdint>
#include <string_view>
#include <thread>

#include "oot_skeletons.h"

namespace ShipLuaHost {

// Operações no jogo que linkspan.oot.skeletons delega. O binding real fica em
// OotNativeSkeletonsGame.cpp; os testes usam um falso. Nulo = UNSUPPORTED.
// Caminhos já com "__OTR__" e válidos até o fim do processo (o SkelAnime guarda o
// ponteiro da animação).
struct OotSkeletonsBridge {
    ShipNativeStatus (*create)(const char* skeleton, const char* animation, void** instance) = nullptr;
    void (*destroy)(void* instance) = nullptr;
    ShipNativeStatus (*play)(void* instance, const char* animation, float speed, uint8_t mode,
                             float morphFrames) = nullptr;
    // 1 quando uma animação ONCE terminou.
    uint8_t (*update)(void* instance) = nullptr;
    void (*frame)(void* instance, float* frame, float* lastFrame) = nullptr;
    void (*draw)(void* play, void* instance) = nullptr;
    // V2 (HOST-SLIM-001). format = SHIP_OOT_SKELETON_*; caminhos com "__OTR__", internados.
    ShipNativeStatus (*createEx)(uint32_t format, const char* skeleton, const char* animation,
                                 const int16_t* baseTranslation, void** instance, float* assetLastFrame) = nullptr;
    // reverse: do último quadro ao 0, velocidade negativa no SkelAnime.
    ShipNativeStatus (*playEx)(void* instance, const char* animation, float speed, uint8_t mode, float morphFrames,
                               bool reverse, float* assetLastFrame) = nullptr;
    // present: o arquivo existe; loaded: é animação do formato da instância.
    ShipNativeStatus (*animationInfo)(void* instance, const char* animation, ShipOotAnimationInfoV2* info) = nullptr;
    void (*state)(void* instance, float* frame, float* endFrame, float* speed) = nullptr;
    // Desenha e chama limb(context, membro) depois de cada membro.
    void (*drawEx)(void* play, void* actor, void* instance, void (*limb)(void* context, int32_t limb),
                   void* context) = nullptr;
    // PlayState::gameplayFrames.
    uint64_t (*tick)() = nullptr;
};

void SetOotSkeletonsBridge(const OotSkeletonsBridge& bridge);
void InitializeOotNativeSkeletons(std::thread::id ownerThread = std::this_thread::get_id());
// Libera todos os esqueletos.
void ResetOotNativeSkeletons();
const ShipOotSkeletonsV1& GetOotNativeSkeletonsService();
uint32_t OotSkeletonCount();

// HOST-SLIM-001 (RFC 0029).
const ShipOotSkeletonsV2& GetOotNativeSkeletonsServiceV2();
void ReleaseOotSkeletonOwner(std::string_view owner);
// Instâncias de esqueletos V2 destruídos, soltas no começo do frame seguinte (LinkSpan_PlayUpdate).
void ReleaseRetiredOotSkeletons();
// Instância do jogo de um handle vivo; só o host (o player-body desenha e move por ela).
void* OotSkeletonInstance(uint64_t skeleton);

// Integração com o jogo (OotNativeSkeletonsGame.cpp, só no jogo).
void RegisterOotSkeletonsGameBridge();

} // namespace ShipLuaHost
