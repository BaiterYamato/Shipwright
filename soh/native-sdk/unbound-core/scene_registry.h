#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LinkSpanUnbound {

struct RegistryEntrance {
    std::string key;
    int32_t index = -1; // -1: próximo grupo livre
    uint8_t spawn = 0;
    bool continueBgm = false;
    bool showTitleCard = false;
    uint8_t endTransition = 2;
    uint8_t startTransition = 2;
};

struct RegistryScene {
    std::string name;
    std::string displayName;
    std::string path;
    int32_t sceneId = -1; // -1: próximo id livre
    uint8_t drawConfig = 0;
    std::vector<RegistryEntrance> entrances;
};

struct SceneRegistryDocument {
    std::vector<RegistryScene> scenes;
    // Uma linha por entrada recusada ou chave ignorada; as demais entradas seguem.
    std::vector<std::string> notes;
};

// Lê unbound/scenes.json já mesclado (SPEC do Unbound §7). Cenas e entradas saem em ordem de chave;
// chaves começadas por '$' ficam de fora. Retorna false só quando o documento inteiro é inválido.
bool ParseSceneRegistry(const std::string& json, SceneRegistryDocument& output, std::string& error);

} // namespace LinkSpanUnbound
