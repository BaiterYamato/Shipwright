#include <cstdio>
#include <cstring>
#include <new>

#include "include/linkspan/unbound/json_factory.h"
#include "oot_resources.h"

namespace {

constexpr const char* SCHEMA = "linkspan.unbound.actor-patch/v1";
constexpr const char* RESOURCE = "unbound/factory-probe.json";

struct Consumer {
    const LinkSpanUnboundJsonFactoryV1* factory;
    const ShipOotResourcesV2* resources;
};

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const char* text, uint32_t size) {
    return write(writer, text, size);
}

ShipNativeStatus SHIP_NATIVE_CALL Probe(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& consumer = *static_cast<Consumer*>(user);
    uint64_t base = 0;
    uint64_t overrideLayer = 0;
    uint64_t result = 0;
    ShipNativeStatus status = consumer.resources->mount_archive("mods/linkspan-unbound-factory-base.zip", &base);
    if (status != SHIP_NATIVE_OK || !base) {
        static const char failure[] = "fail@mount-base";
        return Write(write, writer, failure, sizeof(failure) - 1);
    }
    status = consumer.resources->mount_archive("mods/linkspan-unbound-factory-override.zip", &overrideLayer);
    if (status != SHIP_NATIVE_OK || !overrideLayer) {
        consumer.resources->unmount_archive(base);
        static const char failure[] = "fail@mount-override";
        return Write(write, writer, failure, sizeof(failure) - 1);
    }

    char json[SHIP_NATIVE_MAX_BYTES]{};
    uint32_t jsonSize = 0;
    LinkSpanUnboundJsonMetadataV1 metadata{ sizeof(LinkSpanUnboundJsonMetadataV1) };
    status = consumer.factory->load_merged(consumer.factory->context, SCHEMA, RESOURCE, &result);
    if (status == SHIP_NATIVE_OK) {
        status = consumer.factory->read_json(consumer.factory->context, result, nullptr, 0, &jsonSize);
    }
    if (status == SHIP_NATIVE_OK && jsonSize <= sizeof(json)) {
        status = consumer.factory->read_json(consumer.factory->context, result, json, sizeof(json), &jsonSize);
    }
    if (status == SHIP_NATIVE_OK) {
        status = consumer.factory->get_metadata(consumer.factory->context, result, &metadata, nullptr, 0, nullptr, 0);
    }
    const auto released = result ? consumer.factory->release(consumer.factory->context, result) : SHIP_NATIVE_OK;
    const auto unmountedOverride = consumer.resources->unmount_archive(overrideLayer);
    const auto unmountedBase = consumer.resources->unmount_archive(base);
    if (status != SHIP_NATIVE_OK || released != SHIP_NATIVE_OK || unmountedOverride != SHIP_NATIVE_OK ||
        unmountedBase != SHIP_NATIVE_OK) {
        static const char failure[] = "fail@load-or-cleanup";
        return Write(write, writer, failure, sizeof(failure) - 1);
    }

    char report[1024];
    const int count = std::snprintf(report, sizeof(report), "schema=%s; layers=%u; hash=%016llx; json=%.*s; cleanup=ok",
                                    SCHEMA, metadata.layer_count, static_cast<unsigned long long>(metadata.merged_hash),
                                    static_cast<int>(jsonSize), json);
    return count > 0 && count < static_cast<int>(sizeof(report))
               ? Write(write, writer, report, static_cast<uint32_t>(count))
               : SHIP_NATIVE_LIMIT;
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || !runtime->get_service || !runtime->register_function ||
        !instance) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto* factory = static_cast<const LinkSpanUnboundJsonFactoryV1*>(
        runtime->get_service(runtime->context, LINKSPAN_UNBOUND_JSON_FACTORY_SERVICE,
                             LINKSPAN_UNBOUND_JSON_FACTORY_VERSION, sizeof(LinkSpanUnboundJsonFactoryV1)));
    const auto* resources = static_cast<const ShipOotResourcesV2*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
                             sizeof(ShipOotResourcesV2)));
    if (!factory || !factory->has_schema || !factory->load_merged || !factory->read_json || !factory->get_metadata ||
        !factory->release || !factory->has_schema(factory->context, SCHEMA) || !resources ||
        !resources->mount_archive || !resources->unmount_archive) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* consumer = new (std::nothrow) Consumer{ factory, resources };
    if (!consumer) {
        return SHIP_NATIVE_FAILURE;
    }
    *instance = consumer;
    return runtime->register_function(runtime->context, "unbound_factory_probe", Probe, consumer);
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    delete static_cast<Consumer*>(instance);
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 1u, Init, Shutdown,
    };
    return &descriptor;
}
