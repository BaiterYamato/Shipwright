#ifndef SOH_INTERNAL_HEAP_SIZES_H
#define SOH_INTERNAL_HEAP_SIZES_H

#include "z64.h"

// SOH [Unbound] Interno ao host: não altera os headers que compõem o layout id.
// SOH [Unbound] EnItem00 x64 custa 656 B com o nó; 1 KiB/vaga dá folga para atores maiores.
// SOH [Unbound] Sistema, THA e cópia dos savestates crescem juntos; a arena permanece no THA.
#define SOH_ACTOR_HEAP_RESERVE ((size_t)ACTOR_NUMBER_MAX * 1024)
#define SOH_PLAY_HEAP_SIZE ((size_t)0x1D4790 * 2 + SOH_ACTOR_HEAP_RESERVE)
#define SOH_SYSTEM_HEAP_SIZE ((size_t)SYSTEM_HEAP_SIZE + SOH_ACTOR_HEAP_RESERVE)

#endif
