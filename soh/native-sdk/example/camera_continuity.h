#pragma once

#include <cmath>
#include "z64.h"

// GameState allocations can be reused, including when reloading the same scene.
struct CameraEpoch {
    const PlayState* play = nullptr;
    int scene = -1;
    unsigned frame = 0;

    bool Observe(const PlayState* current) {
        const bool changed = current != play ||
            (current && (current->sceneNum != scene || current->state.frames < frame));
        play = current;
        scene = current ? current->sceneNum : -1;
        frame = current ? current->state.frames : 0;
        return changed;
    }
};

using CameraGeometryFn = VecSph* (*)(VecSph*, Vec3f*, Vec3f*);

inline bool CameraCanResume(const PlayState* play, const SaveContext* save, const Player* player) {
    return play && save && player && save->gameMode == GAMEMODE_NORMAL &&
        play->activeCamera == CAM_ID_MAIN && play->cameraPtrs[CAM_ID_MAIN] &&
        play->cameraPtrs[CAM_ID_MAIN]->status == CAM_STAT_ACTIVE &&
        play->pauseCtx.state == 0 && play->csCtx.state == CS_STATE_IDLE &&
        play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
        !(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE));
}

// Use the rendered view, since a door/cutscene can leave the main camera stale.
// The host's own conversion preserves its binary-angle conventions exactly.
inline bool ResumeCameraFromView(PlayState& play, Camera& camera, CameraGeometryFn geometry) {
    if (!geometry) return false;
    const Vec3f& at = play.view.lookAt;
    const Vec3f& eye = play.view.eye;
    if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z) ||
        !std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) return false;
    VecSph pose{};
    geometry(&pose, &play.view.lookAt, &play.view.eye);
    if (!std::isfinite(pose.r) || pose.r < 0.01f) return false;
    camera.at = at;
    camera.eye = camera.eyeNext = eye;
    camera.dist = pose.r;
    if (std::isfinite(play.view.fovy) && play.view.fovy > 0 && play.view.fovy < 180)
        camera.fov = play.view.fovy;
    play.camX = pose.yaw;
    play.camY = pose.pitch;
    play.manualCamera = true;
    return true;
}
