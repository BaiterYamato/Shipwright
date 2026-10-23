#include "OotPrefixedArchive.h"

#include <ship/resource/File.h>
#include <ship/utils/StrHash64.h>
#include <spdlog/spdlog.h>
#include <unordered_set>
#include "zip.h" // O mesmo header de libzip incluído por ship/resource/archive/O2rArchive.h.

namespace ShipLuaHost {
namespace {

constexpr size_t kMaxTransformOutput = 64u * 1024u * 1024u;

ShipNativeStatus SHIP_NATIVE_CALL AppendBytes(void* writer, const char* bytes, uint32_t length) {
    if (!writer || (!bytes && length)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (length == 0) {
        return SHIP_NATIVE_OK;
    }
    auto& output = *static_cast<std::vector<char>*>(writer);
    if (length > kMaxTransformOutput - output.size()) {
        return SHIP_NATIVE_LIMIT;
    }
    try {
        output.insert(output.end(), bytes, bytes + length);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    return SHIP_NATIVE_OK;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

OotPrefixedArchive::OotPrefixedArchive(const OotPrefixedMountRequest& request, const std::string& archivePath,
                                       uint64_t serial, Ship::ArchiveManager* manager)
    : Ship::Archive(archivePath + "#linkspan-mount-" + std::to_string(serial)),
      mInner(std::make_shared<Ship::O2rArchive>(archivePath)), mManager(manager), mPrefix(request.prefix),
      mStrict(request.strict), mAliasRules(request.aliases), mTransform(request.transform),
      mTransformUser(request.transformUser) {
}

bool OotPrefixedArchive::Open() {
    Close();
    std::unique_lock<std::mutex> lock(mGate);
    mRefusal = SHIP_NATIVE_FAILURE;
    if (mRetired || !mManager) {
        return false;
    }
    try {
        std::vector<std::string> files;
        std::unordered_set<std::string> names;
        std::unordered_map<uint64_t, std::string> planned; // CRC64 → nome
        {
            // O índice do O2rArchive já perdeu duplicatas e colisões. Leia o ZIP original.
            std::unique_ptr<zip_t, decltype(&zip_close)> zip(zip_open(mInner->GetPath().c_str(), ZIP_RDONLY, nullptr),
                                                             &zip_close);
            if (!zip) {
                SPDLOG_WARN("Link-Span: '{}' não abriu para montar sob '{}'", mInner->GetPath(), mPrefix);
                return false;
            }
            const zip_int64_t count = zip_get_num_entries(zip.get(), 0);
            if (count < 0) {
                return false;
            }
            for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
                const char* name = zip_get_name(zip.get(), i, 0);
                if (!name || !*name) {
                    return false;
                }
                std::string inner(name);
                if (inner.back() == '/') {
                    continue;
                }
                if (files.size() == UINT32_MAX) {
                    mRefusal = SHIP_NATIVE_LIMIT;
                    return false;
                }
                std::string canonical = mPrefix + inner;
                const uint64_t id = CRC64(canonical.c_str());
                if (!names.emplace(inner).second || mManager->HashToString(id) != nullptr ||
                    !planned.emplace(id, canonical).second) {
                    SPDLOG_WARN("Link-Span: '{}' duplicado ou colide no VFS; '{}' não montou", canonical,
                                mInner->GetPath());
                    return false;
                }
                files.push_back(std::move(inner));
            }
        }
        if (files.empty()) {
            return false;
        }
        // O archive ainda não está no gerenciador: tudo o que ele acha é dos outros.
        if (!mManager->ListFiles(mPrefix + "*")->empty()) {
            SPDLOG_WARN("Link-Span: prefixo '{}' já ocupado no VFS; '{}' não montou", mPrefix, mInner->GetPath());
            return false;
        }
        std::unordered_map<std::string, std::string> aliasToInner;
        uint32_t aliasesAccepted = 0;
        uint32_t aliasesRejected = 0;
        for (const auto& [source, target] : mAliasRules) {
            std::vector<std::pair<std::string, std::string>> rule; // destino, caminho no archive
            std::unordered_set<uint64_t> ruleHashes;
            bool free = true;
            for (const auto& inner : files) {
                if (!StartsWith(inner, source)) {
                    continue;
                }
                std::string destination = target + inner.substr(source.size());
                const uint64_t id = CRC64(destination.c_str());
                if (mManager->HashToString(id) != nullptr || planned.count(id) || aliasToInner.count(destination) ||
                    !ruleHashes.emplace(id).second) {
                    free = false;
                    break;
                }
                rule.emplace_back(std::move(destination), inner);
            }
            if (!free || rule.empty()) {
                ++aliasesRejected;
                continue;
            }
            for (auto& [destination, inner] : rule) {
                planned.emplace(CRC64(destination.c_str()), destination);
                aliasToInner.emplace(std::move(destination), inner);
            }
            ++aliasesAccepted;
        }
        if (!mInner->Open()) {
            SPDLOG_WARN("Link-Span: '{}' não abriu para montar sob '{}'", mInner->GetPath(), mPrefix);
            mInner->Close();
            mInner->ListFiles()->clear();
            return false;
        }
        for (const auto& [id, name] : planned) {
            (void)id;
            IndexFile(name);
        }
        mAliasToInner = std::move(aliasToInner);
        mOwnIndex = ListFiles();
        mFileCount = static_cast<uint32_t>(files.size());
        mAliasesAccepted = aliasesAccepted;
        mAliasesRejected = aliasesRejected;
        SPDLOG_INFO("Link-Span montou {} arquivos de '{}' sob '{}' ({} aliases aceitos, {} recusados)", mFileCount,
                    mInner->GetPath(), mPrefix, mAliasesAccepted, mAliasesRejected);
        mRefusal = SHIP_NATIVE_OK;
        mAdmitting = !mRetired;
        return true;
    } catch (...) {
        ListFiles()->clear();
        mAliasToInner.clear();
        mOwnIndex.reset();
        mFileCount = mAliasesAccepted = mAliasesRejected = 0;
        mInner->ListFiles()->clear();
        mInner->Close();
        return false;
    }
}

bool OotPrefixedArchive::Close() {
    std::unique_lock<std::mutex> lock(mGate);
    mAdmitting = false;
    mDrained.wait(lock, [this] { return mInFlight == 0; });
    ListFiles()->clear(); // Archive::ListFiles() sem filtro devolve o mapa interno.
    mAliasToInner.clear();
    mOwnIndex.reset();
    mFileCount = mAliasesAccepted = mAliasesRejected = 0;
    mInner->ListFiles()->clear();
    return mInner->Close();
}

OotPrefixedArchive::LoadGuard::LoadGuard(OotPrefixedArchive& archive) {
    std::lock_guard<std::mutex> lock(archive.mGate);
    if (!archive.mAdmitting || archive.mRetired) {
        return;
    }
    ++archive.mInFlight;
    mArchive = &archive;
}

OotPrefixedArchive::LoadGuard::~LoadGuard() {
    if (!mArchive) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mArchive->mGate);
        --mArchive->mInFlight;
    }
    mArchive->mDrained.notify_all();
}

std::shared_ptr<Ship::File> OotPrefixedArchive::LoadFile(const std::string& filePath) {
    LoadGuard load(*this);
    if (!load) {
        return nullptr;
    }
    if (!HasFile(filePath)) {
        return nullptr;
    }
    std::string inner;
    const auto alias = mAliasToInner.find(filePath);
    if (alias != mAliasToInner.end()) {
        inner = alias->second;
    } else if (StartsWith(filePath, mPrefix)) {
        inner = filePath.substr(mPrefix.size());
    } else {
        return nullptr;
    }
    auto file = mInner->LoadFile(inner);
    if (!file || !file->Buffer || !mTransform) {
        return file;
    }
    std::vector<char> output;
    ShipNativeStatus status = SHIP_NATIVE_FAILURE;
    try {
        OotArchiveTransformScope transformScope;
        status = mTransform(mTransformUser, inner.c_str(), reinterpret_cast<const uint8_t*>(file->Buffer->data()),
                            file->Buffer->size(), AppendBytes, &output);
    } catch (...) { status = SHIP_NATIVE_FAILURE; }
    if (status != SHIP_NATIVE_OK) {
        SPDLOG_WARN("Link-Span: transform de '{}' devolveu {}; arquivo omitido", filePath, status);
        return nullptr;
    }
    try {
        file->Buffer = std::make_shared<std::vector<char>>(std::move(output));
    } catch (...) { return nullptr; }
    return file;
}

std::shared_ptr<Ship::File> OotPrefixedArchive::LoadFile(uint64_t hash) {
    LoadGuard load(*this);
    if (!load) {
        return nullptr;
    }
    if (!mOwnIndex) {
        return nullptr;
    }
    const auto found = mOwnIndex->find(hash);
    return found == mOwnIndex->end() ? nullptr : LoadFile(found->second);
}

ShipOotMountInfoV1 OotPrefixedArchive::Info() const {
    ShipOotMountInfoV1 info{};
    info.size = sizeof(info);
    info.file_count = mFileCount;
    info.aliases_accepted = mAliasesAccepted;
    info.aliases_rejected = mAliasesRejected;
    return info;
}

void OotPrefixedArchive::Retire() {
    std::unique_lock<std::mutex> lock(mGate);
    mRetired = true;
    mDrained.wait(lock, [this] { return mInFlight == 0; });
}

} // namespace ShipLuaHost
