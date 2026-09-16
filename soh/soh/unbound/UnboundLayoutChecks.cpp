#include "global.h"
#include "soh/resource/type/scenecommand/SetMesh.h"

#include <limits>

namespace {

using ActorCount = decltype(((ActorContext*)nullptr)->total);
using ObjectCount = decltype(((ObjectContext*)nullptr)->num);
using ObjectCursor = decltype(((ObjectContext*)nullptr)->unk_09);
using ObjectIndex = decltype(((Actor*)nullptr)->objBankIndex);
using SetupActorCount = decltype(((PlayState*)nullptr)->numSetupActors);
using RoomCount = decltype(((PlayState*)nullptr)->numRooms);
using TransitionActorCount = decltype(((TransitionActorContext*)nullptr)->numActors);
using RuntimeMeshCount = decltype(((PolygonType0*)nullptr)->num);
using ResourceMeshCount = decltype(((SOH::PolygonType0*)nullptr)->num);

static_assert(ACTOR_NUMBER_MAX >= 8192, "Unbound actor capacity regressed");
static_assert(std::numeric_limits<ActorCount>::max() >= ACTOR_NUMBER_MAX,
              "ActorContext::total cannot represent ACTOR_NUMBER_MAX");
static_assert(static_cast<ActorCount>(8192) == 8192, "ActorContext::total narrows the documented Unbound limit");

static_assert(OBJECT_EXCHANGE_BANK_MAX >= 1024, "Unbound object bank capacity regressed");
static_assert(std::numeric_limits<ObjectCount>::max() >= OBJECT_EXCHANGE_BANK_MAX,
              "ObjectContext::num cannot represent the enlarged object bank");
static_assert(std::numeric_limits<ObjectCursor>::max() >= OBJECT_EXCHANGE_BANK_MAX,
              "ObjectContext::unk_09 cannot traverse the enlarged object bank");
static_assert(std::numeric_limits<ObjectIndex>::max() >= OBJECT_EXCHANGE_BANK_MAX - 1,
              "Actor::objBankIndex truncates enlarged object-bank slots");
static_assert(static_cast<ObjectCount>(1024) == 1024, "ObjectContext::num narrows values above 255");
static_assert(static_cast<ObjectIndex>(1023) == 1023, "Actor::objBankIndex narrows values above 127");

static_assert(std::numeric_limits<SetupActorCount>::max() >= 65535,
              "PlayState::numSetupActors still has an 8-bit limit");
static_assert(std::numeric_limits<RoomCount>::max() >= 32767, "PlayState::numRooms still has an 8-bit limit");
static_assert(std::numeric_limits<TransitionActorCount>::max() >= 32767,
              "TransitionActorContext::numActors still has an 8-bit limit");

static_assert(std::numeric_limits<RuntimeMeshCount>::max() > 255, "Runtime mesh counts still have an 8-bit limit");
static_assert(std::numeric_limits<ResourceMeshCount>::max() > 255,
              "Resource mesh counts do not mirror the runtime layout");

using EntranceScene = decltype(((EntranceInfo*)nullptr)->scene);
static_assert(std::numeric_limits<EntranceScene>::max() >= 32767,
              "EntranceInfo::scene cannot hold the scene ids registered by mods (128-32767)");

} // namespace
