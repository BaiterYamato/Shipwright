#include "soh/OotActorProvider.h"

#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
    return condition;
}

struct FakeActor {
    int actorId = -1;
    bool killed = false;
    std::uint32_t maxHealth = 4;
};

struct Fixture {
    std::set<std::int16_t> readyObjects{ 10 };
    std::vector<std::unique_ptr<FakeActor>> actors;
    std::size_t readCalls = 0;
    std::size_t healthCalls = 0;
    void* lastReadActor = nullptr;
    std::string lastReadField;
    bool throwRead = false;
    bool throwHealth = false;
    bool notifyKill = false;
    bool observeDuringSpawn = false;
    ShipLua::Result<ShipLua::Handle> initObservation;
    ShipLuaHost::OotActorProvider provider;

    explicit Fixture(ShipLua::HandleLimits limits = { 0, 1024 })
        : provider(
              {
                  { "oot.en_dog", 1, 10, static_cast<std::int16_t>(0x8000) },
                  { "oot.en_torch2", 2, 20, 0 },
                  { "player", 0, 30, 0 },
              },
              ShipLuaHost::OotActorProviderHooks{
                  [this](std::int16_t objectId) { return readyObjects.contains(objectId); },
                  [this](const ShipLuaHost::OotActorDefinition& definition,
                         const ShipLua::ActorSpawnRequest&) -> void* {
                      auto actor = std::make_unique<FakeActor>();
                      actor->actorId = definition.actorId;
                      FakeActor* native = actor.get();
                      actors.emplace_back(std::move(actor));
                      if (observeDuringSpawn) {
                          initObservation = provider.Observe(native);
                      }
                      return native;
                  },
                  [this](void* actor) {
                      static_cast<FakeActor*>(actor)->killed = true;
                      if (notifyKill) {
                          (void)provider.OnNativeActorDestroyed(actor);
                      }
                  },
                  [this](void* actor, const std::string& field) -> ShipLua::Result<ShipLua::ActorFieldValue> {
                      ++readCalls;
                      lastReadActor = actor;
                      lastReadField = field;
                      if (throwRead) {
                          throw std::runtime_error("fake read failure");
                      }
                      if (field == "actor_id") {
                          return ShipLua::Result<ShipLua::ActorFieldValue>::ok(
                              static_cast<std::int64_t>(static_cast<FakeActor*>(actor)->actorId));
                      }
                      return ShipLua::Result<ShipLua::ActorFieldValue>::err(ShipLua::ErrorCode::InvalidArgument,
                                                                            "fake field unavailable");
                  },
                  [this](void* actor, std::uint32_t maxHealth) -> ShipLua::Result<bool> {
                      ++healthCalls;
                      if (throwHealth) {
                          throw 1;
                      }
                      if (maxHealth == 0) {
                          return ShipLua::Result<bool>::err(ShipLua::ErrorCode::InvalidArgument,
                                                            "fake health must be positive");
                      }
                      static_cast<FakeActor*>(actor)->maxHealth = maxHealth;
                      return ShipLua::Result<bool>::ok(true);
                  },
              },
              ShipLua::Logger([](ShipLua::LogLevel, const std::string&, const std::string&) {}), 0, limits) {
    }
};

ShipLua::ActorSpawnRequest DogRequest() {
    ShipLua::ActorSpawnRequest request;
    request.actor = "oot.en_dog";
    request.x = 1.0;
    request.y = 2.0;
    request.z = 3.0;
    return request;
}

void TestCapabilitiesAndAllowlist() {
    Fixture fixture;
    Check(fixture.provider.AllowedActorCount() == 2, "ACTOR_PLAYER must be removed from allowlist");

    ShipLua::CapabilityRegistry registry;
    Check(fixture.provider.RegisterCapabilities(registry).isOk(), "capabilities should register");
    const auto spawn = registry.Info("actor.spawn", "oot");
    Check(spawn.has_value(), "actor.spawn descriptor should exist for OoT");
    Check(spawn->provider == "shipwright-native", "provider name should be stable");
    Check(spawn->limits.perMod == 16, "provider descriptor should publish the per-mod limit");
    Check(!registry.Has("actor.spawn", "mm"), "OoT provider must not advertise MM support");
    const auto observe = registry.Info("actor.observe", "oot");
    if (Check(observe.has_value(), "actor.observe descriptor should exist for OoT")) {
        Check(observe->permissions == std::vector<std::string>{ "world.entities.read" },
              "observation should require read permission");
        Check(observe->description == "Observe any OoT actor through read-only handles.",
              "observation description should match the contract");
        Check(observe->limits.perMod == ShipLuaHost::kOotSpawnLimitPerMod, "observe offer should use the spawn limit");
    }
    const auto health = registry.Info("actor.health", "oot");
    if (Check(health.has_value(), "actor.health descriptor should exist for OoT")) {
        Check(health->permissions == std::vector<std::string>{ "world.entities.write" },
              "health should require write permission");
        Check(health->description == "Extended per-actor health above 255.",
              "health description should match the contract");
        Check(health->limits.perMod == ShipLuaHost::kOotSpawnLimitPerMod, "health offer should use the spawn limit");
    }
    Check(!registry.Has("actor.observe", "mm") && !registry.Has("actor.health", "mm"),
          "new capabilities must remain OoT-only");

    ShipLua::ActorSpawnRequest player = DogRequest();
    player.actor = "player";
    const auto blocked = fixture.provider.Spawn("test.mod", player);
    Check(blocked.code == ShipLua::ErrorCode::Unsupported, "ACTOR_PLAYER must stay blocked");
}

void TestSpawnDestroyAndOwnership() {
    Fixture fixture;
    const auto spawned = fixture.provider.Spawn("test.mod", DogRequest());
    Check(spawned.isOk(), "allowlisted actor should spawn");
    Check(spawned.value->kind == ShipLua::HandleKind::Actor, "spawn should mint actor handle");
    Check(fixture.provider.Exists("test.mod", *spawned.value).isOk(), "owner should resolve handle");

    const auto foreign = fixture.provider.Exists("other.mod", *spawned.value);
    Check(foreign.code == ShipLua::ErrorCode::PermissionDenied, "another mod must not resolve the handle");

    Check(fixture.provider.Destroy("test.mod", *spawned.value).isOk(), "owner should destroy actor");
    Check(fixture.actors.front()->killed, "native actor should be killed");
    const auto stale = fixture.provider.Exists("test.mod", *spawned.value);
    Check(stale.code == ShipLua::ErrorCode::InvalidHandle, "destroyed handle must be stale");
}

void TestObjectDependencyAndInvalidHandle() {
    Fixture fixture;
    ShipLua::ActorSpawnRequest torch = DogRequest();
    torch.actor = "oot.en_torch2";
    const auto missing = fixture.provider.Spawn("test.mod", torch);
    Check(missing.code == ShipLua::ErrorCode::InvalidState, "En_Torch2 must require its object dependency");

    fixture.readyObjects.insert(20);
    Check(fixture.provider.Spawn("test.mod", torch).isOk(), "En_Torch2 should spawn after its object is ready");

    ShipLua::Handle invalid;
    invalid.kind = ShipLua::HandleKind::Actor;
    invalid.slot = 999;
    invalid.generation = 1;
    invalid.sceneGeneration = 1;
    const auto result = fixture.provider.Destroy("test.mod", invalid);
    Check(result.code == ShipLua::ErrorCode::InvalidHandle, "invalid handle must fail without touching native memory");
}

void TestNativeDestroyAndLifecycleCleanup() {
    Fixture fixture;
    const auto first = fixture.provider.Spawn("first.mod", DogRequest());
    const auto second = fixture.provider.Spawn("second.mod", DogRequest());
    Check(first.isOk() && second.isOk(), "two mods should spawn independently");

    Check(fixture.provider.OnNativeActorDestroyed(fixture.actors.front().get()).value.value_or(false),
          "native destroy hook should invalidate a tracked actor");
    Check(fixture.provider.Exists("first.mod", *first.value).code == ShipLua::ErrorCode::InvalidHandle,
          "native destroy should stale the handle");

    const auto released = fixture.provider.ReleaseMod("second.mod");
    Check(released.isOk() && *released.value == 1, "mod unload should release owned actors");
    Check(fixture.actors.back()->killed, "mod unload should kill its native actor");

    const auto third = fixture.provider.Spawn("third.mod", DogRequest());
    Check(third.isOk(), "provider should remain usable after mod cleanup");
    const std::uint32_t oldScene = third.value->sceneGeneration;
    const auto changed = fixture.provider.OnSceneChange();
    Check(changed.isOk() && *changed.value == 1, "scene change should invalidate remaining actors");
    Check(fixture.provider.Handles().SceneGeneration() != oldScene, "scene change must advance handle generation");
}

void TestLimitsAndGameThread() {
    Fixture fixture({ 1, 8 });
    ShipLua::CapabilityRegistry registry;
    Check(fixture.provider.RegisterCapabilities(registry).isOk(), "custom limits should register");
    const auto descriptor = registry.Info("actor.spawn", "oot");
    Check(descriptor.has_value() && descriptor->limits.perMod == ShipLuaHost::kOotSpawnLimitPerMod,
          "capability descriptor must publish the spawn limit independently of registry limits");
    Check(fixture.provider.Spawn("test.mod", DogRequest()).isOk(), "first actor should fit limit");
    const auto limited = fixture.provider.Spawn("test.mod", DogRequest());
    Check(limited.code == ShipLua::ErrorCode::ResourceLimit, "per-mod actor limit must be enforced");

    ShipLua::Result<ShipLua::Handle> threaded;
    std::thread worker([&]() { threaded = fixture.provider.Spawn("worker.mod", DogRequest()); });
    worker.join();
    Check(threaded.code == ShipLua::ErrorCode::InvalidState, "actor spawn from a worker thread must be rejected");
}

void CheckEmpty(const ShipLua::Result<ShipLua::ActorFieldValue>& result, const char* message) {
    Check(result.isOk() && result.value.has_value() && std::holds_alternative<std::monostate>(*result.value), message);
}

void TestObservationIdentityAndOwnership() {
    Fixture fixture;
    FakeActor actor{ 42 };
    Check(fixture.provider.Observe(nullptr).code == ShipLua::ErrorCode::InvalidArgument,
          "null actors cannot be observed");
    const auto first = fixture.provider.Observe(&actor);
    const auto second = fixture.provider.Observe(&actor);
    if (!Check(first.isOk() && second.isOk(), "native actor should be observable")) {
        return;
    }
    Check(*first.value == *second.value, "observing one pointer twice should reuse its handle");
    Check(fixture.provider.Count() == 1, "repeated observation must not allocate another record");
    Check(fixture.provider.Handles().OwnerOf(*first.value) == ShipLuaHost::kOotHostObserverOwner,
          "observation handle should belong to the host");
    Check(fixture.provider.Resolve(*first.value) == &actor, "live observation should resolve its native pointer");
    for (const std::string modId : { "test.mod", ShipLuaHost::kOotHostObserverOwner }) {
        Check(fixture.provider.Exists(modId, *first.value).code == ShipLua::ErrorCode::PermissionDenied,
              "no mod should own an observation handle");
        Check(fixture.provider.Destroy(modId, *first.value).code == ShipLua::ErrorCode::PermissionDenied,
              "no mod should destroy an observed actor");
    }
    Check(!actor.killed, "observation ownership checks must not kill the actor");

    const auto spawned = fixture.provider.Spawn("test.mod", DogRequest());
    if (!Check(spawned.isOk(), "mod actor should spawn alongside observations")) {
        return;
    }
    const auto observed = fixture.provider.Observe(fixture.actors.back().get());
    Check(observed.isOk() && *observed.value == *spawned.value, "Observe should reuse the spawning mod's handle");
    Check(fixture.provider.Handles().OwnerOf(*spawned.value) == "test.mod",
          "observation must preserve spawn ownership");
    Check(fixture.provider.CountForMod(ShipLuaHost::kOotHostObserverOwner) == 0,
          "spawn counts must exclude observation records");
}

void TestObservationFieldsAndHealth() {
    Fixture fixture;
    FakeActor actor{ 42 };
    const auto observed = fixture.provider.Observe(&actor);
    if (!Check(observed.isOk(), "field test actor should be observable")) {
        return;
    }
    const auto field = fixture.provider.Get(*observed.value, "actor_id");
    Check(field.isOk() && field.value && std::get_if<std::int64_t>(&*field.value) &&
              std::get<std::int64_t>(*field.value) == 42,
          "Get should return the readField result");
    Check(fixture.readCalls == 1 && fixture.lastReadActor == &actor && fixture.lastReadField == "actor_id",
          "Get should forward the native pointer and exact field name");
    const auto rejected = fixture.provider.Get(*observed.value, "params");
    Check(rejected.code == ShipLua::ErrorCode::InvalidArgument && rejected.message == "fake field unavailable",
          "Get should preserve hook errors");
    const auto health = fixture.provider.SetMaxHealth("other.mod", *observed.value, 535);
    Check(health.isOk() && health.value.value_or(false) && actor.maxHealth == 535 && fixture.healthCalls == 1,
          "health updates should forward the pointer and maximum without requiring ownership");
    const auto invalidHealth = fixture.provider.SetMaxHealth("other.mod", *observed.value, 0);
    Check(invalidHealth.code == ShipLua::ErrorCode::InvalidArgument &&
              invalidHealth.message == "fake health must be positive",
          "health updates should preserve hook errors");
    fixture.throwRead = true;
    Check(fixture.provider.Get(*observed.value, "actor_id").code == ShipLua::ErrorCode::HostFailure,
          "readField exceptions should become HostFailure");
    fixture.throwHealth = true;
    Check(fixture.provider.SetMaxHealth("test.mod", *observed.value, 600).code == ShipLua::ErrorCode::HostFailure,
          "non-standard health hook exceptions should become HostFailure");
}

void TestObservationInvalidationAndReuse() {
    Fixture fixture;
    FakeActor actor{ 42 };
    const auto observed = fixture.provider.Observe(&actor);
    if (!Check(observed.isOk(), "invalidation test actor should be observable")) {
        return;
    }
    auto wrongKind = *observed.value;
    wrongKind.kind = ShipLua::HandleKind::Prop;
    Check(fixture.provider.Resolve(wrongKind) == nullptr, "wrong-kind handles must not resolve");
    CheckEmpty(fixture.provider.Get(wrongKind, "actor_id"), "wrong-kind Get should be empty");
    Check(fixture.provider.OnNativeActorDestroyed(&actor).value.value_or(false),
          "native destruction should invalidate observations");
    CheckEmpty(fixture.provider.Get(*observed.value, "actor_id"), "Get after native destruction should be empty");
    Check(fixture.readCalls == 0, "stale Get must not invoke readField");
    const auto staleHealth = fixture.provider.SetMaxHealth("test.mod", *observed.value, 535);
    Check(staleHealth.isOk() && staleHealth.value == false && fixture.healthCalls == 0,
          "SetMaxHealth on expired handles should return false without calling the hook");
    Check(fixture.provider.Resolve(*observed.value) == nullptr, "destroyed observations must not resolve");
    Check(fixture.provider.OnNativeActorDestroyed(&actor).value == false,
          "duplicate destruction notifications should do nothing");
    const auto replacement = fixture.provider.Observe(&actor);
    if (Check(replacement.isOk(), "a reused native address should be observable again")) {
        Check(*replacement.value != *observed.value && replacement.value->slot == observed.value->slot,
              "slot and pointer reuse must advance generation");
        CheckEmpty(fixture.provider.Get(*observed.value, "actor_id"), "old generation must not read a new record");
        Check(fixture.provider.Resolve(*replacement.value) == &actor, "new generation should resolve");
    }
}

void TestObservedCleanup() {
    Fixture fixture;
    FakeActor actor{ 42 };
    const auto observed = fixture.provider.Observe(&actor);
    const auto spawned = fixture.provider.Spawn("test.mod", DogRequest());
    if (!Check(observed.isOk() && spawned.isOk(), "cleanup test actors should register")) {
        return;
    }
    const auto hostReleased = fixture.provider.ReleaseMod(ShipLuaHost::kOotHostObserverOwner);
    Check(hostReleased.isOk() && hostReleased.value == 0, "releasing the observer owner must preserve observations");
    const auto released = fixture.provider.ReleaseMod("test.mod");
    Check(released.isOk() && released.value == 1 && fixture.actors.front()->killed,
          "mod release should kill and release spawned actors");
    Check(fixture.provider.Resolve(*observed.value) == &actor && !actor.killed,
          "mod release must preserve observed actors and their handles");
    CheckEmpty(fixture.provider.Get(*spawned.value, "actor_id"), "released spawns should not be readable");
    const auto respawned = fixture.provider.Spawn("test.mod", DogRequest());
    if (!Check(respawned.isOk(), "scene test spawn should succeed")) {
        return;
    }
    const auto changed = fixture.provider.OnSceneChange();
    Check(changed.isOk() && changed.value == 2, "scene cleanup should invalidate spawned and observed handles");
    Check(!actor.killed && fixture.actors.back()->killed, "scene cleanup should kill only spawned actors");
    Check(fixture.provider.Count() == 0 && fixture.provider.Handles().Count() == 0,
          "scene cleanup should clear records");
    CheckEmpty(fixture.provider.Get(*observed.value, "actor_id"), "observations should expire across scenes");
    const auto nextScene = fixture.provider.Observe(&actor);
    Check(nextScene.isOk() && nextScene.value->sceneGeneration != observed.value->sceneGeneration,
          "scene cleanup must clear the pointer index and advance scene generation");
}

void TestSpawnAndObservationLimits() {
    Fixture fixture;
    Check(fixture.provider.Handles().Limits().maxPerMod == 0 && fixture.provider.Handles().Limits().maxTotal == 1024,
          "default registry limits should allow 1024 total without a per-owner cap");
    for (std::size_t i = 0; i < ShipLuaHost::kOotSpawnLimitPerMod; ++i) {
        Check(fixture.provider.Spawn("test.mod", DogRequest()).isOk(), "first 16 mod spawns should fit");
    }
    Check(fixture.provider.Spawn("test.mod", DogRequest()).code == ShipLua::ErrorCode::ResourceLimit,
          "17th mod spawn should hit the provider limit");
    Check(fixture.actors.size() == ShipLuaHost::kOotSpawnLimitPerMod, "limited spawns must not invoke native spawn");
    Check(fixture.provider.Spawn("other.mod", DogRequest()).isOk(), "spawn limits should be independent per mod");
    std::vector<FakeActor> observed(300);
    for (auto& actor : observed) {
        Check(fixture.provider.Observe(&actor).isOk(), "300 observations should fit independently of spawn limits");
    }
    Check(fixture.provider.Count() == 317, "all spawned and observed actors should be tracked");

    Fixture total;
    std::vector<FakeActor> scene(1025);
    for (std::size_t i = 0; i < 1024; ++i) {
        Check(total.provider.Observe(&scene[i]).isOk(), "observations should fit up to the total limit");
    }
    Check(total.provider.Observe(&scene.back()).code == ShipLua::ErrorCode::ResourceLimit,
          "observation 1025 should hit the total registry limit");
    Check(total.provider.Observe(&scene.front()).isOk(), "existing observations should resolve even when full");
    Check(total.provider.Spawn("test.mod", DogRequest()).code == ShipLua::ErrorCode::ResourceLimit &&
              total.actors.empty(),
          "total handle exhaustion should block spawn before calling the native hook");
}

void TestMissingHooksAndThreadValidation() {
    ShipLuaHost::OotActorProvider provider({}, {});
    FakeActor actor;
    const auto observed = provider.Observe(&actor);
    if (!Check(observed.isOk(), "observation should not require native hooks")) {
        return;
    }
    Check(provider.Get(*observed.value, "actor_id").code == ShipLua::ErrorCode::Unsupported,
          "live Get without readField should be Unsupported");
    Check(provider.SetMaxHealth("test.mod", *observed.value, 535).code == ShipLua::ErrorCode::Unsupported,
          "live SetMaxHealth without its hook should be Unsupported");
    ShipLua::ErrorCode observeCode{}, getCode{}, healthCode{};
    void* resolved = &actor;
    std::thread worker([&]() {
        observeCode = provider.Observe(&actor).code;
        getCode = provider.Get(*observed.value, "actor_id").code;
        healthCode = provider.SetMaxHealth("test.mod", *observed.value, 535).code;
        resolved = provider.Resolve(*observed.value);
    });
    worker.join();
    Check(observeCode == ShipLua::ErrorCode::InvalidState && getCode == ShipLua::ErrorCode::InvalidState &&
              healthCode == ShipLua::ErrorCode::InvalidState && resolved == nullptr,
          "new entrypoints must reject worker-thread access");
    Check(provider.OnSceneChange().value == 1, "observation-only scene cleanup should not require kill");
    CheckEmpty(provider.Get(*observed.value, "actor_id"), "expired Get should be empty even without hooks");
    const auto staleHealth = provider.SetMaxHealth("test.mod", *observed.value, 535);
    Check(staleHealth.isOk() && staleHealth.value == false, "expired health should be false even without hooks");
}

void TestKillNotificationReentry() {
    Fixture fixture;
    fixture.notifyKill = true;
    const auto spawned = fixture.provider.Spawn("test.mod", DogRequest());
    if (!Check(spawned.isOk(), "reentry test spawn should succeed")) {
        return;
    }
    Check(fixture.provider.Destroy("test.mod", *spawned.value).isOk(),
          "Destroy should tolerate a native destruction notification inside kill");
    Check(fixture.provider.Count() == 0 && fixture.provider.Handles().Count() == 0,
          "reentrant Destroy should leave records and handles empty");
    Check(fixture.provider.Spawn("test.mod", DogRequest()).isOk(), "release reentry test should spawn");
    Check(fixture.provider.ReleaseMod("test.mod").value == 1, "ReleaseMod should tolerate kill notifications");
    Check(fixture.provider.Spawn("test.mod", DogRequest()).isOk(), "scene reentry test should spawn");
    Check(fixture.provider.Spawn("test.mod", DogRequest()).isOk(), "second scene reentry actor should spawn");
    Check(fixture.provider.OnSceneChange().value == 2, "scene cleanup should count handles invalidated during kill");
    Check(fixture.provider.Count() == 0 && fixture.provider.Handles().Count() == 0,
          "reentrant cleanup should leave the registry consistent");
}

void TestObservationDuringSpawn() {
    Fixture fixture;
    fixture.observeDuringSpawn = true;
    const auto spawned = fixture.provider.Spawn("test.mod", DogRequest());
    if (!Check(spawned.isOk() && fixture.initObservation.isOk(), "spawn may observe its actor during init")) {
        return;
    }
    const auto observed = fixture.provider.Observe(fixture.actors.back().get());
    Check(observed.isOk() && *observed.value == *spawned.value, "completed spawn should keep the mod-owned handle");
    Check(fixture.provider.Count() == 1 && fixture.provider.Handles().Count() == 1,
          "spawn-time observation must not leave duplicate records");
    CheckEmpty(fixture.provider.Get(*fixture.initObservation.value, "actor_id"),
               "temporary observation must expire when spawn assigns ownership");
    Check(fixture.provider.Destroy("test.mod", *spawned.value).isOk(), "spawn-time observation must not break Destroy");
    Check(fixture.provider.Count() == 0 && fixture.provider.Handles().Count() == 0,
          "destroy must leave no observation pointing at the spawned actor");
}

} // namespace

int main() {
    static_assert(std::is_base_of_v<ShipLua::ActorProvider, ShipLuaHost::OotActorProvider>);
    TestCapabilitiesAndAllowlist();
    TestSpawnDestroyAndOwnership();
    TestObjectDependencyAndInvalidHandle();
    TestNativeDestroyAndLifecycleCleanup();
    TestLimitsAndGameThread();
    TestObservationIdentityAndOwnership();
    TestObservationFieldsAndHealth();
    TestObservationInvalidationAndReuse();
    TestObservedCleanup();
    TestSpawnAndObservationLimits();
    TestMissingHooksAndThreadValidation();
    TestKillNotificationReentry();
    TestObservationDuringSpawn();
    std::cout << "OotActorProviderTests: " << failures << " failure(s)\n";
    return failures;
}
