#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <thread>

#include "oot_actors.h"
#include "oot_items.h"

namespace ShipLuaHost {

// Operações no jogo que os serviços linkspan.oot.items/actors delegam. O binding
// real fica em OotNativeItemsGame.cpp; os testes usam um falso. Nulo = UNSUPPORTED.
struct OotItemsBridge {
    // Ícone ("__OTR__..." interno, válido até o fim do processo, ou nullptr) e idade do id.
    void (*setItemVisual)(uint8_t item, const char* icon, uint8_t age) = nullptr;
    uint8_t (*getButtonItem)(uint8_t button) = nullptr;
    void (*setButtonItem)(uint8_t button, uint8_t item) = nullptr;
    // Cria ou reaproveita (mesmo nome) a entrada no ActorDB; -1 em falha.
    int32_t (*addActorType)(const char* name, const ShipOotActorTypeSpecV1& spec) = nullptr;
    void (*killActors)(int16_t actorId, uint8_t category) = nullptr;
    // path já com "__OTR__" e válido até o fim do processo.
    ShipNativeStatus (*drawDisplayList)(void* play, const char* path, uint8_t translucent) = nullptr;
    // Põe o Link para levantar o item; LIMIT se o Player não pode receber agora.
    ShipNativeStatus (*giveItem)(uint8_t item) = nullptr;
};

struct OotItemRecord {
    std::string name;
    ShipOotItemUseFn use = nullptr;
    void* user = nullptr;
    uint8_t age = LINKSPAN_OOT_ITEM_AGE_ANY;
    // set_get_item (items v2); modelPath já com "__OTR__", válido até o fim do processo.
    bool hasGetItem = false;
    const char* modelPath = nullptr;
    uint8_t modelLayer = LINKSPAN_OOT_ITEMS_LAYER_OPAQUE;
    float modelScale = 1.0f;
    std::string message;
    ShipOotItemReceiveFn receive = nullptr;
    void* receiveUser = nullptr;
};

struct OotActorTypeRecord {
    std::string name;
    ShipOotActorTypeSpecV1 spec{};
    bool active = false;
};

void SetOotItemsBridge(const OotItemsBridge& bridge);
void InitializeOotNativeItems(std::thread::id ownerThread = std::this_thread::get_id());
// Remove registros (sem tocar no jogo); tipos de ator ficam inativos e mantêm o id.
void ResetOotNativeItems();
const ShipOotItemsV1& GetOotNativeItemsService();
const ShipOotItemsV2& GetOotNativeItemsServiceV2();
const ShipOotActorsV1& GetOotNativeActorsService();

// Consultas do jogo, na thread do jogo.
bool IsOotSyntheticItemId(uint32_t item);
const OotItemRecord* FindOotItem(uint8_t item);
const OotActorTypeRecord* FindOotActorType(int32_t actorId);
// Durante o draw de um tipo de mod; draw_display_list só vale dentro dele.
void SetOotActorDrawActive(bool active);
// Em volta de cada callback de ator de mod. O ActorDB guarda entradas num vector e
// Actor_Spawn segura um ponteiro para a entrada durante o init: registrar tipo aí
// dentro poderia realocar a tabela, então register_actor_type recusa.
void EnterOotActorCallback();
void LeaveOotActorCallback();

// Botões C com item sintético, por nome, e o inverso na carga do save. Botões com
// id sintético sem registro, ou com nome desconhecido, ficam vazios.
std::map<uint8_t, std::string> ExportOotItemButtons();
void RestoreOotItemButtons(const std::map<uint8_t, std::string>& buttons);

// Integração com o jogo (OotNativeItemsGame.cpp, só no jogo).
void RegisterOotItemGameHooks();
void StoreOotItemButtons();
void RestoreOotItemButtonsFromSave();

} // namespace ShipLuaHost
