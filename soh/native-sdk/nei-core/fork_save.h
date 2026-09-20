// Persistencia do NeiSaveData do fork no namespace nei.state do coremod.
#pragma once

#include <cstdint>
#include <string>

#include "oot_save.h"

namespace LinkSpanNei {

constexpr const char* kForkSaveNamespace = "nei.state";
constexpr uint32_t kForkSaveVersion = 1;

class ForkSave {
  public:
    void Attach(const ShipOotSaveV1* save, uint64_t handle);
    void Detach();
    void OnSaveLoaded();
    // Arquivo novo: sentinelas do fork sem ler o bloco, para não levar o estado do arquivo anterior.
    void ResetForNewSlot();
    void OnSaving();

    // Os testes usam o mesmo serializador do hook sem precisar do jogo.
    std::string SerializeForTests() const;
    // Versão com que o bloco do arquivo carregado foi escrito (0 sem bloco); maior que a atual = bloco preservado.
    uint32_t StoredVersion() const {
        return mStoredVersion;
    }

  private:
    void LoadFields();

    const ShipOotSaveV1* mSave = nullptr;
    uint64_t mSaveHandle = 0;
    uint32_t mStoredVersion = 0;
};

} // namespace LinkSpanNei
