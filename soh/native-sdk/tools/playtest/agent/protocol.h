#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

namespace Playtest {
using Json = nlohmann::json;
inline constexpr size_t MaxLine = 256 * 1024;
inline constexpr int ProtocolVersion = 2;
inline constexpr size_t VfsMaxPaths = 8, VfsMaxPath = 1024, VfsMaxLayers = 32, VfsMaxArchive = 1024;
inline constexpr uint32_t VfsMaxBytes = 8 * 1024 * 1024;
inline constexpr uint64_t VfsTicketBase = uint64_t{1} << 52;
struct Step {
    uint16_t buttons = 0;
    int8_t stick[2]{};
    int8_t cstick[2]{};
    uint32_t frames = 1;
    bool merge = false;
};
struct Request {
    std::string id;
    std::string op;
    Json args;
};
bool ParseButtons(const Json& names, uint16_t& mask, std::string& error);
bool ParseSteps(const Json& steps, std::vector<Step>& out, std::string& error);
bool ParseRequest(std::string_view line, Request& out, std::string& error);
bool ValidateV2(const Request& request, std::string& error);
// Ângulos da API v2 em graus; yaw 0 olha +Z, +90 olha +X.
Json CrossingPlane(int actorId, int params, int rotY, int front, int back);
std::pair<int8_t, int8_t> Steering(double dx, double dz, double inputYawDegrees, int magnitude = 80);
std::string Reply(const std::string& id, const Json& body);
std::string ReplyError(const std::string& id, const std::string& message);
} // namespace Playtest
