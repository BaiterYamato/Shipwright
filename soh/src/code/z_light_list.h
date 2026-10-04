// SOH [Unbound] Internal light-list API; never exported in the native SDK fingerprint.
#ifndef SOH_Z_LIGHT_LIST_H
#define SOH_Z_LIGHT_LIST_H

#include <stddef.h>
// SOH [Unbound] Include after global.h: the existing host types retain their ABI.
#ifdef __cplusplus
extern "C" {
#endif
LightNode* LightContext_InsertListLight(PlayState* play, LightContext* lightCtx, LightInfo* info, size_t listIndex);
#ifdef __cplusplus
}
#endif

#endif
