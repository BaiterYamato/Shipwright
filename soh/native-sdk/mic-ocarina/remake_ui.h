#pragma once

#include <cstdint>
#include "z64.h"

namespace MicRemake {
constexpr std::uint32_t Physical(std::uint8_t button) { return std::uint32_t{1} << button; }
constexpr std::uint32_t NoteMask = BTN_A | BTN_CDOWN | BTN_CRIGHT | BTN_CLEFT | BTN_CUP;
// Switch positions in SDL: L=9, R=10, A=1, Y=2, X=3. Song indices stay native.
constexpr std::uint32_t MapNotes(std::uint32_t physical) {
    return ((physical & Physical(9)) ? BTN_A : 0) |
           ((physical & Physical(10)) ? BTN_CDOWN : 0) |
           ((physical & Physical(1)) ? BTN_CRIGHT : 0) |
           ((physical & Physical(2)) ? BTN_CLEFT : 0) |
           ((physical & Physical(3)) ? BTN_CUP : 0);
}
constexpr bool Visible(int gameMode, int mode) {
    return gameMode == GAMEMODE_NORMAL && mode >= MSGMODE_OCARINA_STARTING && mode < MSGMODE_TEXT_AWAIT_NEXT;
}
constexpr bool Confirmed(int mode) {
    return mode >= MSGMODE_SONG_PLAYED && mode <= MSGMODE_SONG_PLAYED_ACT;
}
constexpr bool CanBrowse(int mode, int action) {
    return mode == MSGMODE_OCARINA_PLAYING && action == OCARINA_ACTION_FREE_PLAY;
}
constexpr int HeldNote(std::uint32_t buttons) {
    constexpr std::uint32_t masks[] = { BTN_A, BTN_CDOWN, BTN_CRIGHT, BTN_CLEFT, BTN_CUP };
    for (int i = 0; i < 5; ++i) if (buttons & masks[i]) return i;
    return -1;
}
constexpr std::uint16_t LearnedSongs(std::uint32_t quests, bool scarecrow) {
    // Song order differs from quest inventory order at Lullaby/Saria/Epona.
    constexpr int questOffsets[] = { 0, 1, 2, 3, 4, 5, 8, 7, 6, 9, 10, 11 };
    std::uint16_t flags = scarecrow ? (1u << 12) : 0;
    for (int i = 0; i < 12; ++i)
        if (quests & (1u << (QUEST_SONG_MINUET + questOffsets[i]))) flags |= (1u << i);
    return flags;
}
} // namespace MicRemake
