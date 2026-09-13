#include "json_merge.h"

#include <nlohmann/json.hpp>

namespace LinkSpanUnbound {
namespace {

using Json = nlohmann::ordered_json;

void MergeValue(Json& base, const Json& patch) {
    if (!base.is_object() || !patch.is_object()) {
        base = patch;
        return;
    }
    for (const auto& [key, value] : patch.items()) {
        if (key == "$schema") {
            continue;
        }
        auto current = base.find(key);
        if (current != base.end() && current->is_object() && value.is_object()) {
            MergeValue(*current, value);
        } else {
            base[key] = value;
        }
    }
}

uint64_t Hash(const std::string& bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

bool MergeDocuments(const std::string& schema, const std::vector<LayerDocument>& layers, MergeResult& output,
                    std::string& error) {
    output = {};
    error.clear();
    if (schema.empty() || layers.empty()) {
        error = "schema and at least one layer are required";
        return false;
    }
    try {
        Json merged;
        bool first = true;
        for (const auto& layer : layers) {
            const Json parsed = Json::parse(layer.json);
            if (!parsed.is_object()) {
                error = "layer root must be an object: " + layer.archive;
                return false;
            }
            const auto marker = parsed.find("$schema");
            if (marker == parsed.end() || !marker->is_string() || marker->get<std::string>() != schema) {
                error = "schema mismatch: " + layer.archive;
                return false;
            }
            if (first) {
                merged = parsed;
                first = false;
            } else {
                MergeValue(merged, parsed);
            }
        }
        merged["$schema"] = schema;
        output.json = merged.dump();
        output.hash = Hash(output.json);
        output.layerCount = static_cast<uint32_t>(layers.size());
        return true;
    } catch (const nlohmann::json::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace LinkSpanUnbound
