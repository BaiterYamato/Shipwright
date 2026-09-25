// Montagem dos assets do NEI (NEI-007). Ver assets.h.
#include "assets.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "oot_resources.h"

namespace fs = std::filesystem;

namespace LinkSpanNei {
namespace {

constexpr char kFolder[] = "nei-assets";
constexpr char kPrefix[] = "nei-assets-";
constexpr char kCore[] = "core";

struct Mounted {
    std::string component;
    uint64_t handle = 0;
};

struct AssetsState {
    const ShipOotResourcesV2* resources = nullptr;
    std::vector<Mounted> mounted;
    std::string status = "ausentes";
    // Caminhos que o has_file já confirmou: o kaleido do fork pergunta pelo mesmo recurso a cada frame.
    std::unordered_set<std::string> exists;
};

AssetsState gAssets;

fs::path GameDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            break;
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
    return fs::current_path();
}

// "nei-assets-form.kafei.o2r" -> "form.kafei"; outro nome -> vazio.
std::string ComponentOf(const fs::path& file) {
    const std::string name = file.filename().string();
    const std::string suffix = ".o2r";
    if (name.size() <= sizeof(kPrefix) - 1 + suffix.size() || name.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return {};
    }
    return name.substr(sizeof(kPrefix) - 1, name.size() - (sizeof(kPrefix) - 1) - suffix.size());
}

} // namespace

void MountAssets(const ShipNativeRuntime* runtime) {
    UnmountAssets();
    gAssets.exists.clear();
    gAssets.resources = static_cast<const ShipOotResourcesV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
        sizeof(ShipOotResourcesV2)));
    if (!gAssets.resources || !gAssets.resources->mount_archive || !gAssets.resources->unmount_archive) {
        gAssets.status = "desligados: host sem linkspan.oot.resources v2";
        return;
    }
    const fs::path folder = GameDirectory() / kFolder;
    std::error_code error;
    std::vector<std::pair<std::string, fs::path>> found;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const std::string component = ComponentOf(it->path());
        if (!component.empty() && it->is_regular_file(error)) {
            found.emplace_back(component, it->path());
        }
    }
    // O núcleo primeiro; componentes opcionais sem ele não servem para nada (os itens vivem no núcleo).
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return (a.first == kCore) != (b.first == kCore) ? a.first == kCore : a.first < b.first;
    });
    if (found.empty() || found.front().first != kCore) {
        gAssets.status = "ausentes (" + (folder / (std::string(kPrefix) + kCore + ".o2r")).string() +
                         "); ícones e modelos provisórios";
        return;
    }
    std::string list;
    std::string failures;
    for (const auto& [component, path] : found) {
        uint64_t handle = 0;
        const ShipNativeStatus status = gAssets.resources->mount_archive(path.string().c_str(), &handle);
        if (status != SHIP_NATIVE_OK) {
            failures += (failures.empty() ? "" : ",") + component;
            if (component == kCore) {
                break;
            }
            continue;
        }
        gAssets.mounted.push_back({ component, handle });
        list += (list.empty() ? "" : "+") + component;
    }
    gAssets.status = list.empty() ? "montagem falhou (" + failures + ")"
                                  : list + (failures.empty() ? "" : " | falharam: " + failures);
}

void UnmountAssets() {
    if (gAssets.resources) {
        for (auto it = gAssets.mounted.rbegin(); it != gAssets.mounted.rend(); ++it) {
            gAssets.resources->unmount_archive(it->handle);
        }
    }
    gAssets.mounted.clear();
    gAssets.exists.clear();
    gAssets.status = "ausentes";
}

bool AssetComponentMounted(const char* component) {
    return std::any_of(gAssets.mounted.begin(), gAssets.mounted.end(),
                       [component](const Mounted& m) { return m.component == component; });
}

const std::string& AssetsStatus() {
    return gAssets.status;
}

} // namespace LinkSpanNei

// Para o código C do fork: 1 quando o núcleo dos assets está montado. Sem ele, desenhar um recurso do fork
// pediria ao resource manager um caminho que não existe.
extern "C" int NeiAssets_CoreMounted(void) {
    return LinkSpanNei::AssetComponentMounted(LinkSpanNei::kCore) ? 1 : 0;
}

extern "C" int NeiAssets_ComponentMounted(const char* component) {
    return component && LinkSpanNei::AssetComponentMounted(component) ? 1 : 0;
}

// Para o resource_guard.c: 1 quando o arquivo existe no VFS, contando os archives que o NEI montou. O
// ResourceMgr_FileExists do host lê um cache feito no boot e não vê o que foi montado depois. Sem o serviço de
// recursos, responde 1 e deixa o host decidir, como antes.
extern "C" int NeiAssets_HasFile(const char* path) {
    using LinkSpanNei::gAssets;
    if (!path) {
        return 0;
    }
    if (std::strncmp(path, "__OTR__", 7) == 0) {
        path += 7;
    }
    if (!gAssets.resources || !gAssets.resources->has_file) {
        return 1;
    }
    if (gAssets.exists.count(path)) {
        return 1;
    }
    // Só o "existe" fica guardado: fora da thread do jogo o has_file responde 0, e a ausência é barata de
    // perguntar de novo (busca de hash no ArchiveManager).
    if (!gAssets.resources->has_file(path)) {
        return 0;
    }
    gAssets.exists.emplace(path);
    return 1;
}
