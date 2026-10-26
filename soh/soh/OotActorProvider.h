#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <shiplua/actor/ActorProvider.h>
#include <shiplua/capability/CapabilityRegistry.h>
#include <shiplua/handles/HandleRegistry.h>
#include <shiplua/runtime/Logger.h>
#include <shiplua/runtime/Result.h>

namespace ShipLuaHost {

inline constexpr const char* kOotHostObserverOwner = "linkspan.host";
inline constexpr std::size_t kOotSpawnLimitPerMod = 16;

// Stable logical actor id translated by the OoT host. Raw engine ids and
// native pointers never cross this provider boundary.
struct OotActorDefinition {
    std::string key;
    std::int16_t actorId = -1;
    std::int16_t objectId = -1;
    std::int16_t params = 0;
};

struct OotActorProviderHooks {
    std::function<bool(std::int16_t objectId)> objectReady;
    std::function<void*(const OotActorDefinition& definition, const ShipLua::ActorSpawnRequest& request)> spawn;
    std::function<void(void* actor)> kill;
    // RFC 0031. Lê um campo do ator nativo vivo (nome já validado pelo core).
    std::function<ShipLua::Result<ShipLua::ActorFieldValue>(void* actor, const std::string& field)> readField;
    // RFC 0031. Liga ou reescala a vida estendida no ator nativo vivo.
    std::function<ShipLua::Result<bool>(void* actor, std::uint32_t maxHealth)> setMaxHealth;
};

// Uso exclusivo da thread do jogo. Handles pertencem ao mod criador ou ao
// observador do host, expiram na troca de cena e nunca contêm um Actor*.
class OotActorProvider final : public ShipLua::ActorProvider {
  public:
    OotActorProvider(std::vector<OotActorDefinition> allowlist, OotActorProviderHooks hooks,
                     ShipLua::Logger logger = {}, std::int16_t forbiddenActorId = 0,
                     ShipLua::HandleLimits limits = { 0, 1024 });

    ShipLua::Result<void> RegisterCapabilities(ShipLua::CapabilityRegistry& registry) const;

    ShipLua::Result<ShipLua::Handle> Spawn(const std::string& ownerModId,
                                           const ShipLua::ActorSpawnRequest& request) override;
    ShipLua::Result<void> Destroy(const std::string& ownerModId, const ShipLua::Handle& handle) override;
    ShipLua::Result<bool> Exists(const std::string& ownerModId, const ShipLua::Handle& handle) const override;

    // RFC 0031. Reutiliza o handle do mod criador ou cria um de observação.
    ShipLua::Result<ShipLua::Handle> Observe(void* actor);
    // Ator nativo do handle vivo, ou nullptr.
    void* Resolve(const ShipLua::Handle& handle) const;
    ShipLua::Result<ShipLua::ActorFieldValue> Get(const ShipLua::Handle& handle, const std::string& field) const override;
    ShipLua::Result<bool> SetMaxHealth(const std::string& callingModId, const ShipLua::Handle& handle,
                                       std::uint32_t maxHealth) override;

    // Called by the host before the native Actor memory is released.
    ShipLua::Result<bool> OnNativeActorDestroyed(void* actor);
    // Called immediately before unloading one mod.
    ShipLua::Result<std::size_t> ReleaseMod(const std::string& modId) override;
    // Called from OnPlayDestroy, while native actors are still valid.
    ShipLua::Result<std::size_t> OnSceneChange();
    ShipLua::Result<std::size_t> Shutdown();

    std::size_t Count() const noexcept {
        return mRecords.size();
    }
    std::size_t CountForMod(const std::string& modId) const;
    std::size_t AllowedActorCount() const noexcept {
        return mAllowlist.size();
    }
    const ShipLua::HandleRegistry& Handles() const noexcept {
        return mHandles;
    }

  private:
    struct ActorRecord {
        ShipLua::Handle handle;
        std::string ownerModId;
        std::string actorKey;
        void* nativeActor = nullptr;
        bool observed = false;
    };

    ShipLua::Result<void> ValidateThread(const char* operation) const;
    static bool IsSafeKey(const std::string& key);
    bool SameHandle(const ActorRecord& record, const ShipLua::Handle& handle) const;

    std::map<std::string, OotActorDefinition> mAllowlist;
    std::int16_t mForbiddenActorId = 0;
    OotActorProviderHooks mHooks;
    ShipLua::Logger mLogger;
    ShipLua::HandleRegistry mHandles;
    std::map<std::uint32_t, ActorRecord> mRecords;
    std::unordered_map<void*, std::uint32_t> mByActor;
    std::thread::id mGameThread;
};

} // namespace ShipLuaHost
