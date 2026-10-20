#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shiplua/native/ship_native_abi.h>
#include "oot_hooks.h"

typedef struct ArchivesProbe {
    char report[8192];
    uint32_t length;
} ArchivesProbe;

static ShipNativeStatus SHIP_NATIVE_CALL Observe(void* user, const ShipNativeHookCall* call) {
    ArchivesProbe* probe = (ArchivesProbe*)user;
    const ShipOotModArchivesHookV1* payload = (const ShipOotModArchivesHookV1*)call->payload;
    const uint8_t* bytes = (const uint8_t*)payload->archives;
    uint32_t index;
    int count = snprintf(probe->report, sizeof(probe->report), "archives-probe n=%u stride=%u",
                         (unsigned)payload->count, (unsigned)payload->archive_stride);
    if (count < 0 || (size_t)count >= sizeof(probe->report))
        return SHIP_NATIVE_FAILURE;
    probe->length = (uint32_t)count;
    for (index = 0; index < payload->count; ++index) {
        const ShipOotModArchiveV1* entry =
            (const ShipOotModArchiveV1*)(bytes + (size_t)index * payload->archive_stride);
        size_t remaining = sizeof(probe->report) - probe->length;
        count = snprintf(probe->report + probe->length, remaining, " %s@%s:%s", entry->owner[0] ? entry->owner : "-",
                         entry->private_prefix[0] ? entry->private_prefix : "-", entry->path);
        if (count < 0)
            return SHIP_NATIVE_FAILURE;
        if ((size_t)count >= remaining) {
            probe->length = (uint32_t)sizeof(probe->report) - 1;
            return SHIP_NATIVE_LIMIT;
        }
        probe->length += (uint32_t)count;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char* request, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    const ArchivesProbe* probe = (const ArchivesProbe*)user;
    (void)request;
    (void)length;
    return write(writer, probe->report, probe->length);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    ArchivesProbe* probe = (ArchivesProbe*)calloc(1, sizeof(ArchivesProbe));
    uint64_t handle = 0;
    ShipNativeStatus status;
    ShipNativeHookSpec spec = { sizeof(ShipNativeHookSpec),
                                LINKSPAN_OOT_HOOK_MODS_ARCHIVES,
                                1,
                                sizeof(ShipOotModArchivesHookV1),
                                SHIP_NATIVE_HOOK_OBSERVE,
                                SHIP_NATIVE_HOOK_AFTER,
                                0,
                                Observe,
                                probe };
    if (!probe)
        return SHIP_NATIVE_FAILURE;
    *output = probe;
    status = runtime->register_hook(runtime->context, &spec, &handle);
    if (status != SHIP_NATIVE_OK)
        return status;
    return runtime->register_function(runtime->context, "report", Report, probe);
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    free(instance);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                     Shutdown };
    return &descriptor;
}
