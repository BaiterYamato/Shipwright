// Leitor adaptado de roborich/Shipwright 9.2.3-unbound0.9, commit cf7db7f7f9b65247347d54abea386669ce9c909f.
#include "actor_registry.h"
#include "json_merge.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>

namespace LinkSpanUnbound {
namespace {
struct Parser {
    std::vector<std::string>& notes;
    template<class... Args> void Note(const char* text, const Args&... args) {
        std::ostringstream output;
        output << text;
        ((output << " | " << args), ...);
        notes.push_back(output.str());
    }
static constexpr int64_t kMessageMax = 0xFFFE;

// The path under `key` with the prefix the asset loaders look for, added once whether or not the writer included
// it; "" when absent, empty or not a string.
std::string ReadPath(const Json& obj, const char* key) {
    std::string path = PathField(obj, key);
    if (path.starts_with(ActorDefinition::kOtrPrefix)) {
        path.erase(0, ActorDefinition::kOtrPrefixLength);
    }
    return path.empty() ? path : ActorDefinition::kOtrPrefix + path;
}

// A distance in world units: absent or unreadable reads as `fallback`, negative as 0.
float Distance(const Json& obj, const char* key, double fallback = 0.0) {
    return std::max((float)NumberField(obj, key, fallback), 0.0f);
}

int16_t ClampS16(int64_t value) {
    return (int16_t)std::clamp<int64_t>(value, INT16_MIN, INT16_MAX);
}

// A number the entry may omit: false when absent or unreadable (SPEC.md §2 treats a wrong type as missing).
bool OptionalNumber(const Json& obj, const char* key, float& out) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    double value = ToNumber(*it, NAN);
    if (std::isnan(value)) {
        return false;
    }
    out = (float)value;
    return true;
}

// The file's path is the type's name: not a number (a scene's `id` would read it as one) and not already an actor's
// name.
bool CheckName(const std::string& name) {
    int64_t number = 0;
    if (name.empty() || ParseIntString(name, number)) {
        Note("[Unbound] actor type '{}': not a valid actor type name", name);
        return false;
    }
    return true;
}

// False, logged, when `obj` has a key outside `known` (keys beginning with "$" are reserved, §2). `where` prefixes
// the key in the message: "" at the top level, "model." inside the model.
bool CheckKeys(const std::string& name, const Json& obj, const char* where, std::initializer_list<const char*> known) {
    for (const auto& [field, value] : obj.items()) {
        if (field.starts_with("$") || std::find(known.begin(), known.end(), field) != known.end()) {
            continue;
        }
        Note("[Unbound] actor type '{}': \"{}{}\" is not a key this build knows", name, where, field);
        return false;
    }
    return true;
}

// Keys this version does not define reject the entry, at the top level and inside each object, so a build that
// predates a later key (base, params, script, model.lod) skips the type instead of spawning it without the
// behavior.
bool CheckAllKeys(const std::string& name, const Json& def) {
    return CheckKeys(name, def, "", { "name", "model", "collision", "talk", "look" }) &&
           CheckKeys(name, Sub(def, "model"), "model.",
                     { "skeleton", "animation", "frame", "speed", "displayList", "translucent", "scale",
                       "yOffset", "segments", "hideLimbs", "shadow", "cullRadius", "drawDistance" }) &&
           CheckKeys(name, Sub(def, "collision"), "collision.", { "radius", "height", "yShift" }) &&
           CheckKeys(name, Sub(def, "talk"), "talk.", { "message", "range" }) &&
           CheckKeys(name, Sub(def, "look"), "look.", { "limb", "pivot", "range", "turnAxis", "nodAxis" });
}

void ReadSegments(const std::string& name, const Json& segments, ActorDefinition& type) {
    for (const auto& [segKey, value] : segments.items()) {
        int64_t segment = 0;
        std::string path = ReadPath(segments, segKey.c_str());
        if (!ParseIntString(segKey, segment) || segment < ActorDefinition::kSegmentMin ||
            segment > ActorDefinition::kSegmentMax || path.empty()) {
            Note("[Unbound] actor type '{}': segment \"{}\" ignored (a segment 8-12 naming a texture path)",
                         name, segKey);
            continue;
        }
        type.segments.emplace_back((uint8_t)segment, std::move(path));
    }
}

// Limbs whose own display list is not drawn: vanilla actors hide spare hands and props their code swaps in.
void ReadHideLimbs(const std::string& name, const Json& limbs, ActorDefinition& type) {
    // Mesmo teto do host (linkspan.oot.actor-models): recusa antes de copiar uma lista enorme.
    constexpr size_t kHideLimbsMax = 4096;
    if (limbs.is_array() && limbs.size() > kHideLimbsMax) {
        Note("[Unbound] actor type '{}': hideLimbs ignored ({} entries, limit {})", name, limbs.size(),
             kHideLimbsMax);
        return;
    }
    for (const Json& limb : limbs) {
        int64_t index = ToInt(limb, 0);
        if (index < 1 || index > INT32_MAX) {
            Note("[Unbound] actor type '{}': hideLimbs entry {} ignored (limbs are numbered from 1)", name,
                         limb.dump());
            continue;
        }
        type.hideLimbs.push_back((int32_t)index);
    }
}

// False, logged, unless the scale is positive and finite: zero draws nothing, a negative scale turns the model inside
// out, and the shadow size divides by it.
bool ReadScale(const std::string& name, const Json& model, ActorDefinition& type) {
    type.scale = (float)NumberField(model, "scale", 0.01);
    if (!std::isfinite(type.scale) || type.scale <= 0.0f) {
        Note("[Unbound] actor type '{}': \"{}.{}\" must be a positive number", name, "model", "scale");
        return false;
    }
    return true;
}

bool ReadModel(const std::string& name, const Json& def, ActorDefinition& type) {
    auto it = def.find("model");
    if (it == def.end() || !it->is_object()) {
        Note("[Unbound] actor type '{}' has no \"{}\"", name, "model");
        return false;
    }
    const Json& model = *it;
    type.skeleton = ReadPath(model, "skeleton");
    type.displayList = ReadPath(model, "displayList");
    if (type.skeleton.empty() == type.displayList.empty()) {
        Note("[Unbound] actor type '{}': \"{}\" needs exactly one of \"{}\" or \"{}\"", name, "model",
                     "skeleton", "displayList");
        return false;
    }
    if (!type.skeleton.empty()) {
        // Required: an OoT skeleton has no usable rest pose. With every joint angle zero it folds up.
        type.animation = ReadPath(model, "animation");
        if (type.animation.empty()) {
            Note("[Unbound] actor type '{}': a \"{}\" needs an \"{}\"", name, "skeleton", "animation");
            return false;
        }
    }
    type.holdFrame = OptionalNumber(model, "frame", type.frame);
    type.speed = (float)NumberField(model, "speed", 1.0);
    type.translucent = Field(model, "translucent") != 0;
    if (!ReadScale(name, model, type)) {
        return false;
    }
    type.yOffset = (float)NumberField(model, "yOffset");
    type.shadow = (float)NumberField(model, "shadow");
    type.cullRadius = Distance(model, "cullRadius");
    type.drawDistance = Distance(model, "drawDistance");
    ReadSegments(name, Sub(model, "segments"), type);
    ReadHideLimbs(name, SubArray(model, "hideLimbs"), type);
    return true;
}

void ReadCollision(const Json& def, ActorDefinition& type) {
    const Json& collision = Sub(def, "collision");
    type.radius = ClampS16(Field(collision, "radius"));
    type.height = ClampS16(Field(collision, "height"));
    type.yShift = ClampS16(Field(collision, "yShift"));
}

// Reads after ReadCollision: the default range depends on the radius.
void ReadTalk(const std::string& name, const Json& def, ActorDefinition& type) {
    auto it = def.find("talk");
    if (it == def.end() || !it->is_object()) {
        return;
    }
    type.talks = true;
    int64_t message = Field(*it, "message");
    if (message < 0 || message > kMessageMax) {
        Note("[Unbound] actor type '{}': message {} is not a message id; placements must set params", name,
                     message);
        message = 0;
    }
    type.message = (uint16_t)message;
    type.talkRange = Distance(*it, "range", 50.0 + std::max<int16_t>(type.radius, 0));
}

// A look axis, normalized into `axis`; absent keeps the default already there. False, logged, unless it is three
// numbers of nonzero length.
bool ReadAxis(const std::string& name, const Json& look, const char* field, ActorAxis& axis) {
    auto it = look.find(field);
    if (it == look.end()) {
        return true;
    }
    double v[3] = { NAN, NAN, NAN };
    if (it->is_array() && it->size() == 3) {
        for (size_t i = 0; i < 3; i++) {
            v[i] = ToNumber((*it)[i], NAN);
        }
    }
    double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(length) || length == 0.0) {
        Note("[Unbound] actor type '{}': \"{}.{}\" is not three numbers of nonzero length; the head will not "
                     "turn",
                     name, "look", field);
        return false;
    }
    axis = { (float)(v[0] / length), (float)(v[1] / length), (float)(v[2] / length) };
    return true;
}

// Unit axes closer than this sine of the angle between them are parallel: the head could turn but not nod.
static constexpr float kParallelSine = 1e-3f;

bool Parallel(const ActorAxis& a, const ActorAxis& b) {
    float x = a.y * b.z - a.z * b.y;
    float y = a.z * b.x - a.x * b.z;
    float z = a.x * b.y - a.y * b.x;
    return std::sqrt(x * x + y * y + z * z) < kParallelSine;
}

// False, logged, when either axis is unusable or the two are parallel.
bool ReadLookAxes(const std::string& name, const Json& look, ActorDefinition& type) {
    if (!ReadAxis(name, look, "turnAxis", type.turnAxis) || !ReadAxis(name, look, "nodAxis", type.nodAxis)) {
        return false;
    }
    if (Parallel(type.turnAxis, type.nodAxis)) {
        Note("[Unbound] actor type '{}': \"{}.{}\" and \"{}.{}\" are parallel; the head will not turn", name,
                     "look", "turnAxis", "look", "nodAxis");
        return false;
    }
    return true;
}

bool ReadLook(const std::string& name, const Json& def, ActorDefinition& type) {
    auto it = def.find("look");
    if (it == def.end() || !it->is_object()) {
        return true;
    }
    if (type.skeleton.empty()) {
        Note("[Unbound] actor type '{}': \"{}\" needs a \"{}\"", name, "look", "skeleton");
        return false;
    }
    if (!it->contains("limb")) {
        Note("[Unbound] actor type '{}': \"{}\" has no \"{}\"; the head will not turn", name, "look",
                     "limb");
        return true;
    }
    type.limb = (int32_t)Field(*it, "limb");
    type.pivot = (float)NumberField(*it, "pivot");
    type.lookRange = Distance(*it, "range", 200.0);
    type.looks = ReadLookAxes(name, *it, type);
    return true;
}

bool ReadType(const std::string& name, const Json& def, ActorDefinition& type) {
    if (!CheckName(name) || !CheckAllKeys(name, def) || !ReadModel(name, def, type)) {
        return false;
    }
    ReadCollision(def, type);
    ReadTalk(name, def, type);
    if (!ReadLook(name, def, type)) {
        return false;
    }
    type.name = name;
    type.displayName = def.contains("name") && def["name"].is_string() ? def["name"].get<std::string>() : name;
    return true;
}


};
}

bool ReadActorDefinition(const std::string& name, const Json& doc, ActorDefinition& type,
                         std::vector<std::string>& notes) {
    type = {};
    if (!doc.is_object()) { notes.push_back(name + ": tipo não é objeto"); return false; }
    return Parser{notes}.ReadType(name, doc, type);
}

std::string ActorNameFromPath(const std::string& path) {
    const std::string prefix = "unbound/actors/";
    if (!path.starts_with(prefix) || !path.ends_with(".json")) return {};
    return path.substr(prefix.size(), path.size() - prefix.size() - 5);
}

bool MergeActorLayers(const std::vector<LayerDocument>& layers, Json& merged, std::vector<std::string>& notes) {
    merged = nullptr;
    bool read = false;
    for (const auto& layer : layers) {
        if (!JsonDepthWithin(layer.json)) {
            notes.push_back(layer.archive + ": tipo de ator JSON aninhado demais; camada pulada");
            continue;
        }
        Json doc = Json::parse(layer.json, nullptr, false, true);
        if (doc.is_discarded() || (!doc.is_object() && !doc.is_null())) {
            notes.push_back(layer.archive + ": tipo de ator JSON inválido; camada pulada");
            continue;
        }
        read = true;
        if (doc.is_null()) merged = nullptr;
        else if (merged.is_null()) merged = std::move(doc);
        else MergeJson(merged, doc);
    }
    StripDirectives(merged);
    return read;
}

ShipOotActorModelSpecV1 ModelSpec(const ActorDefinition& t, const char* owner,
                                std::vector<ShipOotActorModelSegmentV1>& segments) {
    segments.clear();
    for (const auto& [segment, texture] : t.segments) segments.push_back({segment, texture.c_str()});
    ShipOotActorModelSpecV1 s{};
    s.size = sizeof(s); s.owner = owner; s.name = t.name.c_str(); s.display_name = t.displayName.c_str();
    s.skeleton = t.skeleton.c_str(); s.animation = t.animation.c_str(); s.display_list = t.displayList.c_str();
    s.hold_frame = t.holdFrame; s.translucent = t.translucent; s.frame = t.frame; s.speed = t.speed;
    s.scale = t.scale; s.y_offset = t.yOffset; s.shadow = t.shadow; s.cull_radius = t.cullRadius;
    s.draw_distance = t.drawDistance; s.segments = segments.data(); s.segment_count = uint32_t(segments.size());
    s.hide_limbs = t.hideLimbs.data(); s.hide_limb_count = uint32_t(t.hideLimbs.size());
    s.radius = t.radius; s.height = t.height; s.y_shift = t.yShift;
    s.talks = t.talks; s.message = t.message; s.talk_range = t.talkRange;
    s.looks = t.looks; s.look_limb = t.limb; s.look_pivot = t.pivot; s.look_range = t.lookRange;
    s.turn_axis[0] = t.turnAxis.x; s.turn_axis[1] = t.turnAxis.y; s.turn_axis[2] = t.turnAxis.z;
    s.nod_axis[0] = t.nodAxis.x; s.nod_axis[1] = t.nodAxis.y; s.nod_axis[2] = t.nodAxis.z;
    return s;
}

bool ResolveRoomActorId(const Json& actor, const std::function<int32_t(const std::string&)>& resolve,
                        int16_t& id, std::vector<std::string>& notes, const std::string& where) {
    if (actor.contains("params") && actor["params"].is_object()) {
        notes.push_back(where + ": params em objeto não é suportado; ator pulado");
        return false;
    }
    const auto value = actor.find("id");
    int64_t number = 0;
    if (value != actor.end() && value->is_string() && !ParseIntString(value->get<std::string>(), number)) {
        const std::string name = value->get<std::string>();
        const int32_t found = resolve ? resolve(name) : -1;
        if (found < 0 || found > INT16_MAX) {
            notes.push_back(where + ": ator desconhecido '" + name + "'; ator pulado");
            return false;
        }
        id = static_cast<int16_t>(found);
        return true;
    }
    number = Field(actor, "id");
    if (number < 0 || number >= LINKSPAN_OOT_ACTOR_MODELS_ID_BASE) {
        notes.push_back(where + ": id " + std::to_string(number) + " não é estável; use nome; ator pulado");
        return false;
    }
    id = static_cast<int16_t>(number);
    return true;
}
}
