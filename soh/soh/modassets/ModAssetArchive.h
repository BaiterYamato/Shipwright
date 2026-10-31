#pragma once

#include "ship/resource/archive/Archive.h"
#include <unordered_set>
#include <vector>

namespace SohModAssets {
using Names = std::unordered_map<uint64_t, std::string>;
using Hashes = std::unordered_set<uint64_t>;
using Archives = std::vector<std::shared_ptr<Ship::Archive>>;

// Índices construídos uma única vez antes da publicação. Cada substituto possui backend independente.
class ModAssetArchive final : public Ship::Archive {
  public:
    ModAssetArchive(const std::string& path, std::string prefix, Hashes aliasesOrReserved, bool explicitAliases = false,
                    std::shared_ptr<Ship::Archive> backend = nullptr);
    bool Open() override;
    bool Close() override;
    std::shared_ptr<Ship::File> LoadFile(const std::string& path) override;
    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override;
    bool WriteFile(const std::string&, const std::vector<uint8_t>&) override;
    const Names& OriginalFiles() const;
    const Hashes& Aliases() const;
    const std::string& Prefix() const;
    static bool IsRenderPath(const std::string& path);

  private:
    std::shared_ptr<Ship::Archive> mInner;
    std::string mPrefix;
    Hashes mAliasInput, mAliases;
    Names mOriginals;
    bool mExplicitAliases, mOpened = false;
};
} // namespace SohModAssets
