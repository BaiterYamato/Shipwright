#pragma once

#include <cstdint>
#include <string>
#include <thread>

#include "oot_resources.h"

namespace ShipLuaHost {

// Tipos JSON de mods (linkspan.oot.resources v3). O binding (OotNativeJsonTypesGame.cpp) põe a
// fábrica proxy no ResourceLoader; os testes usam um falso. Nulo = UNSUPPORTED.
struct OotJsonTypesBridge {
    // Declara o tipo no loader para as versões pedidas; false se o nome pertence a um tipo do host.
    bool (*declareType)(const std::string& type, uint32_t minVersion, uint32_t maxVersion) = nullptr;
};

void SetOotJsonTypesBridge(const OotJsonTypesBridge& bridge);
// Remove os registros dos mods; os tipos já declarados no loader ficam sem dono.
void InitializeOotNativeJsonTypes(std::thread::id ownerThread = std::this_thread::get_id());

ShipNativeStatus SHIP_NATIVE_CALL RegisterOotJsonType(const ShipOotJsonTypeSpecV1* spec, uint64_t* handle);
ShipNativeStatus SHIP_NATIVE_CALL UnregisterOotJsonType(uint64_t handle);

// Chamado pela fábrica proxy: XML do documento `path`. UNSUPPORTED sem dono para o tipo,
// INVALID_ARGUMENT com versão fora da faixa; outros status vêm do transcodificador.
ShipNativeStatus TranscodeOotJson(const std::string& type, const std::string& path, uint32_t version,
                                  std::string& xml);

} // namespace ShipLuaHost
