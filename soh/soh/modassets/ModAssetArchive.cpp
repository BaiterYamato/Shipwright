#include "ModAssetArchive.h"
#include "ship/resource/archive/O2rArchive.h"
#include "spdlog/spdlog.h"
#include <stdexcept>
#include <utility>

namespace SohModAssets {
ModAssetArchive::ModAssetArchive(const std::string& path, std::string prefix, Hashes aliasesOrReserved,
                                 bool explicitAliases, std::shared_ptr<Ship::Archive> backend)
    : Ship::Archive(path), mInner(backend ? std::move(backend) : std::make_shared<Ship::O2rArchive>(path)),
      mPrefix(std::move(prefix)), mAliasInput(std::move(aliasesOrReserved)), mExplicitAliases(explicitAliases) {
}

bool ModAssetArchive::IsRenderPath(const std::string& path) {
    return path.rfind("objects/", 0) == 0 || path.rfind("textures/", 0) == 0;
}

bool ModAssetArchive::Open() {
    if (mOpened)
        throw std::logic_error("ModAssetArchive: reabertura de wrapper imutavel");
    mOpened = true;
    if (!mInner->Open())
        return false;
    mOriginals = *mInner->ListFiles();
    for (const auto& [hash, path] : mOriginals) {
        IndexFile(mPrefix + path);
        if (IsRenderPath(path) && (mExplicitAliases ? mAliasInput.contains(hash) : !mAliasInput.contains(hash))) {
            IndexFile(path);
            mAliases.insert(hash);
        }
    }
    if (mExplicitAliases && mAliases != mAliasInput)
        return false;
    Hashes().swap(mAliasInput); // A reserva do VFS inicial só é necessária durante a preparação.
    SPDLOG_INFO("ShipLua exp\xC3\xB4s {} assets sob '{}' ({} com alias direto, {} sem alias)", mOriginals.size(),
                mPrefix, mAliases.size(), mOriginals.size() - mAliases.size());
    return !mOriginals.empty();
}

bool ModAssetArchive::Close() {
    return mInner->Close();
}

std::shared_ptr<Ship::File> ModAssetArchive::LoadFile(const std::string& path) {
    if (!HasFile(path))
        return nullptr;
    return mInner->LoadFile(path.rfind(mPrefix, 0) == 0 ? path.substr(mPrefix.size()) : path);
}

std::shared_ptr<Ship::File> ModAssetArchive::LoadFile(uint64_t hash) {
    const auto files = ListFiles();
    const auto found = files->find(hash);
    return found == files->end() ? nullptr : LoadFile(found->second);
}

bool ModAssetArchive::WriteFile(const std::string&, const std::vector<uint8_t>&) {
    return false;
}
const Names& ModAssetArchive::OriginalFiles() const {
    return mOriginals;
}
const Hashes& ModAssetArchive::Aliases() const {
    return mAliases;
}
const std::string& ModAssetArchive::Prefix() const {
    return mPrefix;
}
} // namespace SohModAssets
