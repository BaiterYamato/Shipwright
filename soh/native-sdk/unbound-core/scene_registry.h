#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LinkSpanUnbound {

// Cena e entrada não trazem número: o jogo numera na ordem do registro, e o número de cada uma depende dos mods
// montados (Unbound 0.8, SPEC §7). Tudo é endereçado pelo nome.
struct RegistryEntrance {
    std::string key;
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
    uint8_t drawConfig = 0;
    std::string titleCard; // textura I8 144x24 no VFS; vazio = sem título
    bool horse = false;
    bool horseHasSpawn = false;
    float horseX = 0.0f;
    float horseY = 0.0f;
    float horseZ = 0.0f;
    int16_t horseAngle = 0;
    std::vector<RegistryEntrance> entrances;
};

struct SceneRegistryDocument {
    std::vector<RegistryScene> scenes;
    // Uma linha por entrada recusada ou chave ignorada; as demais entradas seguem.
    std::vector<std::string> notes;
};

// Lê unbound/scenes.json já mesclado (SPEC do Unbound §7). Cenas e entradas saem na ordem de chave da
// §3.5 ($order, inteiros, depois bytes); chaves começadas por '$' ficam de fora. Retorna false só quando o documento inteiro é inválido.
bool ParseSceneRegistry(const std::string& json, SceneRegistryDocument& output, std::string& error);

} // namespace LinkSpanUnbound
