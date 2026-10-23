#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/resource/archive/O2rArchive.h>

#include "OotNativeEngine.h"

namespace ShipLuaHost {

// HOST-SLIM-001 (RFC 0029): .o2r montado sob um prefixo para linkspan.oot.resources V4. Generaliza o
// MmCrossWorldArchive do bootstrap: nomes canônicos prefix + caminho, aliases só onde o destino está livre e transform
// opcional do dono. O caminho do Archive é único por montagem: o mesmo .o2r pode entrar sob dois prefixos.
class OotPrefixedArchive final : public Ship::Archive {
  public:
    OotPrefixedArchive(const OotPrefixedMountRequest& request, const std::string& archivePath, uint64_t serial,
                       Ship::ArchiveManager* manager);

    bool Open() override;
    bool Close() override;
    std::shared_ptr<Ship::File> LoadFile(const std::string& filePath) override;
    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override;
    bool WriteFile(const std::string&, const std::vector<uint8_t>&) override {
        return false;
    }

    // Por que o Open recusou: FAILURE (archive ilegível ou colisão) ou LIMIT (mais de 2^32 arquivos).
    ShipNativeStatus Refusal() const {
        return mRefusal;
    }
    ShipOotMountInfoV1 Info() const;
    bool Strict() const {
        return mStrict;
    }
    // Para as leituras: chamadas novas devolvem nullptr e as em curso terminam antes do retorno.
    void Retire();

  private:
    class LoadGuard {
      public:
        explicit LoadGuard(OotPrefixedArchive& archive);
        ~LoadGuard();
        LoadGuard(const LoadGuard&) = delete;
        LoadGuard& operator=(const LoadGuard&) = delete;
        explicit operator bool() const {
            return mArchive != nullptr;
        }

      private:
        OotPrefixedArchive* mArchive = nullptr;
    };

    std::shared_ptr<Ship::O2rArchive> mInner;
    Ship::ArchiveManager* mManager = nullptr;
    std::string mPrefix;
    bool mStrict = false;
    std::vector<std::pair<std::string, std::string>> mAliasRules;
    ShipOotArchiveTransformFn mTransform = nullptr;
    void* mTransformUser = nullptr;
    std::unordered_map<std::string, std::string> mAliasToInner; // destino do alias → caminho no archive
    std::shared_ptr<std::unordered_map<uint64_t, std::string>> mOwnIndex;
    ShipNativeStatus mRefusal = SHIP_NATIVE_OK;
    uint32_t mFileCount = 0;
    uint32_t mAliasesAccepted = 0;
    uint32_t mAliasesRejected = 0;
    std::mutex mGate;
    std::condition_variable mDrained;
    size_t mInFlight = 0;
    bool mAdmitting = false;
    bool mRetired = false;
};

} // namespace ShipLuaHost
