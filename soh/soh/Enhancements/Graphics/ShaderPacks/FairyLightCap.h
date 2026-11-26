#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace ShaderPacks::FairyLightCap {

constexpr char CVar[] = "gShaderPack.FairyLightCap";
constexpr int NoLimit = 0;
constexpr std::size_t MaxCandidates = 64;

enum class Owner { Other, Navi, Fairy };

struct Candidate {
    Owner owner = Owner::Other;
    float score = 0;
};

template <std::size_t Capacity> struct Admission {
    std::array<bool, Capacity> accepted{};
};

// Admissão anterior à reserva de sombras: uma fada descartada não pode tomar a vaga de uma tocha.
// A Navi é obrigatória na seleção final e não consome o limite das outras fadas.
template <std::size_t Capacity>
Admission<Capacity> Admit(const std::array<Candidate, Capacity>& candidates, std::size_t count,
                          std::size_t sceneCapacity, int cap) {
    Admission<Capacity> result{};
    if (cap <= NoLimit) {
        std::fill_n(result.accepted.begin(), count, true);
        return result;
    }

    std::array<std::size_t, Capacity> fairies{};
    std::size_t fairyCount = 0;
    std::size_t otherCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (candidates[i].owner == Owner::Fairy) {
            fairies[fairyCount++] = i;
            continue;
        }
        result.accepted[i] = true;
        ++otherCount;
    }
    const auto freeSlots = sceneCapacity - std::min(sceneCapacity, otherCount);
    const auto selected = std::min({ fairyCount, freeSlots, static_cast<std::size_t>(cap) });
    std::partial_sort(fairies.begin(), fairies.begin() + selected, fairies.begin() + fairyCount,
                      [&candidates](std::size_t a, std::size_t b) {
                          return candidates[a].score < candidates[b].score ||
                                 (candidates[a].score == candidates[b].score && a < b);
                      });
    for (std::size_t i = 0; i < selected; ++i)
        result.accepted[fairies[i]] = true;
    return result;
}

} // namespace ShaderPacks::FairyLightCap
