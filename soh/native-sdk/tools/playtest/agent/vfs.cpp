#include "vfs.h"
#include <cstring>
#include <stdexcept>

namespace Playtest {
VfsLayers::~VfsLayers() {
    if (hash_)
        BCryptDestroyHash(hash_);
    if (algorithm_)
        BCryptCloseAlgorithmProvider(algorithm_, 0);
}
bool VfsLayers::Init(const ShipOotResourcesV2* resources) {
    // Inicialização única, antes de Start e de qualquer callback de frame.
    if (algorithm_ || !resources || resources->size < sizeof(*resources) || !resources->read_file_layers ||
        !resources->has_file || !resources->list_files)
        return false;
    if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    DWORD bytes = 0, length = 0;
    if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &bytes,
                          0) < 0 ||
        bytes != sizeof(length) || !length)
        return false;
    hashObject_.resize(length);
    if (BCryptCreateHash(algorithm_, &hash_, hashObject_.data(), length, nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG) < 0)
        return false;
    resources_ = resources;
    return true;
}
uint64_t VfsLayers::Submit(const Json& args) {
    std::lock_guard lock(mutex_);
    if (pending_)
        throw std::runtime_error("busy: consulta VFS em andamento");
    // A validação também protege chamadas diretas nos testes.
    Request r{ "vfs", "vfs_layers", args };
    std::string error;
    if (!ValidateV2(r, error))
        throw std::runtime_error(error);
    if (++sequence_ >= VfsTicketBase)
        throw std::runtime_error("tickets VFS esgotados");
    pathCount_ = args["paths"].size();
    for (size_t i = 0; i < pathCount_; ++i) {
        paths_[i] = {};
        const auto& path = args["paths"][i].get_ref<const std::string&>();
        std::memcpy(paths_[i].text.data(), path.data(), path.size());
    }
    budget_ = args.value("max_bytes", VfsMaxBytes);
    layerCount_ = bytes_ = 0;
    ticket_ = VfsTicketBase + sequence_;
    pending_ = true;
    done_ = false;
    return ticket_;
}
ShipNativeStatus SHIP_NATIVE_CALL VfsLayers::Collect(void* user, const ShipOotResourceLayerV2* info,
                                                     const char* archive, const uint8_t* data) {
    auto& self = *static_cast<VfsLayers*>(user);
    auto& path = self.paths_[self.current_];
    if (!info || info->size < sizeof(*info) || !info->layer_count || info->layer_index != path.count ||
        info->layer_index >= info->layer_count || !archive || !info->archive_path_length || (info->data_size && !data))
        return SHIP_NATIVE_FAILURE;
    if (info->layer_count > VfsMaxLayers - path.first || self.layerCount_ >= VfsMaxLayers ||
        info->archive_path_length > VfsMaxArchive || info->data_size > self.budget_ - self.bytes_)
        return SHIP_NATIVE_LIMIT;
    if (path.count && info->layer_count != self.layers_[path.first].count)
        return SHIP_NATIVE_FAILURE;
    if (std::memchr(archive, 0, info->archive_path_length))
        return SHIP_NATIVE_FAILURE;
    auto& layer = self.layers_[self.layerCount_];
    // BCryptHashData não aceita nullptr para buffer vazio em todos os providers.
    if ((info->data_size && BCryptHashData(self.hash_, const_cast<PUCHAR>(data), info->data_size, 0) < 0) ||
        BCryptFinishHash(self.hash_, layer.digest.data(), static_cast<ULONG>(layer.digest.size()), 0) < 0) {
        self.resources_ = nullptr; // Não reutilizar um estado criptográfico possivelmente incompleto.
        return SHIP_NATIVE_FAILURE;
    }
    std::memcpy(layer.archive.data(), archive, info->archive_path_length);
    layer.archive[info->archive_path_length] = 0;
    layer.archiveLength = info->archive_path_length;
    layer.index = info->layer_index;
    layer.count = info->layer_count;
    layer.bytes = info->data_size;
    self.bytes_ += info->data_size;
    ++self.layerCount_;
    ++path.count;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL VfsLayers::Listed(void* user, const char*, uint32_t) {
    // Sem metacaractere, o glob do libultraship só casa o próprio caminho: qualquer entrada é presença.
    *static_cast<bool*>(user) = true;
    return SHIP_NATIVE_LIMIT;
}
bool VfsLayers::CertifyAbsent(const char* path) {
    // read_file_layers devolve UNSUPPORTED para "sem camada" e para "sem VFS". list_files só devolve OK com o
    // gerenciador de archives vivo, e has_file falso exclui o arquivo indexado que não carregou.
    if (std::strpbrk(path, "*?[\\"))
        return false;
    bool listed = false;
    return resources_->list_files(path, Listed, &listed) == SHIP_NATIVE_OK && !listed;
}
void VfsLayers::Tick(uint64_t frame) {
    std::lock_guard lock(mutex_);
    if (!pending_)
        return;
    frame_ = frame;
    for (current_ = 0; current_ < pathCount_; ++current_) {
        auto& path = paths_[current_];
        path.first = static_cast<uint32_t>(layerCount_);
        if (!resources_)
            continue;
        try {
            path.hasFile = resources_->has_file(path.text.data()) != 0;
            path.status = resources_->read_file_layers(path.text.data(), Collect, this);
            if (path.status == SHIP_NATIVE_OK && (!path.count || path.count != layers_[path.first].count))
                path.status = SHIP_NATIVE_FAILURE;
            path.absent = path.status == SHIP_NATIVE_UNSUPPORTED && !path.hasFile && !path.count &&
                          CertifyAbsent(path.text.data());
        } catch (...) {
            path.status = SHIP_NATIVE_FAILURE;
            path.absent = false;
        }
        // Retém o status, mas nunca publica um prefixo como se fosse a lista completa.
        if (path.status != SHIP_NATIVE_OK) {
            layerCount_ = path.first;
            path.count = 0;
        }
    }
    pending_ = false;
    done_ = true;
}
Json VfsLayers::Result(uint64_t ticket) const {
    std::lock_guard lock(mutex_);
    if (!ticket_ || ticket != ticket_)
        throw std::runtime_error("ticket VFS expirado");
    if (!done_)
        return { { "ticket", ticket }, { "status", "pending" } };
    Json observations = Json::array();
    bool complete = true;
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < pathCount_; ++i) {
        const auto& path = paths_[i];
        Json layers = Json::array();
        for (uint32_t j = 0; j < path.count; ++j) {
            const auto& layer = layers_[path.first + j];
            std::string digest(64, '0');
            for (size_t k = 0; k < layer.digest.size(); ++k) {
                digest[2 * k] = hex[layer.digest[k] >> 4];
                digest[2 * k + 1] = hex[layer.digest[k] & 15];
            }
            layers.push_back(
                { { "archive", std::string(layer.archive.data(), layer.archiveLength) }, { "sha256", digest } });
        }
        const bool pathComplete = path.status == SHIP_NATIVE_OK || path.absent;
        complete = complete && pathComplete;
        observations.push_back({ { "path", path.text.data() },
                                 { "layers", std::move(layers) },
                                 { "hostStatus", path.status },
                                 { "hasFile", path.hasFile },
                                 { "absent", path.absent },
                                 { "complete", pathComplete } });
    }
    Json body = { { "status", "observed" },
                  { "complete", complete },
                  { "frame", frame_ },
                  { "bytes_hashed", bytes_ },
                  { "observations", std::move(observations) },
                  { "instrumentation",
                    { { "observer", "linkspan.tool.playtest/vfs_layers" },
                      { "revision", PLAYTEST_VFS_REVISION },
                      { "agent_version", "0.2.2" },
                      { "layout_id", PLAYTEST_LAYOUT_ID },
                      { "resources_version", 2 },
                      { "layer_order", "low-to-high" } } } };
    return { { "ticket", ticket }, { "status", "done" }, { "result", std::move(body) } };
}
void VfsLayers::Cancel() {
    std::lock_guard lock(mutex_);
    pending_ = done_ = false;
    ticket_ = 0;
}
bool VfsLayers::Busy() const {
    std::lock_guard lock(mutex_);
    return pending_;
}
} // namespace Playtest
