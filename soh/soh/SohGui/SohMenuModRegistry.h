#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <shiplua/menu/MenuRegistry.h>

namespace SohGui {

class SohMenuModRegistry final : public ShipLua::MenuRegistry {
  public:
    void SetLoadedMods(std::vector<std::string> modIds);
    std::vector<std::pair<std::string, std::string>> SidebarMods() const;
    ShipLua::Result<void> Upsert(ShipLua::MenuPage page) override;
    void RemoveMod(const std::string& modId) noexcept override;
    std::vector<ShipLua::MenuPage> Snapshot() const override;
    void Enqueue(ShipLua::MenuInput input) override;
    std::vector<ShipLua::MenuInput> DrainInputs(const std::string& modId) override;

    void DrawOverview();
    void DrawOwner(const std::string& modId);

  private:
    // Valor de um slider em edição. A página publicada só alcança o valor no próximo quadro do jogo; até lá a UI
    // mostra o valor local, senão o slider volta ao antigo por alguns quadros e treme.
    struct Editing {
        ShipLua::MenuValue value;
        int framesLeft = 0;
    };

    using Key = std::pair<std::string, std::string>;
    using EditingKey = std::tuple<std::string, std::string, std::string, std::uint64_t>;
    mutable std::mutex mMutex;
    std::map<Key, ShipLua::MenuPage> mPages;
    std::vector<std::string> mLoadedMods;
    std::deque<ShipLua::MenuInput> mInputs;
    std::map<EditingKey, Editing> mEditing;
};

} // namespace SohGui
