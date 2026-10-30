#pragma once
#include "server.h"
#include "oot_engine.h"
#include "oot_camera.h"
#include "oot_world.h"
#include "oot_scene_query.h"
#include "z64.h"
#include "z64player.h"

namespace Playtest {
class Gameplay {
  public:
    bool Init(const ShipNativeRuntime* runtime);
    // Chamado só na thread do jogo e depois de identificar PlayState, nunca na thread TCP.
    void Tick(PlayState* play, uint64_t frame, Server& server, ShipOotInputPadV1* pad);
    void Cleanup();
    void SceneEnded();
    Json Observation() const;
  private:
    const ShipOotEngineV1* engine_ = nullptr;
    const ShipOotMovementV1* movement_ = nullptr;
    const ShipOotCameraV1* camera_ = nullptr;
    const ShipOotWorldV1* world_ = nullptr;
    const ShipOotSceneQueryV1* lists_ = nullptr;
    int32_t (*inCs_)(PlayState*) = nullptr;
    int16_t (*inputYaw_)(Camera*) = nullptr;
    PlayState* play_ = nullptr;
    Player* player_ = nullptr;
    void* segment_ = nullptr;
    uint32_t previousTick_ = 0;
    uint64_t epoch_ = 0, cameraToken_ = 0, frame_ = 0;
    std::string Blocked() const;
    Json CameraState() const;
    Json Actors(const Json& args) const;
    Json SceneMap(const Json& args) const;
    std::optional<Json> Execute(Request& request, InputQueue& queue, ShipOotInputPadV1* pad);
};
} // namespace Playtest
