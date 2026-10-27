#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace Playtest {
using Json = nlohmann::json;
inline constexpr size_t MaxLine = 256 * 1024;
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
std::string Reply(const std::string& id, const Json& body);
std::string ReplyError(const std::string& id, const std::string& message);
} // namespace Playtest
