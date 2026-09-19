// Liga linkspan.oot.resources v3 (OotNativeJsonTypes.cpp) ao ResourceLoader: uma fábrica JSON proxy por
// tipo de mod pede o XML ao transcodificador e o entrega às fábricas XML do host.
#include "OotNativeJsonTypes.h"

#include <map>
#include <memory>
#include <vector>

#include <ship/Context.h>
#include <ship/resource/ResourceFactoryJson.h>
#include <ship/resource/ResourceLoader.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

namespace {

// Ids de tipo dos mods: fora dos fourcc do SoH e da libultraship ("LS" + contador).
constexpr uint32_t kFirstTypeId = 0x4C530100;

std::map<uint32_t, std::string>& TypeNames() {
    static std::map<uint32_t, std::string> names;
    return names;
}

class LinkSpanJsonProxyFactory final : public Ship::ResourceFactoryJson {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File>,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override {
        const auto name = TypeNames().find(initData->Type);
        if (name == TypeNames().end() || initData->ResourceVersion < 0) {
            return nullptr;
        }
        std::string xml;
        const ShipNativeStatus status = ShipLuaHost::TranscodeOotJson(
            name->second, initData->Path, static_cast<uint32_t>(initData->ResourceVersion), xml);
        if (status != SHIP_NATIVE_OK) {
            SPDLOG_ERROR("Link-Span: o tipo JSON '{}/{}' não converteu {} (status {})", name->second,
                         initData->ResourceVersion, initData->Path, static_cast<int>(status));
            return nullptr;
        }
        if (xml.empty() || xml.front() != '<') {
            SPDLOG_ERROR("Link-Span: o tipo JSON '{}' devolveu um XML vazio ou inválido para {}", name->second,
                         initData->Path);
            return nullptr;
        }
        auto file = std::make_shared<Ship::File>();
        file->Buffer = std::make_shared<std::vector<char>>(xml.begin(), xml.end());
        file->Buffer->push_back('\0');
        file->IsLoaded = true;
        auto manager = Ship::Context::GetRawInstance()->GetResourceManager();
        return manager->GetResourceLoader()->LoadResource(initData->Path, file);
    }
};

bool DeclareType(const std::string& type, uint32_t minVersion, uint32_t maxVersion) {
    auto* context = Ship::Context::GetRawInstance();
    auto manager = context ? context->GetResourceManager() : nullptr;
    auto loader = manager ? manager->GetResourceLoader() : nullptr;
    if (!loader) {
        return false;
    }
    static const auto factory = std::make_shared<LinkSpanJsonProxyFactory>();
    auto& names = TypeNames();
    uint32_t typeId = loader->GetResourceType(type);
    if (typeId != 0 && !names.contains(typeId)) {
        return false; // tipo do host (Scene, Texture...) ou de outra fábrica
    }
    if (typeId == 0) {
        typeId = kFirstTypeId + static_cast<uint32_t>(names.size());
        names.emplace(typeId, type);
    }
    for (uint32_t version = minVersion; version <= maxVersion; ++version) {
        if (!loader->RegisterResourceFactory(factory, RESOURCE_FORMAT_JSON, type, typeId, version)) {
            return false;
        }
    }
    return true;
}

[[maybe_unused]] const bool kJsonTypesBound = [] {
    ShipLuaHost::OotJsonTypesBridge bridge;
    bridge.declareType = DeclareType;
    ShipLuaHost::SetOotJsonTypesBridge(bridge);
    return true;
}();

} // namespace
