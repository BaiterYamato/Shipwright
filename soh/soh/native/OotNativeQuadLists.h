#pragma once

#include <cstddef>
#include <shiplua/native/ship_native_abi.h>

namespace ShipLuaHost::Detail {

// Auxiliares internos: sem tabela/ABI publica. SAC mantem os indices dos outros colliders.
template <typename T, typename Count> void EraseQuadFromList(T** list, Count& count, T* quad, bool stableIndices) {
    if (stableIndices) {
        for (Count i = 0; i < count; ++i) {
            if (list[i] == quad) {
                list[i] = nullptr;
            }
        }
        return;
    }
    const Count previous = count;
    Count kept = 0;
    for (Count i = 0; i < previous; ++i) {
        if (list[i] != quad) {
            list[kept++] = list[i];
        }
    }
    count = kept;
    for (Count i = kept; i < previous; ++i) {
        list[i] = nullptr;
    }
}

// A inscricao anterior do mesmo quad sera retirada antes do SetAT/AC.
template <typename T, typename Count>
bool QuadListHasCapacity(T* const* list, Count count, std::size_t capacity, T* quad) {
    if (static_cast<std::size_t>(count) > capacity) {
        return false;
    }
    std::size_t kept = 0;
    for (Count i = 0; i < count; ++i) {
        kept += list[i] != quad;
    }
    return kept < capacity;
}

// Chamadas internas do jogo, sem DLL/callback de mod. O caller guarda todo o estado tocado pelo apply/SetAT/AC.
template <typename Apply, typename Rollback, typename SetAT, typename SetAC>
ShipNativeStatus RegisterQuadTransaction(bool wantsAT, bool wantsAC, Apply apply, Rollback rollback, SetAT setAT,
                                         SetAC setAC) {
    apply();
    if ((wantsAT && setAT() < 0) || (wantsAC && setAC() < 0)) {
        rollback();
        return SHIP_NATIVE_FAILURE;
    }
    return SHIP_NATIVE_OK;
}

} // namespace ShipLuaHost::Detail
