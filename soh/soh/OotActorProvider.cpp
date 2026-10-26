#include "OotActorProvider.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <utility>

#include <shiplua/manifest/SemVersion.h>

namespace ShipLuaHost {
namespace {

constexpr const char* kProviderName = "shipwright-native";
constexpr const char* kProviderVersion = "0.1.0";
constexpr const char* kCapabilityVersion = "0.1.0";

ShipLua::CapabilityProvider MakeOffer(const ShipLua::SemVersion& providerVersion,
                                      const ShipLua::SemVersion& capabilityVersion, const char* permission,
                                      const char* description, std::size_t perModLimit) {
    ShipLua::CapabilityProvider offer;
    offer.name = kProviderName;
    offer.providerVersion = providerVersion;
    offer.capabilityVersion = capabilityVersion;
    offer.games = { "oot" };
    offer.stability = ShipLua::CapabilityStability::Experimental;
    offer.permissions = { permission };
    offer.limits.perMod = perModLimit;
    offer.description = description;
    return offer;
}

} // namespace

OotActorProvider::OotActorProvider(std::vector<OotActorDefinition> allowlist, OotActorProviderHooks hooks,
                                   ShipLua::Logger logger, std::int16_t forbiddenActorId, ShipLua::HandleLimits limits)
    : mForbiddenActorId(forbiddenActorId), mHooks(std::move(hooks)), mLogger(std::move(logger)), mHandles(1, limits),
      mGameThread(std::this_thread::get_id()) {
    for (OotActorDefinition& definition : allowlist) {
        if (!IsSafeKey(definition.key) || definition.actorId < 0 || definition.objectId < 0 ||
            definition.actorId == forbiddenActorId) {
            mLogger.warn("host", "ignored unsafe OoT actor allowlist entry '" + definition.key + "'");
            continue;
        }
        auto inserted = mAllowlist.emplace(definition.key, std::move(definition));
        if (!inserted.second) {
            mLogger.warn("host", "ignored duplicate OoT actor allowlist entry '" + inserted.first->first + "'");
        }
    }
}

ShipLua::Result<void> OotActorProvider::RegisterCapabilities(ShipLua::CapabilityRegistry& registry) const {
    const auto providerVersion = ShipLua::SemVersion::Parse(kProviderVersion);
    const auto capabilityVersion = ShipLua::SemVersion::Parse(kCapabilityVersion);
    if (!providerVersion.isOk() || !capabilityVersion.isOk()) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::HostFailure,
                                          "invalid built-in OoT actor provider version");
    }

    struct OfferSpec {
        const char* id;
        const char* permission;
        const char* description;
    };
    constexpr OfferSpec offers[] = {
        { "actor.spawn", "world.entities.create", "Spawn an allowlisted existing OoT actor." },
        { "actor.destroy", "world.entities.destroy", "Destroy an actor owned by the calling mod." },
        { "actor.exists", "world.entities.read", "Validate an owned OoT actor handle." },
        { "actor.observe", "world.entities.read", "Observe any OoT actor through read-only handles." },
        { "actor.health", "world.entities.write", "Extended per-actor health above 255." },
    };

    std::vector<std::string> registered;
    for (const OfferSpec& spec : offers) {
        auto result = registry.Register(spec.id, MakeOffer(*providerVersion.value, *capabilityVersion.value,
                                                           spec.permission, spec.description, kOotSpawnLimitPerMod));
        if (!result.isOk()) {
            for (const std::string& id : registered) {
                (void)registry.Unregister(id, kProviderName);
            }
            return result;
        }
        registered.emplace_back(spec.id);
    }
    return ShipLua::Result<void>::ok();
}

ShipLua::Result<ShipLua::Handle> OotActorProvider::Spawn(const std::string& ownerModId,
                                                         const ShipLua::ActorSpawnRequest& request) {
    const auto thread = ValidateThread("spawn");
    if (!thread.isOk()) {
        return ShipLua::Result<ShipLua::Handle>::err(thread.code, thread.message);
    }
    if (ownerModId.empty()) {
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidArgument,
                                                     "actor owner mod id cannot be empty");
    }
    if (CountForMod(ownerModId) >= kOotSpawnLimitPerMod) {
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::ResourceLimit,
                                                     "OoT actor spawn limit reached for mod '" + ownerModId + "'");
    }
    if (!std::isfinite(request.x) || !std::isfinite(request.y) || !std::isfinite(request.z) ||
        !std::isfinite(request.rotationX) || !std::isfinite(request.rotationY) || !std::isfinite(request.rotationZ)) {
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidArgument,
                                                     "actor transform must be finite");
    }
    // Nomes curados vêm da allowlist; qualquer outro ator é acessível pelo id
    // genérico "oot.id.<actorId>[.<params>]" (decimal ou 0x-hex). O engine já
    // recusa spawns sem o objeto carregado (Actor_Spawn retorna NULL) — só o
    // player fica proibido, porque um segundo Player corrompe o jogo.
    OotActorDefinition resolved;
    const auto definition = mAllowlist.find(request.actor);
    if (definition != mAllowlist.end()) {
        resolved = definition->second;
    } else {
        auto parseNumber = [](const std::string& text, long& out) {
            if (text.empty()) {
                return false;
            }
            char* end = nullptr;
            out = std::strtol(text.c_str(), &end, 0);
            return end != nullptr && *end == '\0';
        };
        constexpr const char* kGenericPrefix = "oot.id.";
        if (request.actor.rfind(kGenericPrefix, 0) != 0) {
            return ShipLua::Result<ShipLua::Handle>::err(
                ShipLua::ErrorCode::Unsupported,
                "OoT actor '" + request.actor + "' desconhecido — use um nome do catálogo ou oot.id.<actorId>");
        }
        const std::string payload = request.actor.substr(std::char_traits<char>::length(kGenericPrefix));
        const std::size_t dot = payload.find('.');
        long actorId = 0;
        long params = 0;
        if (!parseNumber(payload.substr(0, dot), actorId) ||
            (dot != std::string::npos && !parseNumber(payload.substr(dot + 1), params))) {
            return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidArgument,
                                                         "formato inválido: use oot.id.<actorId>[.<params>]");
        }
        if (actorId <= 0 || actorId > 0x7FFF || actorId == mForbiddenActorId) {
            return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidArgument,
                                                         "actorId fora da faixa permitida");
        }
        resolved.key = request.actor;
        resolved.actorId = static_cast<std::int16_t>(actorId);
        resolved.objectId = -1; // engine valida o objeto no Actor_Spawn
        resolved.params = static_cast<std::int16_t>(params);
    }
    if (!mHooks.objectReady || !mHooks.spawn || !mHooks.kill) {
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidState,
                                                     "OoT actor provider hooks are incomplete");
    }

    if (resolved.objectId >= 0) {
        bool objectReady = false;
        try {
            objectReady = mHooks.objectReady(resolved.objectId);
        } catch (const std::exception& error) {
            return ShipLua::Result<ShipLua::Handle>::err(
                ShipLua::ErrorCode::HostFailure, "OoT object dependency check failed: " + std::string(error.what()));
        } catch (...) {
            return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::HostFailure,
                                                         "OoT object dependency check failed");
        }
        if (!objectReady) {
            return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidState,
                                                         "required OoT object is not loaded for actor '" +
                                                             request.actor + "'");
        }
    }

    auto handle = mHandles.Create(ShipLua::HandleKind::Actor, ownerModId);
    if (!handle.isOk()) {
        return handle;
    }

    void* nativeActor = nullptr;
    try {
        nativeActor = mHooks.spawn(resolved, request);
    } catch (const std::exception& error) {
        (void)mHandles.Destroy(*handle.value, ownerModId);
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::HostFailure,
                                                     "OoT actor spawn failed: " + std::string(error.what()));
    } catch (...) {
        (void)mHandles.Destroy(*handle.value, ownerModId);
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::HostFailure, "OoT actor spawn failed");
    }
    if (nativeActor == nullptr) {
        (void)mHandles.Destroy(*handle.value, ownerModId);
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::HostFailure, "OoT rejected the actor spawn");
    }

    // actor.init pode observar o ator dentro do hook de spawn. A posse final
    // é do mod criador; o handle temporário de observação deve expirar.
    const auto existing = mByActor.find(nativeActor);
    if (existing != mByActor.end()) {
        const ActorRecord previous = mRecords.at(existing->second);
        if (!previous.observed) {
            (void)mHandles.Destroy(*handle.value, ownerModId);
            return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::HostFailure,
                                                         "OoT spawn returned an already owned actor");
        }
        (void)mHandles.Destroy(previous.handle, previous.ownerModId);
        mRecords.erase(previous.handle.slot);
        mByActor.erase(existing);
    }
    ActorRecord record{ *handle.value, ownerModId, request.actor, nativeActor, false };
    mRecords.emplace(record.handle.slot, std::move(record));
    mByActor.emplace(nativeActor, handle.value->slot);
    mLogger.info(ownerModId,
                 "spawned OoT actor '" + request.actor + "' in handle slot " + std::to_string(handle.value->slot));
    return handle;
}

ShipLua::Result<void> OotActorProvider::Destroy(const std::string& ownerModId, const ShipLua::Handle& handle) {
    const auto thread = ValidateThread("destroy");
    if (!thread.isOk()) {
        return thread;
    }
    const auto valid = mHandles.Validate(handle, ShipLua::HandleKind::Actor, ownerModId);
    if (!valid.isOk()) {
        return valid;
    }
    const auto record = mRecords.find(handle.slot);
    if (record == mRecords.end() || !SameHandle(record->second, handle)) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::HostFailure, "OoT actor handle lost its native record");
    }
    if (record->second.observed) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::PermissionDenied,
                                          "observed OoT actors cannot be destroyed by mods");
    }
    const ActorRecord value = record->second;
    try {
        mHooks.kill(value.nativeActor);
    } catch (const std::exception& error) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::HostFailure,
                                          "OoT actor destroy failed: " + std::string(error.what()));
    } catch (...) { return ShipLua::Result<void>::err(ShipLua::ErrorCode::HostFailure, "OoT actor destroy failed"); }
    // O hook pode ter notificado a destruição durante kill.
    const auto remaining = mRecords.find(handle.slot);
    if (remaining != mRecords.end() && SameHandle(remaining->second, handle)) {
        mByActor.erase(value.nativeActor);
        mRecords.erase(remaining);
        const auto destroyed = mHandles.Destroy(handle, ownerModId);
        if (!destroyed.isOk()) {
            return destroyed;
        }
    }
    mLogger.info(ownerModId, "destroyed OoT actor '" + value.actorKey + "'");
    return ShipLua::Result<void>::ok();
}

ShipLua::Result<bool> OotActorProvider::Exists(const std::string& ownerModId, const ShipLua::Handle& handle) const {
    const auto thread = ValidateThread("exists");
    if (!thread.isOk()) {
        return ShipLua::Result<bool>::err(thread.code, thread.message);
    }
    const auto valid = mHandles.Validate(handle, ShipLua::HandleKind::Actor, ownerModId);
    if (!valid.isOk()) {
        return ShipLua::Result<bool>::err(valid.code, valid.message);
    }
    const auto record = mRecords.find(handle.slot);
    if (record == mRecords.end() || !SameHandle(record->second, handle)) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::InvalidHandle,
                                          "OoT actor handle has no live native record");
    }
    if (record->second.observed) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::PermissionDenied,
                                          "observed OoT actors are not owned by mods");
    }
    return ShipLua::Result<bool>::ok(true);
}

ShipLua::Result<ShipLua::Handle> OotActorProvider::Observe(void* actor) {
    const auto thread = ValidateThread("observe");
    if (!thread.isOk()) {
        return ShipLua::Result<ShipLua::Handle>::err(thread.code, thread.message);
    }
    if (actor == nullptr) {
        return ShipLua::Result<ShipLua::Handle>::err(ShipLua::ErrorCode::InvalidArgument,
                                                     "cannot observe a null OoT actor");
    }
    const auto existing = mByActor.find(actor);
    if (existing != mByActor.end()) {
        return ShipLua::Result<ShipLua::Handle>::ok(mRecords.at(existing->second).handle);
    }
    auto handle = mHandles.Create(ShipLua::HandleKind::Actor, kOotHostObserverOwner);
    if (!handle.isOk()) {
        return handle;
    }
    ActorRecord record{ *handle.value, kOotHostObserverOwner, {}, actor, true };
    mRecords.emplace(record.handle.slot, std::move(record));
    mByActor.emplace(actor, handle.value->slot);
    return handle;
}

void* OotActorProvider::Resolve(const ShipLua::Handle& handle) const {
    if (!ValidateThread("resolve").isOk() || !mHandles.IsAlive(handle)) {
        return nullptr;
    }
    const auto record = mRecords.find(handle.slot);
    if (record == mRecords.end() || !SameHandle(record->second, handle)) {
        return nullptr;
    }
    return record->second.nativeActor;
}

ShipLua::Result<ShipLua::ActorFieldValue> OotActorProvider::Get(const ShipLua::Handle& handle,
                                                                const std::string& field) const {
    const auto thread = ValidateThread("get");
    if (!thread.isOk()) {
        return ShipLua::Result<ShipLua::ActorFieldValue>::err(thread.code, thread.message);
    }
    void* actor = Resolve(handle);
    if (actor == nullptr) {
        return ShipLua::Result<ShipLua::ActorFieldValue>::ok(std::monostate{});
    }
    if (!mHooks.readField) {
        return ShipLua::Result<ShipLua::ActorFieldValue>::err(ShipLua::ErrorCode::Unsupported,
                                                              "OoT actor readField hook is unavailable");
    }
    try {
        return mHooks.readField(actor, field);
    } catch (const std::exception& error) {
        return ShipLua::Result<ShipLua::ActorFieldValue>::err(
            ShipLua::ErrorCode::HostFailure, "OoT actor field read failed: " + std::string(error.what()));
    } catch (...) {
        return ShipLua::Result<ShipLua::ActorFieldValue>::err(ShipLua::ErrorCode::HostFailure,
                                                              "OoT actor field read failed");
    }
}

ShipLua::Result<bool> OotActorProvider::SetMaxHealth(const std::string& callingModId, const ShipLua::Handle& handle,
                                                     std::uint32_t maxHealth) {
    (void)callingModId; // A permissão de escrita é validada pelo core, sem exigir posse.
    const auto thread = ValidateThread("set max health");
    if (!thread.isOk()) {
        return ShipLua::Result<bool>::err(thread.code, thread.message);
    }
    void* actor = Resolve(handle);
    if (actor == nullptr) {
        return ShipLua::Result<bool>::ok(false);
    }
    if (!mHooks.setMaxHealth) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::Unsupported,
                                          "OoT actor setMaxHealth hook is unavailable");
    }
    try {
        return mHooks.setMaxHealth(actor, maxHealth);
    } catch (const std::exception& error) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::HostFailure,
                                          "OoT actor health update failed: " + std::string(error.what()));
    } catch (...) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::HostFailure, "OoT actor health update failed");
    }
}

ShipLua::Result<bool> OotActorProvider::OnNativeActorDestroyed(void* actor) {
    const auto thread = ValidateThread("native actor destroy");
    if (!thread.isOk()) {
        return ShipLua::Result<bool>::err(thread.code, thread.message);
    }
    if (actor == nullptr) {
        return ShipLua::Result<bool>::ok(false);
    }
    const auto existing = mByActor.find(actor);
    if (existing == mByActor.end()) {
        return ShipLua::Result<bool>::ok(false);
    }
    const auto record = mRecords.find(existing->second);
    const ActorRecord value = record->second;
    const auto destroyed = mHandles.Destroy(value.handle, value.ownerModId);
    mByActor.erase(existing);
    mRecords.erase(record);
    if (!destroyed.isOk()) {
        return ShipLua::Result<bool>::err(destroyed.code, destroyed.message);
    }
    mLogger.debug(value.ownerModId, "invalidated externally destroyed OoT actor '" + value.actorKey + "'");
    return ShipLua::Result<bool>::ok(true);
}

ShipLua::Result<std::size_t> OotActorProvider::ReleaseMod(const std::string& modId) {
    const auto thread = ValidateThread("mod release");
    if (!thread.isOk()) {
        return ShipLua::Result<std::size_t>::err(thread.code, thread.message);
    }
    if (modId.empty()) {
        return ShipLua::Result<std::size_t>::err(ShipLua::ErrorCode::InvalidArgument, "mod id cannot be empty");
    }
    std::vector<ActorRecord> owned;
    for (const auto& [slot, record] : mRecords) {
        (void)slot;
        if (!record.observed && record.ownerModId == modId) {
            owned.push_back(record);
        }
    }
    std::size_t released = 0;
    for (const ActorRecord& value : owned) {
        const auto record = mRecords.find(value.handle.slot);
        if (record == mRecords.end() || !SameHandle(record->second, value.handle)) {
            continue;
        }
        try {
            mHooks.kill(value.nativeActor);
        } catch (...) { mLogger.error(modId, "native kill failed during OoT actor cleanup"); }
        const auto remaining = mRecords.find(value.handle.slot);
        if (remaining != mRecords.end() && SameHandle(remaining->second, value.handle)) {
            mByActor.erase(value.nativeActor);
            mRecords.erase(remaining);
            (void)mHandles.Destroy(value.handle, modId);
        }
        ++released;
    }
    if (released > 0) {
        mLogger.info(modId, "released " + std::to_string(released) + " owned OoT actor(s)");
    }
    return ShipLua::Result<std::size_t>::ok(released);
}

ShipLua::Result<std::size_t> OotActorProvider::OnSceneChange() {
    const auto thread = ValidateThread("scene change");
    if (!thread.isOk()) {
        return ShipLua::Result<std::size_t>::err(thread.code, thread.message);
    }
    std::vector<ActorRecord> owned;
    for (const auto& [slot, record] : mRecords) {
        (void)slot;
        if (!record.observed) {
            owned.push_back(record);
        }
    }
    const std::size_t invalidated = mHandles.Count();
    for (const ActorRecord& record : owned) {
        if (!Resolve(record.handle)) {
            continue;
        }
        try {
            mHooks.kill(record.nativeActor);
        } catch (...) { mLogger.error(record.ownerModId, "native kill failed during OoT scene cleanup"); }
    }
    mByActor.clear();
    mRecords.clear();
    (void)mHandles.InvalidateScene();
    if (invalidated > 0) {
        mLogger.info("host", "invalidated " + std::to_string(invalidated) + " OoT actor handle(s) on scene change");
    }
    return ShipLua::Result<std::size_t>::ok(invalidated);
}

ShipLua::Result<std::size_t> OotActorProvider::Shutdown() {
    return OnSceneChange();
}

std::size_t OotActorProvider::CountForMod(const std::string& modId) const {
    return static_cast<std::size_t>(std::count_if(mRecords.begin(), mRecords.end(), [&modId](const auto& entry) {
        return !entry.second.observed && entry.second.ownerModId == modId;
    }));
}

ShipLua::Result<void> OotActorProvider::ValidateThread(const char* operation) const {
    if (std::this_thread::get_id() != mGameThread) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::InvalidState,
                                          std::string("OoT actor ") + operation + " must run on the game thread");
    }
    return ShipLua::Result<void>::ok();
}

bool OotActorProvider::IsSafeKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_' || value == '.' ||
               value == '-';
    });
}

bool OotActorProvider::SameHandle(const ActorRecord& record, const ShipLua::Handle& handle) const {
    return record.handle == handle;
}

} // namespace ShipLuaHost
