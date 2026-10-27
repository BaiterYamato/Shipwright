#include "protocol.h"
#include <array>
#include <utility>

namespace Playtest {
bool ParseButtons(const Json& names, uint16_t& mask, std::string& error) {
    static constexpr std::pair<const char*, uint16_t> buttons[] = {
        { "A", 0x8000 },     { "B", 0x4000 },     { "Z", 0x2000 },      { "START", 0x1000 }, { "DUP", 0x0800 },
        { "DDOWN", 0x0400 }, { "DLEFT", 0x0200 }, { "DRIGHT", 0x0100 }, { "L", 0x0020 },     { "R", 0x0010 },
        { "CUP", 0x0008 },   { "CDOWN", 0x0004 }, { "CLEFT", 0x0002 },  { "CRIGHT", 0x0001 }
    };
    uint16_t result = 0;
    if (!names.is_array()) {
        error = "buttons precisa ser uma lista";
        return false;
    }
    for (const auto& name : names) {
        bool found = false;
        for (const auto& [key, bit] : buttons) {
            if (name == key) {
                result |= bit;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "botão desconhecido";
            return false;
        }
    }
    mask = result;
    return true;
}
bool ParseSteps(const Json& steps, std::vector<Step>& out, std::string& error) {
    if (!steps.is_array() || steps.empty() || steps.size() > 600) {
        error = "steps precisa conter 1..600 passos";
        return false;
    }
    std::vector<Step> result;
    for (const auto& item : steps) {
        if (!item.is_object()) {
            error = "passo precisa ser objeto";
            return false;
        }
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() != "buttons" && it.key() != "stick" && it.key() != "cstick" && it.key() != "frames" &&
                it.key() != "mode") {
                error = "campo desconhecido no passo";
                return false;
            }
        }
        Step step;
        if (item.contains("buttons") && !ParseButtons(item["buttons"], step.buttons, error))
            return false;
        for (const auto& key : { "stick", "cstick" }) {
            if (!item.contains(key))
                continue;
            const auto& axes = item[key];
            if (!axes.is_array() || axes.size() != 2) {
                error = "stick precisa ter dois eixos";
                return false;
            }
            for (size_t i = 0; i < 2; ++i) {
                if (!axes[i].is_number_integer() || axes[i] < -128 || axes[i] > 127) {
                    error = "eixo fora de -128..127";
                    return false;
                }
                (std::string_view(key) == "stick" ? step.stick : step.cstick)[i] = axes[i].get<int8_t>();
            }
        }
        if (item.contains("frames")) {
            const auto& frames = item["frames"];
            if (!frames.is_number_integer() || frames < 1 || frames > 3600) {
                error = "frames fora de 1..3600";
                return false;
            }
            step.frames = frames.get<uint32_t>();
        }
        if (item.contains("mode")) {
            if (item["mode"] != "replace" && item["mode"] != "merge") {
                error = "mode precisa ser replace ou merge";
                return false;
            }
            step.merge = item["mode"] == "merge";
        }
        result.push_back(step);
    }
    out = std::move(result);
    return true;
}
bool ParseRequest(std::string_view line, Request& out, std::string& error) {
    out = {};
    if (line.size() > MaxLine) {
        error = "mensagem excede o limite";
        return false;
    }
    const auto j = Json::parse(line, nullptr, false);
    if (!j.is_object()) {
        error = "JSON inválido ou pedido não é objeto";
        return false;
    }
    if (j.contains("id") && j["id"].is_string())
        out.id = j["id"].get<std::string>();
    if (out.id.empty() || out.id.size() > 128 || !j.contains("op") || !j["op"].is_string()) {
        error = "id e op precisam ser strings; id deve ter 1..128 caracteres";
        return false;
    }
    out.op = j["op"].get<std::string>();
    if (out.op != "hello" && out.op != "state" && out.op != "input" && out.op != "cancel" && out.op != "release") {
        error = "op desconhecida";
        return false;
    }
    out.args = j;
    return true;
}
std::string Reply(const std::string& id, const Json& body) {
    Json j = body;
    j["id"] = id;
    j["ok"] = true;
    return j.dump() + '\n';
}
std::string ReplyError(const std::string& id, const std::string& message) {
    return Json{ { "id", id }, { "ok", false }, { "error", message } }.dump() + '\n';
}
} // namespace Playtest
