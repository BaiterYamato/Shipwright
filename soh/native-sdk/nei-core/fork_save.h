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
    void OnSaving();

    // Os testes usam o mesmo serializador do hook sem precisar do jogo.
    std::string SerializeForTests() const;

  private:
    void LoadFields();

    const ShipOotSaveV1* mSave = nullptr;
    uint64_t mSaveHandle = 0;
};

} // namespace LinkSpanNei
