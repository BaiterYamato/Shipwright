#pragma once

#include <cstdint>
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
};

void SetOotSkeletonsBridge(const OotSkeletonsBridge& bridge);
void InitializeOotNativeSkeletons(std::thread::id ownerThread = std::this_thread::get_id());
// Libera todos os esqueletos.
void ResetOotNativeSkeletons();
const ShipOotSkeletonsV1& GetOotNativeSkeletonsService();
uint32_t OotSkeletonCount();

// Integração com o jogo (OotNativeSkeletonsGame.cpp, só no jogo).
void RegisterOotSkeletonsGameBridge();

} // namespace ShipLuaHost
