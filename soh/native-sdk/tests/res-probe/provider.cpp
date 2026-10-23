#include <shiplua/native/ship_native_abi.h>
#include "oot_resources.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#ifndef LINKSPAN_OOT_RESOURCES_VERSION_4
#error "Compile depois de aplicar F2g: o SDK precisa de resources V4."
#endif

namespace {
constexpr char kOwner[] = "linkspan.test.res_probe";
constexpr char kArchive[] = "mods/res-probe/res-probe.o2r";
constexpr char kPrefix[] = "mod/res-probe/";
constexpr char kVanilla[] = "objects/gameplay_keep/gPlayerAnim_link_normal_wait";
constexpr char kCanonical[] = "mod/res-probe/textures/res_probe/a";
constexpr char kAlias[] = "textures/res_probe/a";
constexpr char kPrefixedVanilla[] = "mod/res-probe/objects/gameplay_keep/gPlayerAnim_link_normal_wait";
constexpr unsigned kQuietFrames = 20;
constexpr std::array<const char*, 8> kNames{ "vanilla", "mount",     "names",         "bytes",
                                             "list",    "duplicate", "unmount_quiet", "remount" };

enum class Stage { Fresh, Quiet, Complete };
struct Probe {
    const ShipOotResourcesV4* api = nullptr;
    std::atomic<unsigned long long> transforms{ 0 };
    std::array<int, 8> results{}; // 0 = não executado, 1 = passou, -1 = falhou.
    Stage stage = Stage::Fresh;
    uint64_t handle = 0;
    uint32_t vanillaSize = 0;
    unsigned frames = 0;
    unsigned long long quietBefore = 0;
    bool unmountNamesAbsent = false;
    std::string failure;
};

void Check(Probe& p, unsigned index, bool success) {
    p.results.at(index) = success ? 1 : -1;
}

ShipNativeStatus SHIP_NATIVE_CALL Transform(void* user, const char*, const uint8_t* data, size_t size,
                                            ShipNativeWriteFn write, void* writer) {
    auto& p = *static_cast<Probe*>(user);
    p.transforms.fetch_add(1, std::memory_order_relaxed);
    if (!write || (!data && size))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto status = write(writer, "T:", 2);
    // A fixture é pequena; recusar truncamento, nunca converter size_t silenciosamente.
    if (status != SHIP_NATIVE_OK || size > UINT32_MAX)
        return status == SHIP_NATIVE_OK ? SHIP_NATIVE_LIMIT : status;
    return write(writer, reinterpret_cast<const char*>(data), static_cast<uint32_t>(size));
}

ShipNativeStatus Mount(Probe& p, uint64_t& handle, ShipOotMountInfoV1& info) {
    const ShipOotMountAliasV1 aliases[] = { { sizeof(ShipOotMountAliasV1), 0, "textures/", "textures/" },
                                            { sizeof(ShipOotMountAliasV1), 0, "objects/", "objects/" } };
    ShipOotPrefixedArchiveSpecV1 spec{};
    spec.size = sizeof(spec);
    spec.owner = kOwner;
    spec.archive_path = kArchive;
    spec.prefix = kPrefix;
    spec.aliases = aliases;
    spec.alias_count = 2;
    spec.transform = Transform;
    spec.transform_user = &p;
    // STRICT/resolver interno pertence à F3; a F2 usa aliases.
    info = {};
    info.size = sizeof(info);
    handle = 0;
    return p.api->mount_archive_prefixed(&spec, &handle, &info);
}

bool Names(const Probe& p, bool expected) {
    return !!p.api->has_file(kCanonical) == expected && !!p.api->has_file(kAlias) == expected &&
           !!p.api->has_file(kPrefixedVanilla) == expected;
}

ShipNativeStatus SHIP_NATIVE_CALL List(void* user, const char* path, uint32_t length) {
    try {
        if (!path || !length)
            return SHIP_NATIVE_INVALID_ARGUMENT;
        static_cast<std::vector<std::string>*>(user)->emplace_back(path, length);
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

void Start(Probe& p) {
    if (p.stage != Stage::Fresh)
        return;
    const auto* api = p.api;
    Check(p, 0,
          api->has_file(kVanilla) && api->read_file(kVanilla, nullptr, 0, &p.vanillaSize) == SHIP_NATIVE_OK &&
              p.vanillaSize > 0);
    if (p.results[0] != 1) {
        p.failure = "vanilla ausente: usar runtime OoT isolado com assets completos";
        p.stage = Stage::Complete;
        return;
    }
    ShipOotMountInfoV1 info{};
    const auto mounted = Mount(p, p.handle, info);
    Check(p, 1,
          mounted == SHIP_NATIVE_OK && p.handle && info.file_count == 3 && info.aliases_accepted == 1 &&
              info.aliases_rejected == 1);
    if (mounted != SHIP_NATIVE_OK || !p.handle) {
        p.failure = "montagem falhou; não seguir com recursos ausentes";
        p.stage = Stage::Complete;
        return;
    }
    Check(p, 2, Names(p, true));
    uint8_t bytes[64]{};
    uint32_t count = 0, vanillaAfter = 0;
    constexpr char wanted[] = "T:LINKSPAN-RES-A";
    Check(p, 3,
          api->read_file(kAlias, bytes, sizeof(bytes), &count) == SHIP_NATIVE_OK && count == sizeof(wanted) - 1 &&
              std::memcmp(bytes, wanted, count) == 0 &&
              api->read_file(kVanilla, nullptr, 0, &vanillaAfter) == SHIP_NATIVE_OK && vanillaAfter == p.vanillaSize &&
              p.transforms.load() > 0);
    std::vector<std::string> paths;
    const auto listed = api->list_files("mod/res-probe/*", List, &paths);
    const std::array<std::string, 3> expected{ kCanonical, "mod/res-probe/textures/res_probe/b", kPrefixedVanilla };
    bool exact = listed == SHIP_NATIVE_OK && paths.size() == expected.size();
    for (const auto& path : expected) {
        unsigned occurrences = 0;
        for (const auto& actual : paths)
            occurrences += actual == path;
        exact = exact && occurrences == 1;
    }
    Check(p, 4, exact);
    uint64_t duplicate = UINT64_MAX;
    const auto refused = Mount(p, duplicate, info);
    Check(p, 5, refused == SHIP_NATIVE_FAILURE && duplicate == 0);
    // Não deixar uma montagem inesperadamente aceita fora do cleanup.
    if (duplicate && duplicate != UINT64_MAX && duplicate != p.handle)
        api->unmount_archive(duplicate);
    const auto unmounted = api->unmount_archive(p.handle);
    if (unmounted != SHIP_NATIVE_OK) {
        Check(p, 6, false);
        p.failure = "unmount falhou; montagem deixada para cleanup do owner";
        p.stage = Stage::Complete;
        return;
    }
    p.handle = 0;
    p.unmountNamesAbsent = Names(p, false);
    p.quietBefore = p.transforms.load();
    p.frames = 0;
    p.stage = Stage::Quiet;
}

void Poll(Probe& p) {
    if (p.stage != Stage::Quiet || ++p.frames < kQuietFrames)
        return;
    Check(p, 6, p.unmountNamesAbsent && Names(p, false) && p.transforms.load() == p.quietBefore);
    ShipOotMountInfoV1 info{};
    const auto remounted = Mount(p, p.handle, info);
    Check(p, 7,
          remounted == SHIP_NATIVE_OK && p.handle && info.file_count == 3 && info.aliases_accepted == 1 &&
              info.aliases_rejected == 1);
    // Intencional: deixar montado. onProviderUnload do HOST deve drenar/desmontar
    // antes de Shutdown liberar transform_user. Shutdown não mascara falha do host.
    p.stage = Stage::Complete;
}

std::string Report(const Probe& p) {
    const char* stage = p.stage == Stage::Fresh ? "fresh" : p.stage == Stage::Quiet ? "quiet" : "complete";
    std::string result = std::string("res-probe report stage=") + stage + "\n";
    unsigned passed = 0;
    for (unsigned i = 0; i < p.results.size(); ++i) {
        passed += p.results[i] == 1;
        result += "res-probe check=" + std::to_string(i + 1) + " name=" + kNames[i] + " status=" +
                  (p.results[i] == 1    ? "ok"
                   : p.results[i] == -1 ? "fail"
                                        : "not_run") +
                  "\n";
    }
    result += "res-probe quiet_frames=" + std::to_string(p.frames) +
              " transform_calls=" + std::to_string(p.transforms.load()) + " left_handle=" + std::to_string(p.handle) +
              "\n";
    if (!p.failure.empty())
        result += "res-probe reason=" + p.failure + "\n";
    if (p.stage == Stage::Complete)
        result += std::string("res-probe ") + (passed == 8 ? "ok " : "fail ") + std::to_string(passed) + "/8";
    return result;
}

template <unsigned Operation>
ShipNativeStatus SHIP_NATIVE_CALL Command(void* user, const char*, uint32_t, ShipNativeWriteFn write, void* writer) {
    if (!user || !write)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& p = *static_cast<Probe*>(user);
    try {
        if constexpr (Operation == 1)
            Start(p);
        if constexpr (Operation == 2)
            Poll(p);
        const auto report = Report(p);
        return write(writer, report.data(), static_cast<uint32_t>(report.size()));
    } catch (...) {
        p.stage = Stage::Complete;
        p.failure = "excecao contida no probe; nenhum resultado positivo inferido";
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    if (output)
        *output = nullptr;
    if (!runtime || !output || runtime->size < sizeof(*runtime) || runtime->abi_major != 1 || runtime->abi_minor < 4 ||
        !runtime->get_service || !runtime->register_function)
        return SHIP_NATIVE_UNSUPPORTED;
    auto* p = new (std::nothrow) Probe;
    if (!p)
        return SHIP_NATIVE_FAILURE;
    *output = p; // rollback também recebe a instância.
    p->api = static_cast<const ShipOotResourcesV4*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_4,
                             sizeof(ShipOotResourcesV4)));
    if (!p->api || p->api->size < sizeof(ShipOotResourcesV4) || !p->api->has_file || !p->api->read_file ||
        !p->api->list_files || !p->api->mount_archive_prefixed || !p->api->unmount_archive)
        return SHIP_NATIVE_UNSUPPORTED;
    auto status = runtime->register_function(runtime->context, "start", Command<1>, p);
    if (status == SHIP_NATIVE_OK)
        status = runtime->register_function(runtime->context, "poll", Command<2>, p);
    if (status == SHIP_NATIVE_OK)
        status = runtime->register_function(runtime->context, "report", Command<0>, p);
    return status;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* p = static_cast<Probe*>(instance);
    if (!p)
        return;
    // Vida da DLL/callback sob concorrência requer prova instrumentada do host.
    std::fprintf(stderr, "res-probe shutdown left_handle=%llu transform_calls=%llu\n",
                 static_cast<unsigned long long>(p->handle), p->transforms.load());
    delete p;
}
} // namespace

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{ sizeof(descriptor), 1, 4, Init, Shutdown };
    return &descriptor;
}
