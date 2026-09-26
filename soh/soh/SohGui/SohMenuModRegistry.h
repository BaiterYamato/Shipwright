#pragma once

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <shiplua/menu/MenuRegistry.h>

namespace SohGui {

class SohMenuModRegistry final : public ShipLua::MenuRegistry {
  public:
    ShipLua::Result<void> Upsert(ShipLua::MenuPage page) override;
    void RemoveMod(const std::string& modId) noexcept override;
    std::vector<ShipLua::MenuPage> Snapshot() const override;
    void Enqueue(ShipLua::MenuInput input) override;
    std::vector<ShipLua::MenuInput> DrainInputs(const std::string& modId) override;

    void Draw();

  private:
    // Valor de um slider em edição. A página publicada só alcança o valor no próximo quadro do jogo; até lá a UI
    // mostra o valor local, senão o slider volta ao antigo por alguns quadros e treme. Só a thread da UI usa.
    struct Editing {
        ShipLua::MenuValue value;
        int framesLeft = 0;
    };

    using Key = std::pair<std::string, std::string>;
    mutable std::mutex mMutex;
    std::map<Key, ShipLua::MenuPage> mPages;
    std::deque<ShipLua::MenuInput> mInputs;
    std::map<std::string, Editing> mEditing;
};

} // namespace SohGui
