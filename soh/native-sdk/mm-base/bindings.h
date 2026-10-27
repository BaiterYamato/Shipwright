#pragma once
#include <shiplua/native/ship_native_abi.h>

namespace MmBase {
bool BindResources(const ShipNativeRuntime* runtime);
void ClearBindings();
void* LoadResource(const char* path);
} // namespace MmBase
