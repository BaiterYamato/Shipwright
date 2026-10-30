#include "protocol.h"
#include <array>
#include <utility>
#include <cmath>
#include <algorithm>
#include <set>

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
    if (out.op != "hello" && out.op != "state" && out.op != "input" && out.op != "cancel" && out.op != "release" &&
        out.op != "result" && out.op != "scene_map" && out.op != "actors" && out.op != "teleport" &&
        out.op != "camera_state" && out.op != "camera_set" && out.op != "walk_to") {
        error = "op desconhecida";
        return false;
    }
    out.args = j;
    return ValidateV2(out, error);
}
bool ValidateV2(const Request& request, std::string& error) {
    const auto& a = request.args;
    const auto& op = request.op;
    if (op == "hello" || op == "input" || op == "state" || op == "cancel" || op == "release")
        return true; // V1 continua aceito pelo agente novo.
    std::set<std::string> allowed{ "id", "op" };
    auto fields = [&](std::initializer_list<const char*> keys) {
        for (auto k : keys)
            allowed.insert(k);
    };
    auto number = [&](const char* key, double lo, double hi, bool required = true, bool integer = false) {
        if (!a.contains(key))
            return !required;
        const auto& v = a[key];
        return v.is_number() && (!integer || v.is_number_integer()) && std::isfinite(v.get<double>()) && v >= lo &&
               v <= hi;
    };
    auto vec = [&](const char* key) {
        if (!a.contains(key) || !a[key].is_array() || a[key].size() != 3)
            return false;
        for (const auto& v : a[key])
            if (!v.is_number() || !std::isfinite(v.get<double>()) || std::abs(v.get<double>()) > 10000000)
                return false;
        return true;
    };
    bool valid = true;
    if (op == "result") {
        fields({ "ticket" });
        valid = number("ticket", 1, 9007199254740991.0, true, true);
    } else if (op == "scene_map" || op == "actors") {
        fields({ "offset", "limit" });
        valid = number("offset", 0, 65535, false, true) && number("limit", 1, 128, false, true);
        if (op == "actors") {
            fields({ "radius", "category" });
            valid = valid && number("radius", 0, 10000000, false) && number("category", 0, 11, false, true);
        }
    } else if (op == "teleport" || op == "walk_to") {
        fields({ "x", "z", "timeout" });
        valid =
            number("x", -10000000, 10000000) && number("z", -10000000, 10000000) && number("timeout", 0.1, 120, false);
        if (op == "teleport") {
            fields({ "y", "yaw" });
            valid = valid && number("y", -10000000, 10000000) && number("yaw", -360, 360, false);
        } else {
            fields({ "tolerance" });
            valid = valid && number("tolerance", 1, 1000, false);
        }
    } else if (op == "camera_set") {
        fields({ "mode", "eye", "at", "yaw", "pitch", "distance" });
        const auto mode = a.value("mode", Json("free"));
        valid = mode == "normal" || mode == "free";
        if (mode == "normal")
            valid = valid && a.size() == 3;
        else if (a.contains("eye")) {
            valid = valid && vec("eye") && vec("at") && !a.contains("yaw") && !a.contains("pitch") &&
                    !a.contains("distance");
            if (valid) {
                double d = 0;
                for (int i = 0; i < 3; ++i)
                    d += std::pow(a["eye"][i].get<double>() - a["at"][i].get<double>(), 2);
                valid = d > 0.0001;
            }
        } else
            valid = valid && number("yaw", -360, 360) && number("pitch", -89, 89) && number("distance", 1, 100000) &&
                    (!a.contains("at") || vec("at"));
    }
    for (auto it = a.begin(); it != a.end(); ++it)
        valid = valid && allowed.count(it.key()) != 0;
    if (!valid)
        error = "argumentos v2 inválidos: campo desconhecido, número fora da faixa ou combinação incompatível";
    return valid;
}
Json CrossingPlane(int actorId, int params, int rotY, int front, int back) {
    // En_Holl = 0x23; semântica do ator fica na ferramenta, nunca no exe.
    const int type = (static_cast<unsigned>(params) >> 6) & 7;
    const bool holl = actorId == 0x23 && type <= 6;
    const bool shutter = actorId == 0x2E;
    const bool vertical = holl && (type == 1 || type == 2 || type == 5);
    const double rad = rotY * (3.14159265358979323846 / 32768.0);
    double nx = vertical ? 0 : std::sin(rad), ny = vertical ? 1 : 0, nz = vertical ? 0 : std::cos(rad);
    const char* axis = vertical ? "y" : (std::abs(nx) < 0.0001 ? "z" : (std::abs(nz) < 0.0001 ? "x" : "oblique_xz"));
    // Verticais 2/5: lado 0 acima; horizontais: lado 0 em local -Z.
    const bool positiveFront = vertical;
    if (!holl && !shutter)
        return { { "axis", axis },
                 { "normal", { nx, ny, nz } },
                 { "semantics", "orientation_only" },
                 { "negative_room", nullptr },
                 { "positive_room", nullptr },
                 { "note", "orientação local-Z; ativação e sentido dependem deste tipo de ator" } };
    return { { "axis", axis },
             { "normal", { nx, ny, nz } },
             { "type", holl ? Json(type) : Json(nullptr) },
             { "semantics", holl ? "en_holl" : "door_shutter" },
             { "negative_room", positiveFront ? back : front },
             { "positive_room", positiveFront ? front : back },
             { "bidirectional", !holl || type != 1 },
             { "fixed_destination", holl && type == 1 ? Json(back) : Json(nullptr) },
             { "note", holl && type == 0
                           ? "tipo 0 antecipa carregamento do lado oposto; confirmar sala após cruzar"
                           : "sinal = dot(posição-Link - posição-plano, normal); respeitar faixa de ativação" } };
}
std::pair<int8_t, int8_t> Steering(double dx, double dz, double yaw, int magnitude) {
    const double angle = std::atan2(dx, dz) - yaw * (3.14159265358979323846 / 180.0);
    // OoT calcula o ângulo do stick com atan2(-stickX, stickY).
    return { static_cast<int8_t>(std::clamp(std::lround(-std::sin(angle) * magnitude), -128L, 127L)),
             static_cast<int8_t>(std::clamp(std::lround(std::cos(angle) * magnitude), -128L, 127L)) };
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
