// Testa o provider verdadeiro contra uma tabela falsa. Não implementa nem prova
// OotPrefixedArchive, ResourceManager, descoberta, cache do LUS ou dreno real.
#include "provider.cpp"
#include <cstdlib>
#include <iostream>
#include <map>

namespace Mock {
enum class Fault { None, NoVanilla, Counts, Bytes, List, Duplicate, Stale };
struct MountRecord {
    ShipOotArchiveTransformFn transform;
    void* user;
};
std::map<uint64_t, MountRecord> live;
uint64_t nextHandle = 1;
Fault fault = Fault::None;
ShipOotResourcesV4 api{};
int errors = 0;
constexpr char baseline[] = "VANILLA-SINTETICO";

void Require(bool condition, const char* message) {
    if (!condition) {
        ++errors;
        std::cerr << "FALHOU: " << message << "\n";
    }
}

uint8_t SHIP_NATIVE_CALL Has(const char* path) {
    if (std::strcmp(path, kVanilla) == 0)
        return fault != Fault::NoVanilla;
    const bool mounted = !live.empty() || fault == Fault::Stale;
    return mounted && (std::strcmp(path, kCanonical) == 0 || std::strcmp(path, kAlias) == 0 ||
                       std::strcmp(path, kPrefixedVanilla) == 0);
}
ShipNativeStatus SHIP_NATIVE_CALL Write(void* user, const char* data, uint32_t length) {
    static_cast<std::string*>(user)->append(data, length);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Read(const char* path, uint8_t* output, uint32_t capacity, uint32_t* count) {
    if (!Has(path))
        return SHIP_NATIVE_FAILURE;
    std::string value;
    if (std::strcmp(path, kVanilla) == 0) {
        value = baseline;
    } else {
        if (live.empty())
            return SHIP_NATIVE_FAILURE;
        const auto record = live.begin()->second;
        const char payload[] = "LINKSPAN-RES-A";
        const auto status =
            record.transform(record.user, "textures/res_probe/a", reinterpret_cast<const uint8_t*>(payload),
                             sizeof(payload) - 1, Write, &value);
        if (status != SHIP_NATIVE_OK)
            return status;
        if (fault == Fault::Bytes)
            value = "ERRADO";
    }
    *count = static_cast<uint32_t>(value.size());
    if (!output && !capacity)
        return SHIP_NATIVE_OK;
    if (capacity < value.size())
        return SHIP_NATIVE_LIMIT;
    std::memcpy(output, value.data(), value.size());
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Enumerate(const char*, ShipOotResourcePathFn callback, void* user) {
    const char* paths[] = { kCanonical, "mod/res-probe/textures/res_probe/b", kPrefixedVanilla };
    if (fault == Fault::List)
        paths[1] = "mod/res-probe/arquivo-inesperado";
    for (const auto* path : paths) {
        auto status = callback(user, path, static_cast<uint32_t>(std::strlen(path)));
        if (status != SHIP_NATIVE_OK)
            return status;
    }
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL MountFake(const ShipOotPrefixedArchiveSpecV1* spec, uint64_t* handle,
                                            ShipOotMountInfoV1* info) {
    *handle = 0;
    Require(std::strcmp(spec->owner, kOwner) == 0 && std::strcmp(spec->prefix, kPrefix) == 0 &&
                std::strcmp(spec->archive_path, kArchive) == 0 && spec->flags == 0 && spec->alias_count == 2,
            "probe entrega owner, caminho, prefixo e dois aliases do plano");
    Require(spec->reserved_align == 0 && spec->reserved_flags == 0 && spec->reserved_aliases == 0 && spec->transform &&
                spec->transform_user,
            "spec tem reservas zero e transform");
    if (!live.empty() && fault != Fault::Duplicate)
        return SHIP_NATIVE_FAILURE;
    *handle = nextHandle++;
    live[*handle] = { spec->transform, spec->transform_user };
    info->file_count = fault == Fault::Counts ? 4 : 3;
    info->aliases_accepted = info->aliases_rejected = 1;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Unmount(uint64_t handle) {
    return live.erase(handle) ? SHIP_NATIVE_OK : SHIP_NATIVE_INVALID_ARGUMENT;
}
void Reset(Fault next) {
    live.clear();
    nextHandle = 1;
    fault = next;
    api = {};
    api.size = sizeof(api);
    api.has_file = Has;
    api.read_file = Read;
    api.list_files = Enumerate;
    api.mount_archive_prefixed = MountFake;
    api.unmount_archive = Unmount;
}
void Scenario(Fault next, unsigned failureIndex = 99, bool lateCallback = false) {
    Reset(next);
    Probe p;
    p.api = &api;
    Start(p);
    if (next != Fault::NoVanilla)
        Require(p.stage == Stage::Quiet, "probe espera frames depois de unmount");
    if (lateCallback)
        p.transforms.fetch_add(1); // simula chamada tardia; não prova concorrência.
    for (unsigned i = 0; i < kQuietFrames - 1; ++i)
        Poll(p);
    if (next != Fault::NoVanilla)
        Require(p.stage == Stage::Quiet, "não declarar sucesso antes da janela de silêncio");
    Poll(p);
    Require(p.stage == Stage::Complete, "probe termina");
    if (failureIndex == 99 && !lateCallback) {
        for (int result : p.results)
            Require(result == 1, "happy path passa oito checks");
        Require(Report(p).find("res-probe ok 8/8") != std::string::npos, "relatório completo passa");
    } else {
        const unsigned index = lateCallback ? 6 : failureIndex;
        Require(p.results.at(index) == -1, "falha injetada reprova check correspondente");
        Require(Report(p).find("res-probe ok 8/8") == std::string::npos, "falha não vira verde");
    }
    if (next != Fault::NoVanilla)
        Require(p.handle && live.size() == 1, "remontagem fica para cleanup por owner");
    if (p.handle) {
        Require(Unmount(p.handle) == SHIP_NATIVE_OK && live.empty(),
                "cleanup do host falso remove montagem restante antes de destruir Probe");
    }
}
} // namespace Mock

int main() {
    using namespace Mock;
    Scenario(Fault::None);
    Scenario(Fault::NoVanilla, 0);
    Scenario(Fault::Counts, 1);
    Scenario(Fault::Bytes, 3);
    Scenario(Fault::List, 4);
    Scenario(Fault::Duplicate, 5);
    Scenario(Fault::Stale, 6);
    Scenario(Fault::None, 99, true);
    if (errors)
        return EXIT_FAILURE;
    std::cout << "res-probe offline: 8 cenarios ok (mock; sem prova do host)\n";
    return EXIT_SUCCESS;
}
