#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <shiplua/native/NativeHooks.h>

#include "oot_hooks.h"

namespace ShipLuaHost {
struct OotHookPoints {
    uint64_t playUpdate = 0;
    uint64_t actorInit = 0;
    uint64_t playDrawEnd = 0;
    uint64_t actorUpdate = 0;
    uint64_t actorDraw = 0;
    uint64_t saveLoaded = 0;
    uint64_t saveSaving = 0;
    uint64_t saveDeleted = 0;
    uint64_t saveCopied = 0;
    uint64_t playerLimbDraw = 0;
    uint64_t roomActors = 0;
    uint64_t renderActorDraw = 0;
    uint64_t renderWorldLights = 0;
    uint64_t renderSkyGradient = 0;
    uint64_t renderSky = 0;
    uint64_t renderSkyClouds = 0;
    uint64_t renderFileSelectSky = 0;
    uint64_t pointLightColor = 0;
    uint64_t anchorState = 0;
    uint64_t fairyLights = 0;
    uint64_t playerVoiceSfx = 0;
    uint64_t playerRollAllowed = 0;
    uint64_t playerLedgeGrabAllowed = 0;
    uint64_t playerAction = 0;
    uint64_t playerUpdateEnd = 0;
    uint64_t playerMaskDraw = 0;
    uint64_t playDestroy = 0;
    uint64_t audioMix = 0;
    uint64_t modsArchives = 0;
    uint64_t actorDamage = 0;
    uint64_t inputPad = 0;
};
void SetOotHookLogger(void (*logger)(const std::string& message));
// Cria um registro novo com os pontos do host declarados. Chamar na thread do jogo.
std::shared_ptr<ShipLua::NativeHookRegistry> CreateOotHookRegistry();
ShipLua::NativeHookRegistry* GetOotHookRegistry();
const OotHookPoints& GetOotHookPoints();
void ResetOotHooks();
// Dispara um ponto oot.save.* (observe) se houver hook; na thread do jogo.
void DispatchOotSaveHook(uint64_t point, int32_t slot, int32_t otherSlot);
// Dispara oot.room.actors sobre `entries` (`count` em uso de `capacity`) e devolve a
// contagem final. Payload inválido depois dos hooks (contagem acima da capacidade,
// buffer ou capacidade trocados) mantém `count` e registra no log.
uint32_t DispatchOotRoomActors(void* play, int32_t sceneId, int32_t room, int32_t layer, const char* roomPath,
                               ShipOotActorEntryV2* entries, uint32_t count, uint32_t capacity);
void DispatchOotPointLightColor(ShipOotPointLightColorHookV2* payload);
bool HasOotPointLightColorHooks();
// HOST-SLIM-001 (RFC 0029): despachos dos pontos do Link e do Play, sem tipos do jogo. Thread do jogo.
bool HasOotHooks(uint64_t point);
// 1 quando um hook consumiu a voz; senão *sfx recebe o som final.
uint8_t DispatchOotPlayerVoice(void* play, void* player, uint16_t* sfx);
// E entre `allowed` e o resultado dos hooks: um hook só veta.
uint8_t DispatchOotPlayerGate(uint64_t point, void* play, void* player, uint8_t allowed);
// Roda `action(context)` se `vanillaAllowed`, entre os observers; devolve 1 se rodou.
uint8_t DispatchOotPlayerAction(void* play, void* player, void* input, uint8_t vanillaAllowed,
                                void (*action)(void* context), void* context);
void DispatchOotPlayerTick(uint64_t point, void* play, void* player);
// REPLACE do hook ou `drawVanilla(context)`, este só se `vanillaAllowed`.
void DispatchOotPlayerMaskDraw(void* play, void* player, int32_t mask, uint8_t vanillaAllowed,
                               void (*drawVanilla)(void* context), void* context);
void DispatchOotPlayDestroy(void* play);
// oot.mods.archives: um archive candidato aceito, na ordem de prioridade crescente.
struct OotModArchive {
    std::string path;          // absoluto, com '/'
    std::string owner;         // id do manifesto da pasta; vazio para arquivo solto em mods/
    std::string privatePrefix; // onde o host montou; vazio se não montou
};
// Dispara oot.mods.archives (OBSERVE) com as entradas; thread do jogo, uma vez no boot.
void DispatchOotModArchives(const std::vector<OotModArchive>& archives);
// Thread do jogo. Sem hooks, devolve damage intacto. Payload válido durante esta chamada.
float DispatchOotActorDamage(ShipOotActorDamageHookV1* payload);
// Parcela aplicada depois do Lua. Se algum estágio trocou o valor (nativeChanged guarda a troca nativa mesmo que
// o Lua devolva o original), cabe no que falta para 255 no acumulador u8 do alvo; sem troca, fica o vanilla.
float FinalizeOotActorDamage(float originalDamage, float transformedDamage, uint8_t accumulatedDamage,
                             bool nativeChanged = false);

} // namespace ShipLuaHost
