#pragma once
#include <string>
#include <shiplua/native/ship_native_abi.h>
namespace LinkSpanNei {
bool StartBackends(const ShipNativeRuntime* runtime);
void StopBackends();
std::string SensorCatalog();
std::string SensorWishes();
bool SetSensorWish(unsigned slot, unsigned item);
}
