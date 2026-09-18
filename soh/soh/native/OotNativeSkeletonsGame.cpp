// Liga linkspan.oot.skeletons (OotNativeSkeletons.cpp) ao SkelAnime do jogo. As tabelas de
// juntas ficam no heap do host, fora da arena da cena, para o esqueleto sobreviver à troca
// de cena até o mod destruir.
#include "OotNativeSkeletons.h"

#include <new>
#include <vector>

#include "soh/ResourceManagerHelpers.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

struct SkeletonInstance {
    SkelAnime skelAnime{};
    std::vector<Vec3s> joints;
    std::vector<Vec3s> morph;
    bool flex = false;
};

constexpr u8 kSkeletonTypeFlex = 1;
constexpr u8 kSkeletonTypeCurve = 2;

ShipNativeStatus Create(const char* skeleton, const char* animation, void** out) {
    if (!ResourceMgr_FileExists(skeleton) || !ResourceMgr_FileExists(animation)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* header = ResourceMgr_LoadSkeletonByName(skeleton, nullptr);
    if (!header || header->skeletonType == kSkeletonTypeCurve || !header->limbCount) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* instance = new (std::nothrow) SkeletonInstance;
    if (!instance) {
        return SHIP_NATIVE_FAILURE;
    }
    const s32 limbCount = header->limbCount + 1;
    instance->joints.assign(limbCount, Vec3s{});
    instance->morph.assign(limbCount, Vec3s{});
    instance->flex = header->skeletonType == kSkeletonTypeFlex;
    // O SkelAnime aceita os caminhos "__OTR__..." e registra o esqueleto para troca de assets.
    auto* skelPath = const_cast<char*>(skeleton);
    auto* animPath = reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation));
    if (instance->flex) {
        SkelAnime_InitFlex(gPlayState, &instance->skelAnime, reinterpret_cast<FlexSkeletonHeader*>(skelPath), animPath,
                           instance->joints.data(), instance->morph.data(), limbCount);
    } else {
        SkelAnime_Init(gPlayState, &instance->skelAnime, reinterpret_cast<SkeletonHeader*>(skelPath), animPath,
                       instance->joints.data(), instance->morph.data(), limbCount);
    }
    *out = instance;
    return SHIP_NATIVE_OK;
}

void Destroy(void* state) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    ResourceMgr_UnregisterSkeleton(&instance->skelAnime);
    delete instance;
}

ShipNativeStatus Play(void* state, const char* animation, float speed, uint8_t mode, float morphFrames) {
    if (!ResourceMgr_FileExists(animation)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* instance = static_cast<SkeletonInstance*>(state);
    auto* anim = reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation));
    Animation_Change(&instance->skelAnime, anim, speed, 0.0f, static_cast<f32>(Animation_GetLastFrame(anim)), mode,
                     morphFrames);
    return SHIP_NATIVE_OK;
}

uint8_t Update(void* state) {
    return SkelAnime_Update(&static_cast<SkeletonInstance*>(state)->skelAnime) ? 1 : 0;
}

void Frame(void* state, float* frame, float* lastFrame) {
    const auto& skelAnime = static_cast<SkeletonInstance*>(state)->skelAnime;
    *frame = skelAnime.curFrame;
    *lastFrame = skelAnime.endFrame;
}

void Draw(void* play, void* state) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    auto* playState = static_cast<PlayState*>(play);
    Gfx_SetupDL_25Opa(playState->state.gfxCtx);
    if (instance->flex) {
        SkelAnime_DrawFlexOpa(playState, instance->skelAnime.skeleton, instance->skelAnime.jointTable,
                              instance->skelAnime.dListCount, nullptr, nullptr, nullptr);
    } else {
        SkelAnime_DrawOpa(playState, instance->skelAnime.skeleton, instance->skelAnime.jointTable, nullptr, nullptr,
                          nullptr);
    }
}

} // namespace

namespace ShipLuaHost {

void RegisterOotSkeletonsGameBridge() {
    OotSkeletonsBridge bridge;
    bridge.create = Create;
    bridge.destroy = Destroy;
    bridge.play = Play;
    bridge.update = Update;
    bridge.frame = Frame;
    bridge.draw = Draw;
    SetOotSkeletonsBridge(bridge);
}

} // namespace ShipLuaHost
