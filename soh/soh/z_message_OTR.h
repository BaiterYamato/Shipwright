#pragma once
// SOH [Unbound] Owned, growable, hash-indexed message tables (unbound-docs/SPEC.md §5). The JSON text format is
// read by the Unbound framework (coremod), which feeds the tables through OTRMessage_Set.
#include "z64.h"
#include "message_data_static.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MessageLanguage { MSG_NES, MSG_GER, MSG_FRA, MSG_JPN, MSG_STAFF, MSG_LANGUAGE_COUNT } MessageLanguage;

// Builds every language table (base resource plus override/) and publishes the s*MessageEntryTablePtr globals.
// Idempotent.
void OTRMessage_Init(void);

// Hash lookup into a published table pointer. NULL when the id is absent or the table is unknown.
MessageTableEntry* OTRMessage_Find(MessageTableEntry* table, u16 textId);

// Adds or replaces message `textId` in `language` (MSG_*). `bytes` are the raw message bytes (control codes
// included); the end marker is appended when missing. Returns 1 on success. Republishes the table pointers, so
// call it outside message display (the game thread between frames).
s32 OTRMessage_Set(s32 language, u16 textId, u8 typePos, const char* bytes, u32 size);
// Removes an added or vanilla message id; returns 1 when it existed.
s32 OTRMessage_Remove(s32 language, u16 textId);

#ifdef __cplusplus
}
#endif
