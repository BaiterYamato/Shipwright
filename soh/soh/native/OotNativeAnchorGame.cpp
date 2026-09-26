// Liga linkspan.oot.anchor (OotNativeAnchor.cpp) e os itens sintéticos ao Anchor do SoH: o get-item de um item
// de mod vai aos parceiros pelo nome do registro, e o estado do time leva os namespaces compartilhados.
#include "OotNativeAnchor.h"

#include <spdlog/spdlog.h>

#include "OotNativeHooks.h"
#include "OotNativeItems.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
void LinkSpan_ReceiveSyntheticItem(PlayState* play, u16 item);
}

namespace {

constexpr uint32_t kItemPacketVersion = 1;

bool AnchorConnected() {
    return Anchor::Instance && Anchor::Instance->isConnected && Anchor::Instance->roomState.syncItemsAndFlags;
}

bool FindItemByName(const std::string& name, uint8_t& id) {
    for (uint32_t item = 0; item <= 0xFF; ++item) {
        const auto* record = ShipLuaHost::FindOotItem(static_cast<uint8_t>(item));
        if (record && record->name == name) {
            id = static_cast<uint8_t>(item);
            return true;
        }
    }
    return false;
}

void Warn(const std::string& who, const std::string& message) {
    SPDLOG_WARN("Link-Span anchor: {} {}", who, message);
    Notification::Emit({ .prefix = who, .message = message });
}

} // namespace

// SendPacket_GiveItem (GiveItem.cpp): item sintético vai pelo nome do registro, que é o mesmo em todo cliente com
// o mod; o id runtime depende da ordem de registro. false se o item não está registrado.
bool LinkSpan_AnchorWriteGiveItem(uint16_t item, nlohmann::json& payload) {
    const auto* record = item <= 0xFF ? ShipLuaHost::FindOotItem(static_cast<uint8_t>(item)) : nullptr;
    if (!record) {
        return false;
    }
    payload["linkspanItem"] = record->name;
    payload["linkspanItemVersion"] = kItemPacketVersion;
    return true;
}

// HandlePacket_GiveItem: entrega o item de mod pelo receive de quem registrou o nome aqui. Parceiro com um item
// que este cliente não conhece: aviso, sem entregar nada.
void LinkSpan_AnchorReadGiveItem(const nlohmann::json& payload, const std::string& from) {
    const auto name = payload.value("linkspanItem", std::string{});
    if (payload.value("linkspanItemVersion", 0u) != kItemPacketVersion || name.empty()) {
        Warn(from, "sent a mod item in a format this Link-Span does not read");
        return;
    }
    uint8_t item = 0;
    if (!FindItemByName(name, item)) {
        Warn(from, "found " + name + ", but no loaded mod registers it");
        return;
    }
    SPDLOG_INFO("Link-Span anchor: {} recebido de {}", name, from);
    LinkSpan_ReceiveSyntheticItem(gPlayState, item);
    Notification::Emit({ .prefix = from, .message = "found", .suffix = name });
}

// SendPacket_UpdateTeamState: namespaces compartilhados em state.linkspan. Vindo do OnSaveFile (thread de save), o
// oot.save.saving já rodou no OotBeforeSave; a pedido de outro cliente (thread do jogo) os mods gravam o estado
// nos namespaces agora, como num save, para o time não receber o do último save.
void LinkSpan_AnchorWriteTeamState(nlohmann::json& state) {
    if (ShipLuaHost::OnOotAnchorOwnerThread()) {
        ShipLuaHost::DispatchOotSaveHook(ShipLuaHost::GetOotHookPoints().saveSaving, gSaveContext.fileNum, -1);
    }
    auto linkspan = ShipLuaHost::ExportOotAnchorTeamState();
    if (!linkspan.is_null()) {
        state["linkspan"] = std::move(linkspan);
    }
}

// HandlePacket_UpdateTeamState: substitui os namespaces e avisa os mods por oot.anchor.state.
void LinkSpan_AnchorReadTeamState(const nlohmann::json& state) {
    if (!state.is_object() || !state.contains("linkspan")) {
        return;
    }
    const auto result = ShipLuaHost::ImportOotAnchorTeamState(state["linkspan"]);
    for (const auto& name : result.refused) {
        Warn("Team state", "has " + name + " in another schema version; local data kept");
    }
    if (!result.replaced.empty()) {
        SPDLOG_INFO("Link-Span anchor: estado do time substituiu {} namespace(s)", result.replaced.size());
        ShipLuaHost::DispatchOotSaveHook(ShipLuaHost::GetOotHookPoints().anchorState, gSaveContext.fileNum, -1);
    }
}

namespace ShipLuaHost {

// RegisterOotSaveSection (OotNativeSaveGame.cpp), na inicialização.
void RegisterOotAnchorGameHooks() {
    OotAnchorBridge bridge;
    bridge.connected = AnchorConnected;
    SetOotAnchorBridge(bridge);
}

} // namespace ShipLuaHost
