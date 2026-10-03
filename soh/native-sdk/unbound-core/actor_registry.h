#pragma once
// Formato de atores Unbound 0.9, independente do jogo.
#include "unbound_format.h"
#include "oot_actor_models.h"
#include <utility>
#include <functional>

namespace LinkSpanUnbound {
struct ActorAxis { float x, y, z; };
struct ActorDefinition {
    // Every asset path carries this prefix, which the game's asset loaders look for.
    static constexpr const char* kOtrPrefix = "__OTR__";
    static constexpr size_t kOtrPrefixLength = sizeof("__OTR__") - 1;
    // The segments a type may bind; 13 holds flex-skeleton matrices.
    static constexpr uint8_t kSegmentMin = 8;
    static constexpr uint8_t kSegmentMax = 12;

    std::string name;        // registry key; also the actor's ActorDB name
    std::string displayName; // ActorDB description

    // Model: exactly one of `skeleton` or `displayList` is set. Every path carries kOtrPrefix, so it can be passed
    // wherever vanilla code passes an asset symbol.
    std::string skeleton;
    std::string animation;  // required with a skeleton
    bool holdFrame = false; // true = hold the animation on `frame`; false = loop it at `speed`
    float frame = 0.0f;
    float speed = 1.0f;
    std::string displayList;
    bool translucent = false;
    float scale = 0.01f; // positive and finite
    float yOffset = 0.0f;
    float shadow = 0.0f;                                // round shadow size at scale 0.01; 0 = none
    float cullRadius = 0.0f;                            // world units around the origin; 0 = the default zone
    float drawDistance = 0.0f;                          // world units; 0 = the default
    std::vector<std::pair<uint8_t, std::string>> segments; // segment 8-12 -> texture path
    std::vector<int32_t> hideLimbs;                       // limb-draw numbering (root = 1)

    // Collision: a solid cylinder, present when radius and height are both positive.
    int16_t radius = 0;
    int16_t height = 0;
    int16_t yShift = 0;

    // Talk: the type can talk; a placement's message is its params when non-zero, otherwise `message`.
    bool talks = false;
    uint16_t message = 0;
    float talkRange = 0.0f;

    // Look: the head limb (limb-draw numbering, root = 1) turns toward the player, about unit axes in the limb's own
    // space, around the point `pivot` along turnAxis. The defaults are vanilla rigs' axes.
    bool looks = false;
    int32_t limb = 0;
    float pivot = 0.0f;
    float lookRange = 200.0f;
    ActorAxis turnAxis = { 1.0f, 0.0f, 0.0f };
    ActorAxis nodAxis = { 0.0f, 0.0f, 1.0f };

    bool HasCollision() const {
        return radius > 0 && height > 0;
    }
};

std::string ActorNameFromPath(const std::string& path);
bool MergeActorLayers(const std::vector<LayerDocument>& layers, Json& merged, std::vector<std::string>& notes);
bool ReadActorDefinition(const std::string& name, const Json& doc, ActorDefinition& type,
                         std::vector<std::string>& notes);
ShipOotActorModelSpecV1 ModelSpec(const ActorDefinition& type, const char* owner,
                                std::vector<ShipOotActorModelSegmentV1>& segments);
bool ResolveRoomActorId(const Json& actor, const std::function<int32_t(const std::string&)>& resolve,
                        int16_t& id, std::vector<std::string>& notes, const std::string& where);
}
