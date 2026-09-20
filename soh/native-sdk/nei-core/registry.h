#pragma once

// Registro de itens do NEI (NEI-002) sobre linkspan.oot.items v3 e linkspan.oot.save. Puro: recebe as
// tabelas do host por ponteiro, então os testes usam tabelas falsas.
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "include/linkspan/nei/nei_items.h"
#include "oot_items.h"
#include "oot_save.h"

namespace LinkSpanNei {

constexpr const char* kSaveNamespace = "nei.items";
constexpr uint32_t kSaveVersion = 1;

// Estado gravado de um id, definido ou não nesta sessão.
struct SavedState {
    bool owned = false;
    uint16_t count = 0;
    uint8_t level = 0;
};

class Registry {
  public:
    // `save` pode faltar (sem persistência); `saveHandle` vem de open_namespace.
    void Attach(const ShipOotItemsV3* items, const ShipOotSaveV1* save, uint64_t saveHandle);
    // Remove os itens do jogo e zera tudo.
    void Detach();

    ShipNativeStatus Define(const NeiItemDefinitionV1* definition, uint64_t* handle);
    ShipNativeStatus Remove(uint64_t handle);
    ShipNativeStatus Find(const char* id, uint64_t* handle) const;
    ShipNativeStatus List(NeiItemVisitFn visit, void* user) const;
    ShipNativeStatus GetState(uint64_t handle, NeiItemStateV1* state) const;
    ShipNativeStatus Give(uint64_t handle);
    ShipNativeStatus Grant(uint64_t handle);
    ShipNativeStatus Revoke(uint64_t handle);
    ShipNativeStatus SetCount(uint64_t handle, uint16_t count);
    ShipNativeStatus AddCount(uint64_t handle, int32_t delta, uint16_t* result);
    ShipNativeStatus SetLevel(uint64_t handle, uint8_t level);
    ShipNativeStatus Equip(uint64_t handle, uint8_t button);
    ShipNativeStatus Unequip(uint64_t handle);
    ShipNativeStatus GetName(uint64_t handle, uint8_t language, char* output, uint32_t capacity,
                             uint32_t* outputSize) const;

    // Hook oot.save.loaded: lê o bloco do arquivo carregado e reaplica em todos os itens definidos.
    void OnSaveLoaded();
    // Arquivo novo: esquece o estado do arquivo anterior sem ler bloco nenhum.
    void ResetForNewSlot();
    // Grava o bloco se algo mudou desde a última gravação (antes do save do jogo e a cada mudança).
    void Flush();
    std::string Stats() const;
    // Para os testes: o JSON que Flush gravaria agora.
    std::string SerializeForTests() const;

  private:
    struct Level {
        std::string icon;
        std::array<std::string, LINKSPAN_NEI_LANGUAGES> names;
        uint16_t maxCount = 0;
    };
    struct Item {
        Registry* owner = nullptr;
        uint64_t handle = 0;
        std::string id;
        uint8_t age = LINKSPAN_OOT_ITEM_AGE_ANY;
        std::string model;
        uint8_t modelLayer = 0;
        float modelScale = 1.0f;
        std::array<std::string, LINKSPAN_NEI_LANGUAGES> messages;
        std::vector<Level> levels;
        uint16_t giveCount = 0;
        NeiItemUseFn use = nullptr;
        NeiItemReceivedFn received = nullptr;
        void* user = nullptr;
        uint8_t runtime = 0xFF;
    };

    static ShipNativeStatus SHIP_NATIVE_CALL UseTrampoline(void* user, uint8_t item, uint8_t button);
    static ShipNativeStatus SHIP_NATIVE_CALL ReceiveTrampoline(void* user, uint8_t item);

    Item* Lookup(uint64_t handle) const;
    SavedState& StateOf(const Item& item);
    const SavedState* FindState(const Item& item) const;
    uint16_t MaxCount(const Item& item) const;
    uint8_t ButtonOf(const Item& item) const;
    // Ícone e número do botão conforme o estado; tira dos botões quem não é possuído.
    void Refresh(Item& item);
    void MarkDirty();

    const ShipOotItemsV3* mItems = nullptr;
    const ShipOotSaveV1* mSave = nullptr;
    uint64_t mSaveHandle = 0;
    uint64_t mNextHandle = 1;
    std::map<uint64_t, std::unique_ptr<Item>> mDefined;
    std::map<std::string, uint64_t, std::less<>> mById;
    std::map<std::string, SavedState, std::less<>> mStates;
    bool mDirty = false;
    uint32_t mLoads = 0;
    uint32_t mReceived = 0;
    uint32_t mUses = 0;
};

} // namespace LinkSpanNei
