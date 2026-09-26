#include <algorithm>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/native/OotNativeScenes.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SceneCommand.h"
#include "soh/resource/type/scenecommand/SetCollisionHeader.h"

#define CVAR_GRAVE_HOLE_NAME CVAR_ENHANCEMENT("GraveHoles")
#define GRAVE_HOLES_DEFAULT 0
#define CVAR_GRAVE_HOLE_VALUE CVarGetInteger(CVAR_GRAVE_HOLE_NAME, GRAVE_HOLES_DEFAULT)
#define GRAVEYARD_SCENE_FILEPATH "scenes/shared/spot02_scene/spot02_scene"
#define CUSTOM_SURFACE_TYPE 32

const static std::array<std::pair<std::pair<u16, u16>, std::pair<u16, u16>>, 6> graveyardGeometryPatches = { {
    // { { startPolygon, endPolygon }, { originalSurfaceType, patchedSurfaceType } }
    { { 487, 509 }, { 20, CUSTOM_SURFACE_TYPE } }, // Floor around graves
    { { 651, 658 }, { 20, CUSTOM_SURFACE_TYPE } }, // Floor around Royal Family Tomb
    { { 613, 620 }, { 0, 15 } },                   // Grave ledges (Hylian Shield)
    { { 623, 630 }, { 0, 15 } },                   // Grave ledges (Redead)
    { { 633, 640 }, { 0, 15 } },                   // Grave ledges (Dampe)
    { { 643, 650 }, { 0, 15 } },                   // Grave ledges (Royal Family)
} };

CollisionHeader* getGraveyardCollisionHeader() {
    /*
     * Load the graveyard collision header manually. Since its position varies between versions, we cannot directly use
     * dspot02_sceneCollisionHeader_003C54. We have to scroll through the scene cmds to get the header the same way the
     * game does.
     */
    // SOH [Link-Span] (Unbound 0.8) a cena que o jogo carrega: com a base Unbound montada é o scene.json, e
    // remendar o arquivo vanilla não mudava nada em jogo. Mesmo fallback do OTRPlay_SpawnScene. O recurso fica
    // preso aqui porque o header remendado vive dentro dele.
    static std::shared_ptr<SOH::Scene> graveyardScene;
    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    std::string scenePath;
    if (ShipLuaHost::OotSceneOverridePath(SCENE_GRAVEYARD, false, scenePath)) {
        // O caminho vem de um mod: pode não ser uma cena.
        graveyardScene = std::dynamic_pointer_cast<SOH::Scene>(resourceManager->LoadResource(scenePath));
        if (graveyardScene == nullptr) {
            SPDLOG_ERROR("Grave Hole Jumps: {} não carregou como cena; usando o recurso vanilla", scenePath);
        }
    }
    if (graveyardScene == nullptr) {
        scenePath = GRAVEYARD_SCENE_FILEPATH;
        graveyardScene = std::dynamic_pointer_cast<SOH::Scene>(resourceManager->LoadResource(scenePath));
    }
    SOH::Scene* scene = graveyardScene.get();
    if (scene == nullptr) {
        SPDLOG_ERROR("Grave Hole Jumps: não foi possível carregar {}", scenePath);
        return nullptr;
    }
    SOH::SetCollisionHeader* sceneCmd = nullptr;
    for (size_t i = 0; i < scene->commands.size(); i++) {
        auto cmd = scene->commands[i];
        // SOH [Link-Span] comando que a fábrica não entendeu fica nulo na lista (o OTRScene_ExecuteCommands pula),
        // e a cena de um mod pode ter um.
        if (cmd == nullptr) {
            continue;
        }
        if (cmd->cmdId == SOH::SceneCommandID::SetCollisionHeader) {
            sceneCmd = static_cast<SOH::SetCollisionHeader*>(cmd.get());
            break;
        }
    }
    if (sceneCmd == nullptr || sceneCmd->collisionHeader == nullptr) {
        SPDLOG_ERROR("Grave Hole Jumps: {} não tem colisão", scenePath);
        return nullptr;
    }
    CollisionHeader* graveyardColHeader = (CollisionHeader*)sceneCmd->GetRawPointer();
    uint32_t surfaceTypesCount = sceneCmd->collisionHeader->surfaceTypesCount;
    // SOH [Link-Span] uma colisão trocada por mod pode ter outra contagem: sem a vaga do tipo extra e sem os
    // polígonos da tabela, o remendo escreveria fora dos arrays.
    uint32_t lastPatchedPoly = 0;
    for (auto& mappingPatch : graveyardGeometryPatches) {
        lastPatchedPoly = std::max<uint32_t>(lastPatchedPoly, mappingPatch.first.second);
    }
    if (surfaceTypesCount > CUSTOM_SURFACE_TYPE || graveyardColHeader->numPolygons <= lastPatchedPoly) {
        SPDLOG_ERROR("Grave Hole Jumps: a colisão de {} não é a do cemitério vanilla ({} tipos, {} polígonos)",
                     scenePath, surfaceTypesCount, graveyardColHeader->numPolygons);
        return nullptr;
    }
    SPDLOG_INFO("Grave Hole Jumps: remendando a colisão de {}", scenePath);

    /*
     * Copy the surface type list and give ourselves some extra space to create another surface type for Link to fall
     * into graves. NTSC 1.0's graveyard has 31 surface types, while later versions have 32. The contents of the lists
     * are shifted somewhat between versions, so to be safe we just create an extra slot that is not in any version.
     */
    static SurfaceType newSurfaceTypes[33];
    memcpy(newSurfaceTypes, graveyardColHeader->surfaceTypeList, sizeof(SurfaceType) * surfaceTypesCount);
    newSurfaceTypes[CUSTOM_SURFACE_TYPE] = SurfaceType_Unpack(0x24000004, 0xFC8); // SOH [Unbound] unpacked storage
    graveyardColHeader->surfaceTypeList = newSurfaceTypes;

    return graveyardColHeader;
}

void ApplyGraveyardGeometryPatches() {
    static CollisionHeader* graveyardColHeader = getGraveyardCollisionHeader();
    if (graveyardColHeader == nullptr) {
        return;
    }
    for (auto& mappingPatch : graveyardGeometryPatches) {
        for (int i = mappingPatch.first.first; i <= mappingPatch.first.second; i++) {
            CollisionPoly* poly = &graveyardColHeader->polyList[i];
            poly->type = CVAR_GRAVE_HOLE_VALUE ? mappingPatch.second.first : mappingPatch.second.second;
        }
    }
}

void RegisterGraveHoleJumps() {
    ApplyGraveyardGeometryPatches();
}

static RegisterShipInitFunc initFunc(RegisterGraveHoleJumps, { CVAR_GRAVE_HOLE_NAME });
