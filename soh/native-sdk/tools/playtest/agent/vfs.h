#pragma once
#include "protocol.h"
#include "oot_resources.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <mutex>

namespace Playtest {
// JSON e alocações do observador ficam na thread TCP/Init. Tick só usa buffers fixos.
// A implementação do host carrega todas as camadas antes dos callbacks e pode alocar.
class VfsLayers {
  public:
    ~VfsLayers();
    bool Init(const ShipOotResourcesV2* resources);
    uint64_t Submit(const Json& args);
    Json Result(uint64_t ticket) const;
    void Tick(uint64_t frame);
    void Cancel();
    bool Busy() const;

  private:
    struct Path {
        std::array<char, VfsMaxPath + 1> text{};
        ShipNativeStatus status = SHIP_NATIVE_UNSUPPORTED;
        uint32_t first = 0, count = 0;
        bool hasFile = false;
        bool absent = false; // ausência certificada: VFS respondeu e nenhum archive indexa o caminho
    };
    struct Layer {
        std::array<char, VfsMaxArchive + 1> archive{};
        uint32_t archiveLength = 0, index = 0, count = 0, bytes = 0;
        std::array<uint8_t, 32> digest{};
    };
    static ShipNativeStatus SHIP_NATIVE_CALL Collect(void*, const ShipOotResourceLayerV2*, const char*, const uint8_t*);
    static ShipNativeStatus SHIP_NATIVE_CALL Listed(void*, const char*, uint32_t);
    bool CertifyAbsent(const char* path);
    mutable std::mutex mutex_;
    const ShipOotResourcesV2* resources_ = nullptr; // tabela estática do serviço, não dados de callback
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<uint8_t> hashObject_; // dimensionado apenas em Init
    std::array<Path, VfsMaxPaths> paths_{};
    std::array<Layer, VfsMaxLayers> layers_{};
    size_t pathCount_ = 0, layerCount_ = 0, current_ = 0;
    uint32_t budget_ = VfsMaxBytes, bytes_ = 0;
    uint64_t sequence_ = 0, ticket_ = 0, frame_ = 0;
    bool pending_ = false, done_ = false;
};
} // namespace Playtest
