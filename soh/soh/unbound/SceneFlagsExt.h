#pragma once
// SOH [Unbound] Room-keyed flags for rooms >= 32, which do not fit the u32 masks in SavedSceneFlags /
// ActorContext. Ported from the Unbound fork's SceneDB (fd236b9) without its scene registry: the host keys the
// persisted bits by the stable scene name from linkspan.oot.scenes (SCENE_* or the mod scene id).
// Clear flags are staged like ActorContext.flags: LoadClear on scene init (which also resets temp flags),
// SaveClear alongside Play_SaveSceneFlags, so unsaved flags are discarded on game over for every room alike.
#include <stdint.h>

#ifdef __cplusplus
class SaveManager;
void SceneFlagsExt_RegisterSaveFunctions(SaveManager& saveManager); // called from the SaveManager ctor

extern "C" {
#endif

typedef enum SceneFlagsExtKind {
    SCENE_FLAGS_EXT_CLEAR,
    SCENE_FLAGS_EXT_TEMP_CLEAR,
} SceneFlagsExtKind;

int32_t SceneFlagsExt_Get(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit);
void SceneFlagsExt_Set(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit);
void SceneFlagsExt_Unset(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit);
void SceneFlagsExt_LoadClear(int32_t sceneNum);
void SceneFlagsExt_SaveClear(int32_t sceneNum);

#ifdef __cplusplus
}
#endif
