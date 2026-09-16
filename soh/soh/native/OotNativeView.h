#pragma once

#include <cstdint>
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
};

void SetOotViewBridge(const OotViewBridge& bridge);
void InitializeOotNativeView(std::thread::id ownerThread = std::this_thread::get_id());
// Libera a câmera (se ainda existir) e zera os escopos.
void ResetOotNativeView();
const ShipOotCameraV1& GetOotNativeCameraService();
const ShipOotRenderV1& GetOotNativeRenderService();

// Uma vez por frame, antes do update das câmeras: perde a posse se a câmera mudou,
// senão reaplica a vista.
void UpdateOotCamera();

// Escopo de draw em que linkspan.oot.render vale. Aninhável; o Leave desfaz os push
// sem pop feitos dentro dele.
void EnterOotRenderScope();
void LeaveOotRenderScope();

// Integração com o jogo (OotNativeViewGame.cpp, só no jogo).
void RegisterOotViewGameHooks();

} // namespace ShipLuaHost
