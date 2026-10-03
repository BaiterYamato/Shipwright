#include "OotNativeRegistry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct ListedEntry {
    uint64_t handle;
    int32_t id;
    std::string name;
};

ShipNativeStatus SHIP_NATIVE_CALL CollectEntry(void* user, uint64_t handle, int32_t id, const char* name,
                                               uint32_t nameLength) {
    auto& entries = *static_cast<std::vector<ListedEntry>*>(user);
    entries.push_back({ handle, id, std::string(name, nameLength) });
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL StopListing(void*, uint64_t, int32_t, const char*, uint32_t) {
    return SHIP_NATIVE_FAILURE;
}

} // namespace

int main() {
    ShipLuaHost::InitializeOotNativeRegistry();
    const auto& registry = ShipLuaHost::GetOotNativeRegistryService();
    Check(registry.size == sizeof(ShipOotRegistryV1), "tabela V1 deve declarar tamanho completo");

    uint64_t scenes = 0;
    Check(registry.create_space("unbound/scenes", 0x80, 0x82, 1, &scenes) == SHIP_NATIVE_OK && scenes,
          "coremod deve criar espaço namespaced");
    uint64_t duplicate = 0;
    Check(registry.create_space("unbound/scenes", 0x90, 0x92, 1, &duplicate) == SHIP_NATIVE_INVALID_ARGUMENT,
          "nome de espaço duplicado deve ser recusado");
    uint64_t foundSpace = 0;
    Check(registry.find_space("unbound/scenes", &foundSpace) == SHIP_NATIVE_OK && foundSpace == scenes,
          "espaço deve ser descoberto por nome");

    const std::array<uint8_t, 4> forestPayload{ 0x53, 0x43, 0x4E, 0x31 };
    uint64_t forest = 0;
    int32_t forestId = 0;
    Check(registry.register_entry(scenes, "unbound/forest_temple/main", LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                  forestPayload.data(), static_cast<uint32_t>(forestPayload.size()), &forest,
                                  &forestId) == SHIP_NATIVE_OK &&
              forest && forestId == 0x80,
          "primeiro ID automático deve ser o menor livre");

    const std::array<uint8_t, 2> spiritPayload{ 0x02, 0x01 };
    uint64_t spirit = 0;
    int32_t spiritId = 0;
    Check(registry.register_entry(scenes, "unbound/spirit_temple/main", 0x82, spiritPayload.data(),
                                  static_cast<uint32_t>(spiritPayload.size()), &spirit, &spiritId) == SHIP_NATIVE_OK &&
              spiritId == 0x82,
          "ID explícito alinhado deve ser aceito");

    uint64_t water = 0;
    int32_t waterId = 0;
    Check(registry.register_entry(scenes, "unbound/water_temple/main", LINKSPAN_OOT_REGISTRY_AUTO_ID, nullptr, 0,
                                  &water, &waterId) == SHIP_NATIVE_OK &&
              waterId == 0x81,
          "alocação deve preencher o menor buraco disponível");

    uint64_t foundEntry = 0;
    int32_t foundId = 0;
    Check(registry.find_entry_by_name(scenes, "unbound/forest_temple/main", &foundEntry, &foundId) == SHIP_NATIVE_OK &&
              foundEntry == forest && foundId == forestId,
          "entrada deve ser resolvida por nome");
    foundEntry = 0;
    Check(registry.find_entry_by_id(scenes, 0x82, &foundEntry) == SHIP_NATIVE_OK && foundEntry == spirit,
          "entrada deve ser resolvida por ID");

    uint32_t nameSize = 0;
    uint32_t payloadSize = 0;
    int32_t readId = 0;
    Check(registry.read_entry(forest, nullptr, 0, &nameSize, nullptr, 0, &payloadSize, &readId) == SHIP_NATIVE_OK &&
              nameSize == std::strlen("unbound/forest_temple/main") && payloadSize == forestPayload.size() &&
              readId == 0x80,
          "consulta de tamanhos não deve exigir buffers");
    std::array<char, 64> name{};
    std::array<uint8_t, 8> payload{};
    Check(registry.read_entry(forest, name.data(), 3, &nameSize, payload.data(), payload.size(), &payloadSize,
                              &readId) == SHIP_NATIVE_LIMIT,
          "buffer curto deve informar limite sem truncar");
    Check(registry.read_entry(forest, name.data(), name.size(), &nameSize, payload.data(), payload.size(), &payloadSize,
                              &readId) == SHIP_NATIVE_OK &&
              std::string(name.data(), nameSize) == "unbound/forest_temple/main" &&
              std::equal(forestPayload.begin(), forestPayload.end(), payload.begin()),
          "host deve devolver cópias integrais de nome e payload");

    std::vector<ListedEntry> listed;
    Check(registry.list_entries(scenes, CollectEntry, &listed) == SHIP_NATIVE_OK && listed.size() == 3 &&
              listed[0].id == 0x80 && listed[1].id == 0x81 && listed[2].id == 0x82,
          "enumeração deve ser ordenada por ID");
    Check(registry.list_entries(scenes, StopListing, nullptr) == SHIP_NATIVE_FAILURE,
          "status do callback deve interromper e propagar a enumeração");

    uint64_t entrances = 0;
    Check(registry.create_space("unbound/entrances", 0x1000, 0x100C, 4, &entrances) == SHIP_NATIVE_OK,
          "espaço com stride deve ser aceito");
    uint64_t badEntry = 0;
    int32_t badId = 0;
    Check(registry.register_entry(entrances, "unbound/bad", 0x1002, nullptr, 0, &badEntry, &badId) ==
              SHIP_NATIVE_INVALID_ARGUMENT,
          "ID desalinhado deve ser recusado");

    Check(registry.unregister_entry(forest) == SHIP_NATIVE_OK, "entrada deve poder ser removida pelo handle");
    uint64_t reused = 0;
    int32_t reusedId = 0;
    Check(registry.register_entry(scenes, "unbound/fire_temple/main", LINKSPAN_OOT_REGISTRY_AUTO_ID, nullptr, 0,
                                  &reused, &reusedId) == SHIP_NATIVE_OK &&
              reusedId == 0x80,
          "ID liberado deve ser reutilizado deterministicamente");

    ShipNativeStatus otherThreadStatus = SHIP_NATIVE_OK;
    std::thread worker([&] {
        uint64_t ignored = 0;
        otherThreadStatus = registry.find_space("unbound/scenes", &ignored);
    });
    worker.join();
    Check(otherThreadStatus == SHIP_NATIVE_INVALID_ARGUMENT, "thread externa deve ser recusada");

    const auto& owned = ShipLuaHost::GetOotNativeRegistryServiceV2();
    Check(owned.size == sizeof(ShipOotRegistryV2), "tabela V2 deve declarar tamanho completo");
    uint64_t equipment = 0;
    Check(owned.create_space_owned("core.equipment", "custom/equipment", 1024, 1030, 1,
                                   &equipment) == SHIP_NATIVE_OK, "coremod cria espaço com dono");
    uint64_t sword = 0;
    int32_t swordId = 0;
    Check(owned.register_entry_owned("mod.sword", equipment, "custom/sword", LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                     nullptr, 0, &sword, &swordId) == SHIP_NATIVE_OK && swordId == 1024,
          "mod registra equipamento com dono");
    uint64_t conflict = 0;
    int32_t conflictId = 0;
    Check(owned.register_entry_owned("mod.other", equipment, "custom/sword", LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                     nullptr, 0, &conflict, &conflictId) == SHIP_NATIVE_INVALID_ARGUMENT &&
              !conflict, "chave duplicada deve falhar antes de registrar entrada parcial");
    uint32_t ownerSize = 0;
    Check(owned.read_entry_owner(sword, nullptr, 0, &ownerSize) == SHIP_NATIVE_OK &&
              ownerSize == std::strlen("mod.sword"), "dono pode ser consultado para diagnóstico");
    std::array<char, 32> ownerName{};
    Check(owned.read_entry_owner(sword, ownerName.data(), ownerName.size(), &ownerSize) == SHIP_NATIVE_OK &&
              std::string(ownerName.data(), ownerSize) == "mod.sword", "nome do dono deve ser íntegro");
    ShipLuaHost::ReleaseOotRegistryOwner("mod.other");
    Check(owned.find_entry_by_name(equipment, "custom/sword", &foundEntry, &foundId) == SHIP_NATIVE_OK,
          "rollback do mod rejeitado preserva entrada alheia");
    ShipLuaHost::ReleaseOotRegistryOwner("mod.sword");
    Check(owned.find_entry_by_name(equipment, "custom/sword", &foundEntry, &foundId) == SHIP_NATIVE_UNSUPPORTED,
          "unload remove entrada do dono");
    Check(owned.register_entry_owned("mod.other", equipment, "custom/sword", LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                     nullptr, 0, &conflict, &conflictId) == SHIP_NATIVE_OK && conflictId == 1024,
          "ID e chave podem ser reutilizados após unload");
    ShipLuaHost::ReleaseOotRegistryOwner("core.equipment");
    Check(owned.find_space("custom/equipment", &foundSpace) == SHIP_NATIVE_UNSUPPORTED,
          "unload do dono do espaço remove-o em cascata");

    Check(registry.destroy_space(scenes) == SHIP_NATIVE_OK, "destroy_space deve remover o catálogo em cascata");
    Check(registry.find_entry_by_id(scenes, 0x81, &foundEntry) == SHIP_NATIVE_UNSUPPORTED &&
              registry.unregister_entry(water) == SHIP_NATIVE_UNSUPPORTED,
          "handles do espaço destruído devem ficar inválidos");
    Check(registry.destroy_space(entrances) == SHIP_NATIVE_OK, "segundo espaço deve ser removido");

    ShipLuaHost::ResetOotNativeRegistry();
    Check(registry.find_space("unbound/scenes", &foundSpace) == SHIP_NATIVE_INVALID_ARGUMENT,
          "registry resetado deve permanecer indisponível até nova inicialização");
    return failures ? 1 : 0;
}
