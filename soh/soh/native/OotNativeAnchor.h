#pragma once

#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "oot_anchor.h"

namespace ShipLuaHost {

struct OotAnchorBridge {
    // Anchor conectado numa sala que sincroniza itens e flags.
    bool (*connected)() = nullptr;
};

// Resultado de aplicar o estado do time: namespaces substituídos e recusados (versão de schema diferente).
struct OotAnchorImport {
    std::vector<std::string> replaced;
    std::vector<std::string> refused;
};

void SetOotAnchorBridge(const OotAnchorBridge& bridge);
void InitializeOotNativeAnchor(std::thread::id ownerThread = std::this_thread::get_id());
// Esquece os namespaces compartilhados (testes e troca de host).
void ResetOotNativeAnchor();
const ShipOotAnchorV1& GetOotNativeAnchorService();
bool OnOotAnchorOwnerThread();

// Parte do Link-Span no estado do time: {"version":1,"namespaces":{nome:{"version":v,"data":...}}}. Vazio (null)
// sem namespace compartilhado com dados.
nlohmann::json ExportOotAnchorTeamState();
// Substitui os namespaces compartilhados que vierem no estado; os que este cliente não compartilha ficam de fora.
OotAnchorImport ImportOotAnchorTeamState(const nlohmann::json& state);

} // namespace ShipLuaHost
