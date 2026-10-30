// Testes sem jogo da política de montagem de camadas (HOST-SLIM-001 F4).
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "layer_archives.h"
#include "unbound_docs.h"

namespace {
namespace fs = std::filesystem;
using namespace LinkSpanUnbound;
int gFailures = 0;
int gCases = 0;
#define CHECK(expr)                                                                 \
    do {                                                                            \
        if (!(expr)) {                                                              \
            std::fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #expr); \
            ++gFailures;                                                            \
        }                                                                           \
    } while (0)
const std::string kValid = R"({"format":"unbound","formatVersion":2})";

struct FakeResources {
    struct Layer {
        std::string archive;
        std::string json;
        uint32_t dataSize = 0; // tamanho declarado no lugar do real, para camada alheia acima dos limites
    };
    static FakeResources* active;
    ShipOotResourcesV2 service{};
    std::map<std::string, std::vector<Layer>> files;
    std::vector<std::string> reads;
    std::vector<std::string> mounts;
    std::vector<uint64_t> unmounts;
    std::vector<size_t> capacities;
    const std::vector<uint64_t>* tracked = nullptr;
    ShipNativeStatus mountStatus = SHIP_NATIVE_OK;
    ShipNativeStatus readStatus = SHIP_NATIVE_OK;
    uint64_t nextHandle = 101;
    uint32_t layerCount = 0;
    uint32_t dataSize = 0;
    bool badLayerSize = false;
    bool badLayerIndex = false;
    bool nullData = false;
    fs::path directory;
    std::vector<fs::path> identities;

    FakeResources() {
        active = this;
        service.size = sizeof(service);
        service.read_file_layers = Read;
        service.mount_archive = Mount;
        service.unmount_archive = Unmount;
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned i = 0;; ++i) {
            directory = fs::current_path() / ("archives-test-" + std::to_string(seed) + "-" + std::to_string(i));
            if (fs::create_directory(directory))
                break;
        }
    }
    ~FakeResources() {
        // Só removemos os arquivos vazios criados por esta instância, sem remoção recursiva.
        std::error_code error;
        for (const auto& path : identities)
            fs::remove(path, error);
        fs::remove(directory, error);
        active = nullptr;
    }
    std::string Archive(const std::string& name) {
        const auto path = directory / name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("não criou identidade do archive");
        identities.push_back(path);
        return path.generic_string();
    }
    void Private(const std::string& prefix, const std::string& path, const std::string& json = kValid,
                 uint32_t dataSize = 0) {
        files[prefix + "unbound.json"].push_back({ path, json, dataSize });
    }
    static ShipNativeStatus SHIP_NATIVE_CALL Read(const char* path, ShipOotResourceLayerFn callback, void* user) {
        auto& self = *active;
        self.reads.emplace_back(path);
        if (self.readStatus != SHIP_NATIVE_OK)
            return self.readStatus;
        const auto found = self.files.find(path);
        if (found == self.files.end())
            return SHIP_NATIVE_FAILURE;
        for (size_t i = 0; i < found->second.size(); ++i) {
            const auto& layer = found->second[i];
            ShipOotResourceLayerV2 metadata{};
            metadata.size = self.badLayerSize ? sizeof(metadata) - 1 : sizeof(metadata);
            metadata.layer_index = self.badLayerIndex ? 1 : static_cast<uint32_t>(i);
            metadata.layer_count = self.layerCount ? self.layerCount : static_cast<uint32_t>(found->second.size());
            metadata.data_size = self.dataSize    ? self.dataSize
                                 : layer.dataSize ? layer.dataSize
                                                  : static_cast<uint32_t>(layer.json.size());
            metadata.archive_path_length = static_cast<uint32_t>(layer.archive.size());
            const auto status = callback(user, &metadata, layer.archive.c_str(),
                                         self.nullData ? nullptr : reinterpret_cast<const uint8_t*>(layer.json.data()));
            if (status != SHIP_NATIVE_OK)
                return status;
        }
        return SHIP_NATIVE_OK;
    }
    static ShipNativeStatus SHIP_NATIVE_CALL Mount(const char* path, uint64_t* handle) {
        auto& self = *active;
        self.mounts.emplace_back(path);
        if (self.tracked)
            self.capacities.push_back(self.tracked->capacity());
        *handle = self.nextHandle++; // até erro escreve um valor: ele não pode entrar no Shutdown.
        return self.mountStatus;
    }
    static ShipNativeStatus SHIP_NATIVE_CALL Unmount(uint64_t handle) {
        active->unmounts.push_back(handle);
        return SHIP_NATIVE_OK;
    }
};
FakeResources* FakeResources::active = nullptr;

ShipOotModArchiveV1 Entry(const std::string& path, const char* prefix, uint32_t order = 0) {
    return { sizeof(ShipOotModArchiveV1), order, path.c_str(), "", prefix };
}
struct Run {
    std::vector<uint64_t> handles;
    std::vector<std::string> log;
    LayerMountSummary summary;
    ShipNativeStatus Mount(FakeResources& fake, const ShipOotModArchiveV1* entries, uint32_t count,
                           uint32_t stride = sizeof(ShipOotModArchiveV1),
                           uint32_t size = sizeof(ShipOotModArchivesHookV1)) {
        const ShipOotModArchivesHookV1 payload{ size, entries, count, stride };
        fake.tracked = &handles;
        return MountLayerArchives(fake.service, payload, handles, log, summary);
    }
};

void ValidLoose() {
    ++gCases;
    FakeResources fake;
    Run run;
    const auto path = fake.Archive("loose.o2r");
    fake.Private("mod/loose/", path);
    auto entry = Entry(path, "mod/loose/");
    CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
    CHECK(fake.mounts == std::vector<std::string>{ path });
    CHECK(run.handles == std::vector<uint64_t>{ 101 });
    CHECK(run.summary.Report() == "montadas=1 ja_na_raiz=0 falhas=0");
    CHECK(run.log ==
          (std::vector<std::string>{ "camada montada na raiz: " + path, "camadas: montadas=1 ja_na_raiz=0 falhas=0" }));
    CHECK(fake.capacities.size() == 1 && fake.capacities[0] >= 1);
}
void IgnoredExtensionAndPrefix() {
    for (const auto* name : { "ignored.otr", "ignored.O2R", "empty.o2r" }) {
        ++gCases;
        FakeResources fake;
        Run run;
        const auto path = fake.Archive(name);
        fake.Private("mod/ignored/", path);
        auto entry = Entry(path, std::string(name) == "empty.o2r" ? "" : "mod/ignored/");
        CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
        CHECK(fake.mounts.empty() && run.handles.empty());
        CHECK(fake.reads == std::vector<std::string>{ "unbound.json" });
        CHECK(run.summary.Report() == "montadas=0 ja_na_raiz=0 falhas=0");
    }
}
void InvalidManifests() {
    for (const auto& json : std::vector<std::string>{
             "{quebrado", "[]", "{}", R"({"format":"unbound"})", R"({"format":"other","formatVersion":2})",
             R"({"format":"unbound","formatVersion":1})", "/*comentário*/" + kValid,
             kValid + std::string(kMaxLayerManifestBytes, ' '),
             R"({"format":"unbound","formatVersion":2,"deep":)" + std::string(65, '[') + "0" + std::string(65, ']') +
                 "}" }) {
        ++gCases;
        FakeResources fake;
        Run run;
        const auto path = fake.Archive("invalid.o2r");
        fake.Private("mod/invalid/", path, json);
        auto entry = Entry(path, "mod/invalid/");
        CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
        CHECK(fake.mounts.empty() && run.handles.empty() && run.summary.failed == 0);
    }
}
void SameStem() {
    ++gCases;
    FakeResources fake;
    Run run;
    // Mesmo stem em diretórios distintos, compartilhando o prefixo privado.
    const auto a = fake.Archive("same.o2r");
    const auto otherDir = fake.directory / "other";
    fs::create_directory(otherDir);
    const auto b = (otherDir / "same.o2r").generic_string();
    { std::ofstream identity(b); }
    fake.identities.push_back(b);
    // A ordem dos callbacks deliberadamente traz primeiro o manifesto inválido.
    fake.Private("mod/same/", b, "{}");
    fake.Private("mod/same/", a);
    ShipOotModArchiveV1 entries[]{ Entry(b, "mod/same/"), Entry(a, "mod/same/") };
    CHECK(run.Mount(fake, entries, 2) == SHIP_NATIVE_OK);
    CHECK(fake.mounts == std::vector<std::string>{ a });
    CHECK(run.summary.mounted == 1 && run.summary.failed == 0);
    // Desfaz a pasta depois dos arquivos sem remoção recursiva.
    fake.identities.push_back(otherDir);
}
void AlreadyAtRoot() {
    ++gCases;
    FakeResources fake;
    Run run;
    const auto path = fake.Archive("root.o2r");
    fake.Private("mod/root/", path);
    const auto alias = (fake.directory / "." / "root.o2r").generic_string();
    fake.files["unbound.json"].push_back({ alias, kValid });
    auto entry = Entry(path, "mod/root/");
    CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
    CHECK(fake.mounts.empty() && run.handles.empty());
    CHECK(run.summary.Report() == "montadas=0 ja_na_raiz=1 falhas=0");
    CHECK(run.log ==
          (std::vector<std::string>{ "camada já na raiz: " + path, "camadas: montadas=0 ja_na_raiz=1 falhas=0" }));
}
void MountFailure() {
    for (const auto& [status, label] :
         std::vector<std::pair<ShipNativeStatus, std::string>>{ { SHIP_NATIVE_FAILURE, "failure" },
                                                                { SHIP_NATIVE_INVALID_ARGUMENT, "invalid" },
                                                                { SHIP_NATIVE_UNSUPPORTED, "unsupported" },
                                                                { SHIP_NATIVE_LIMIT, "limit" } }) {
        ++gCases;
        FakeResources fake;
        Run run;
        fake.mountStatus = status;
        const auto path = fake.Archive("fail.o2r");
        fake.Private("mod/fail/", path);
        auto entry = Entry(path, "mod/fail/");
        CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
        CHECK(fake.mounts == std::vector<std::string>{ path });
        CHECK(run.handles.empty());
        CHECK(run.summary.Report() == "montadas=0 ja_na_raiz=0 falhas=1");
        CHECK(run.log == (std::vector<std::string>{ "camada não montada (" + label + "): " + path,
                                                    "camadas: montadas=0 ja_na_raiz=0 falhas=1" }));
        UnmountLayerArchives(fake.service, run.handles);
        CHECK(fake.unmounts.empty());
    }
}
void PayloadValidation() {
    ++gCases;
    FakeResources fake;
    Run run;
    const auto path = fake.Archive("payload.o2r");
    auto entry = Entry(path, "mod/payload/");
    CHECK(run.Mount(fake, &entry, 1, sizeof(entry), sizeof(ShipOotModArchivesHookV1) - 1) == SHIP_NATIVE_UNSUPPORTED);
    CHECK(run.Mount(fake, &entry, 1, sizeof(entry) - 1) == SHIP_NATIVE_UNSUPPORTED);
    CHECK(run.Mount(fake, nullptr, 1) == SHIP_NATIVE_UNSUPPORTED);
    CHECK(fake.reads.empty() && fake.mounts.empty() && run.log.empty() && run.handles.empty());
    CHECK(run.Mount(fake, nullptr, 0, 0) == SHIP_NATIVE_OK);
    CHECK(run.summary.Report() == "montadas=0 ja_na_raiz=0 falhas=0");
}
void EntryValidation() {
    for (int field = 0; field < 3; ++field) {
        ++gCases;
        FakeResources fake;
        Run run;
        const auto path = fake.Archive("entry.o2r");
        fake.Private("mod/entry/", path);
        auto entry = Entry(path, "mod/entry/");
        if (field == 0)
            --entry.size;
        if (field == 1)
            entry.path = nullptr;
        if (field == 2)
            entry.private_prefix = nullptr;
        CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
        CHECK(fake.mounts.empty() && run.handles.empty());
    }
}
void LargerStrideAndOrder() {
    ++gCases;
    FakeResources fake;
    Run run;
    const auto a = fake.Archive("z.o2r");
    const auto b = fake.Archive("a.o2r");
    fake.Private("mod/z/", a);
    fake.Private("mod/a/", b);
    struct Extended {
        ShipOotModArchiveV1 entry;
        uint64_t future[3];
    };
    const Extended entries[]{ { Entry(a, "mod/z/", 99), { 11, 12, 13 } }, { Entry(b, "mod/a/", 0), { 21, 22, 23 } } };
    run.handles.push_back(77); // chamadas repetidas preservam os handles anteriores.
    CHECK(run.Mount(fake, &entries[0].entry, 2, sizeof(Extended)) == SHIP_NATIVE_OK);
    CHECK(fake.mounts == (std::vector<std::string>{ a, b }));
    CHECK(run.handles == (std::vector<uint64_t>{ 77, 101, 102 }));
    CHECK(run.summary.mounted == 2);
    CHECK(fake.capacities.size() == 2 && fake.capacities[0] >= 3 && fake.capacities[1] >= 3);
    UnmountLayerArchives(fake.service, run.handles);
    CHECK(fake.unmounts == (std::vector<uint64_t>{ 102, 101, 77 }));
    CHECK(run.handles.empty());
    UnmountLayerArchives(fake.service, run.handles);
    CHECK(fake.unmounts.size() == 3);
}
void ReadValidation() {
    for (int mode = 0; mode < 8; ++mode) {
        ++gCases;
        FakeResources fake;
        Run run;
        const auto path = fake.Archive("read.o2r");
        if (mode != 0)
            fake.Private("mod/read/", path);
        if (mode == 1)
            fake.readStatus = SHIP_NATIVE_FAILURE;
        if (mode == 2)
            fake.badLayerSize = true;
        if (mode == 3)
            fake.badLayerIndex = true;
        if (mode == 4)
            fake.nullData = true;
        if (mode == 5)
            fake.layerCount = 1025;
        if (mode == 6)
            fake.dataSize = 64 * 1024 * 1024 + 1;
        if (mode == 7) {
            fake.files.clear();
            const auto other = fake.Archive("other.o2r");
            fake.Private("mod/read/", other); // nenhuma camada vem do próprio archive.
        }
        auto entry = Entry(path, "mod/read/");
        CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
        // Índice e contagem de camadas não importam: só a camada do próprio archive é lida (revisão 2, N3).
        if (mode == 3 || mode == 5) {
            CHECK(fake.mounts == (std::vector<std::string>{ path }) && run.handles.size() == 1);
        } else {
            CHECK(fake.mounts.empty() && run.handles.empty());
        }
    }
}
void OtherOriginBeyondLimits() {
    ++gCases;
    FakeResources fake;
    Run run;
    const auto own = fake.Archive("same.o2r");
    const auto other = fake.Archive("other-same.o2r");
    // Archive de mesmo stem com unbound.json acima de qualquer limite vem antes: não pode derrubar a leitura do outro.
    fake.Private("mod/same/", other, "{}", 64 * 1024 * 1024 + 1);
    fake.Private("mod/same/", own);
    auto entry = Entry(own, "mod/same/");
    CHECK(run.Mount(fake, &entry, 1) == SHIP_NATIVE_OK);
    CHECK(fake.mounts == (std::vector<std::string>{ own }));
    CHECK(run.summary.mounted == 1);
}
} // namespace

int main() {
    try {
        ValidLoose();
        IgnoredExtensionAndPrefix();
        InvalidManifests();
        SameStem();
        AlreadyAtRoot();
        MountFailure();
        PayloadValidation();
        EntryValidation();
        LargerStrideAndOrder();
        ReadValidation();
        OtherOriginBeyondLimits();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "exceção no teste: %s\n", error.what());
        ++gFailures;
    }
    std::printf("archives_tests: %d casos, %d falhas\n", gCases, gFailures);
    return gFailures ? 1 : 0;
}
