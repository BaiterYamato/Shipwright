#include "bindings.h"
#include <z64.h>
#include "z64audio.h"
#include "soh/ResourceManagerHelpers.h"

namespace MmBase {
namespace {
using LoadFn = void* (*)(const char*);
LoadFn gLoad = nullptr;
} // namespace
bool BindResources(const ShipNativeRuntime* runtime) {
    uintptr_t address = 0;
    // LACUNA: resources V4 transporta bytes; ainda não carrega áudio tipado.
    if (runtime->resolve_symbol(runtime->context, "ResourceGetDataByName", &address) != SHIP_NATIVE_OK || !address)
        return false;
    gLoad = reinterpret_cast<LoadFn>(address);
    return true;
}
void ClearBindings() {
    gLoad = nullptr;
}
void* LoadResource(const char* path) {
    return gLoad && path ? gLoad(path) : nullptr;
}
} // namespace MmBase

// Shims locais da DLL: nenhuma import library do jogo.
extern "C" SoundFontSample* ResourceMgr_LoadAudioSample(const char* path) {
    return static_cast<SoundFontSample*>(MmBase::LoadResource(path));
}
extern "C" SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path) {
    return static_cast<SoundFont*>(MmBase::LoadResource(path));
}
extern "C" SequenceData* ResourceMgr_LoadSeqPtrByName(const char* path) {
    return static_cast<SequenceData*>(MmBase::LoadResource(path));
}
