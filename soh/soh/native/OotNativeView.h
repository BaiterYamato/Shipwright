#pragma once

#include <cstdint>
#include <string_view>
#include <thread>

#include "oot_camera.h"
#include "oot_render.h"

namespace ShipLuaHost {

// Operações no jogo que linkspan.oot.camera/render delegam. O binding real fica em
// OotNativeViewGame.cpp; os testes usam um falso. Nulo = UNSUPPORTED.
struct OotViewBridge {
    // Cria e ativa a subcâmera a partir da vista atual; LIMIT fora de gameplay ou em cutscene.
    ShipNativeStatus (*cameraCreate)(int16_t* camera, void** play) = nullptr;
    // Ainda é a câmera ativa da mesma cena.
    bool (*cameraValid)(int16_t camera, void* play) = nullptr;
    // Remove a subcâmera se ela ainda existir e devolve a principal.
    void (*cameraDestroy)(int16_t camera, void* play) = nullptr;
    void (*cameraApply)(int16_t camera, void* play, const ShipOotCameraViewV1& view) = nullptr;
    ShipNativeStatus (*cameraRead)(ShipOotCameraViewV1* view) = nullptr;
    // path já com "__OTR__" e válido até o fim do processo.
    ShipNativeStatus (*drawDisplayList)(void* play, const char* path, uint8_t layer) = nullptr;
    void (*matrixPush)() = nullptr;
    void (*matrixPop)() = nullptr;
    void (*matrixTranslate)(float x, float y, float z) = nullptr;
    void (*matrixScale)(float x, float y, float z) = nullptr;
    void (*matrixRotateZYX)(int16_t x, int16_t y, int16_t z) = nullptr;
    ShipNativeStatus (*emitToonKey)(uint8_t layer, int8_t dx, int8_t dy, int8_t dz,
                                    uint8_t r, uint8_t g, uint8_t b) = nullptr;
    ShipNativeStatus (*emitStencil)(uint8_t layer, uint8_t mode) = nullptr;
    ShipNativeStatus (*emitToonShadow)(uint8_t layer, int16_t feetClampY, float size) = nullptr;
    ShipNativeStatus (*flushToonShadows)(uint8_t layer) = nullptr;
    void (*setToonRamp)(float center, float softness, float highlight, float shadow, bool debugBands) = nullptr;
    void (*setToonShadowParams)(float opacity, float minElevation, float slabDepth, float slabRise,
                                int32_t edgeSoftness, bool showVolume) = nullptr;
    // Devolve rampa e sombra aos valores padrão do renderer.
    void (*resetToonLook)() = nullptr;
    // V3. `gfx` é o GraphicsContext do escopo de draw.
    void (*matrixTranslateNew)(float x, float y, float z) = nullptr;
    // Eixo já normalizado.
    void (*matrixRotateAxis)(float radians, float x, float y, float z) = nullptr;
    const void* (*exportMatrix)(void* gfx) = nullptr;
    void (*drawNative)(void* gfx, const void* displayList, uint8_t layer) = nullptr;
    // gSPToon nas duas camadas.
    void (*setToon)(void* gfx, bool enabled) = nullptr;
    ShipNativeStatus (*frameInfo)(ShipOotRenderFrameInfoV1* info) = nullptr;
    void (*interpolationOpen)(const void* key, int32_t child) = nullptr;
    void (*interpolationClose)() = nullptr;
};

void SetOotViewBridge(const OotViewBridge& bridge);
void InitializeOotNativeView(std::thread::id ownerThread = std::this_thread::get_id());
// Libera a câmera (se ainda existir) e zera os escopos.
void ResetOotNativeView();
const ShipOotCameraV1& GetOotNativeCameraService();
const ShipOotRenderV1& GetOotNativeRenderService();
const ShipOotRenderV2& GetOotNativeRenderServiceV2();
const ShipOotRenderV3& GetOotNativeRenderServiceV3();
void ReleaseOotRenderOwner(std::string_view owner);

// Uma vez por frame, antes do update das câmeras: perde a posse se a câmera mudou,
// senão reaplica a vista.
void UpdateOotCamera();

enum class OotRenderScopeKind : uint8_t {
    Draw,
    // Hook oot.render.actor_draw: vale também set_actor_toon_enabled.
    ActorDraw,
};

// Escopo de draw em que linkspan.oot.render vale. Aninhável; o Leave desfaz os push e fecha os filhos de
// interpolação deixados abertos dentro dele. `gfx` é o GraphicsContext do desenho (nulo nos testes).
void EnterOotRenderScope(void* gfx = nullptr, OotRenderScopeKind kind = OotRenderScopeKind::Draw);
void LeaveOotRenderScope();
// Há um escopo de draw aberto (linkspan.oot.skeletons desenha só dentro dele).
bool InOotRenderScope();
// Início do laço de atores (LinkSpan_RenderToonActorsEnabled): guarda se o colchete toon abre neste frame.
void BeginOotActorDrawFrame(bool toonBracket);
// Antes do hook de cada ator: religa o colchete que o ator anterior desligou (set_actor_toon_enabled).
void RestoreOotActorToon(void* gfx);
bool OotRenderToonActorsEnabled();
bool OotRenderVanillaShadowsSuppressed();
bool OotRenderVanillaPointGlowHidden();
// Algum mod registrou estado de render (linkspan.oot.render v2).
bool OotRenderStateActive();
bool OotRenderHasShadowReceivers();
bool OotRenderIsShadowReceiver(int16_t actorId);

// Integração com o jogo (OotNativeViewGame.cpp, só no jogo).
void RegisterOotViewGameHooks();

} // namespace ShipLuaHost
