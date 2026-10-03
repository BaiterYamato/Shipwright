// Testes do registro do NEI com tabelas falsas de linkspan.oot.items v3 e linkspan.oot.save.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "fork/nei_save_bridge.h"
#include "fork_save.h"
#include "registry.h"
#include "mods/nei_save.h"

namespace {
NeiSaveData gForkSave;
uint8_t gRestoredLanternFire = 0;
}

extern "C" NeiSaveData* Nei_Save(void) {
    return &gForkSave;
}

extern "C" void NeiLantern_RestoreFire(uint8_t fireType) {
    gRestoredLanternFire = fireType;
}

namespace {

int gFailures = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            std::fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #expr); \
            ++gFailures;                                                    \
        }                                                                   \
    } while (0)

namespace FakeItems {
struct Registered {
    std::string name;
    std::string icon;
    ShipOotItemUseFn use = nullptr;
    void* user = nullptr;
    ShipOotGetItemSpecV1 get{};
    std::string message;
    uint16_t ammo = LINKSPAN_OOT_ITEMS_NO_AMMO;
    uint16_t full = 0;
};
std::map<uint8_t, Registered> items;
uint8_t buttons[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t language = LINKSPAN_OOT_LANGUAGE_ENGLISH;
uint8_t nextId = LINKSPAN_OOT_ITEMS_FIRST_ID;
uint8_t lastGiven = 0xFF;

void Reset() {
    items.clear();
    std::memset(buttons, 0xFF, sizeof(buttons));
    language = LINKSPAN_OOT_LANGUAGE_ENGLISH;
    nextId = LINKSPAN_OOT_ITEMS_FIRST_ID;
    lastGiven = 0xFF;
}

ShipNativeStatus SHIP_NATIVE_CALL Register(const ShipOotItemSpecV1* spec, uint8_t* item) {
    Registered registered;
    registered.name = spec->name;
    registered.icon = spec->icon_path;
    registered.use = spec->use;
    registered.user = spec->user;
    *item = nextId++;
    items[*item] = registered;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Unregister(uint8_t item) {
    for (auto& button : buttons) {
        if (button == item) {
            button = 0xFF;
        }
    }
    return items.erase(item) ? SHIP_NATIVE_OK : SHIP_NATIVE_INVALID_ARGUMENT;
}
ShipNativeStatus SHIP_NATIVE_CALL Find(const char*, uint8_t*) {
    return SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL GetButton(uint8_t button, uint8_t* item) {
    if (button < 1 || button > 3) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *item = buttons[button];
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL SetButton(uint8_t button, uint8_t item) {
    if (button < 1 || button > 3 || (item != 0xFF && !items.contains(item))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    buttons[button] = item;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL SetGetItem(uint8_t item, const ShipOotGetItemSpecV1* spec) {
    auto& registered = items.at(item);
    registered.get = *spec;
    registered.message = spec->message;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Give(uint8_t item) {
    lastGiven = item;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL SetIcon(uint8_t item, const char* icon) {
    items.at(item).icon = icon;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL SetAmmo(uint8_t item, uint16_t count, uint16_t full) {
    items.at(item).ammo = count;
    items.at(item).full = full;
    return SHIP_NATIVE_OK;
}
uint8_t SHIP_NATIVE_CALL Language() {
    return language;
}
// O jogo termina de levantar o item: o host chama o receive.
void FinishGive() {
    auto& registered = items.at(lastGiven);
    registered.get.receive(registered.get.user, lastGiven);
}
ShipNativeStatus Press(uint8_t button) {
    auto& registered = items.at(buttons[button]);
    return registered.use(registered.user, buttons[button], button);
}

const ShipOotItemsV3 table{ sizeof(ShipOotItemsV3), Register, Unregister, Find, GetButton, SetButton,
                            SetGetItem,              Give,     SetIcon,    SetAmmo, Language };
} // namespace FakeItems

namespace FakeSave {
std::string block;
bool hasBlock = false;
int32_t slot = 0;
uint32_t writes = 0;
uint32_t storedVersion = 1;

ShipNativeStatus SHIP_NATIVE_CALL Open(const char*, uint32_t, uint64_t* handle) {
    *handle = 7;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Read(uint64_t, char* output, uint32_t capacity, uint32_t* size) {
    *size = hasBlock ? static_cast<uint32_t>(block.size()) : 0;
    if (!hasBlock) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (!output && capacity == 0) {
        return SHIP_NATIVE_OK;
    }
    if (capacity < block.size()) {
        return SHIP_NATIVE_LIMIT;
    }
    std::memcpy(output, block.data(), block.size());
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Write(uint64_t, const char* json, uint32_t length) {
    block.assign(json, length);
    hasBlock = true;
    ++writes;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Handle(uint64_t) {
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Version(uint64_t, uint32_t* version) {
    *version = hasBlock ? storedVersion : 0;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Required(uint64_t, uint8_t) {
    return SHIP_NATIVE_OK;
}
int32_t SHIP_NATIVE_CALL Slot() {
    return slot;
}
const ShipOotSaveV1 table{ sizeof(ShipOotSaveV1), Open, Read, Write, Handle, Version, Required,
                           Handle,                Handle, Handle, Slot };
} // namespace FakeSave

uint32_t gUses = 0;
uint32_t gReceived = 0;

ShipNativeStatus SHIP_NATIVE_CALL Use(void*, uint64_t, uint8_t) {
    ++gUses;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Received(void*, uint64_t) {
    ++gReceived;
    return SHIP_NATIVE_OK;
}

const char* kNamesSmall[3] = { "Seed Pouch", "Samentasche", nullptr };
const char* kNamesBig[3] = { "Big Seed Pouch", nullptr, "" };

struct Definition {
    NeiItemLevelV1 levels[2];
    NeiItemDefinitionV1 item;
    Definition(const Definition&) = delete;
    Definition& operator=(const Definition&) = delete;
    Definition() {
        levels[0] = { sizeof(NeiItemLevelV1), "textures/a", {}, 10 };
        std::memcpy(levels[0].names, kNamesSmall, sizeof(kNamesSmall));
        levels[1] = { sizeof(NeiItemLevelV1), "textures/b", {}, 20 };
        std::memcpy(levels[1].names, kNamesBig, sizeof(kNamesBig));
        item = {};
        item.size = sizeof(NeiItemDefinitionV1);
        item.id = "test.nei.seeds";
        item.age = LINKSPAN_OOT_ITEM_AGE_ANY;
        item.model_path = "objects/x/gModelDL";
        item.model_layer = LINKSPAN_OOT_ITEMS_LAYER_OPAQUE;
        item.model_scale = 1.0f;
        item.get_messages[0] = "You got seeds!";
        item.get_messages[1] = "Samen!";
        item.level_count = 2;
        item.levels = levels;
        item.give_count = 5;
        item.use = Use;
        item.received = Received;
    }
};

std::string Name(LinkSpanNei::Registry& registry, uint64_t handle, uint8_t language) {
    char text[LINKSPAN_NEI_MAX_NAME];
    uint32_t size = 0;
    if (registry.GetName(handle, language, text, sizeof(text), &size) != SHIP_NATIVE_OK) {
        return "<erro>";
    }
    return std::string(text, size);
}

void TestValidation() {
    FakeItems::Reset();
    LinkSpanNei::Registry registry;
    registry.Attach(&FakeItems::table, &FakeSave::table, 7);
    uint64_t handle = 0;
    // Definition aponta para os próprios níveis: um objeto novo por caso.
    const auto rejects = [&](auto mutate) {
        Definition bad;
        mutate(bad);
        return registry.Define(&bad.item, &handle) == SHIP_NATIVE_INVALID_ARGUMENT;
    };
    CHECK(rejects([](Definition& d) { d.item.id = "semponto"; }));
    CHECK(rejects([](Definition& d) { d.item.get_messages[0] = nullptr; }));
    CHECK(rejects([](Definition& d) { d.levels[0].max_count = 100; }));
    CHECK(rejects([](Definition& d) { d.item.level_count = 0; }));
    CHECK(rejects([](Definition& d) { d.levels[1].icon_path = "__OTR__textures/b"; }));
    CHECK(rejects([](Definition& d) { d.item.get_messages[2] = "AcentuaÃ§Ã£o"; }));
    CHECK(rejects([](Definition& d) { d.item.model_scale = 0.0f; }));
    CHECK(FakeItems::items.empty());

    Definition good;
    CHECK(registry.Define(&good.item, &handle) == SHIP_NATIVE_OK && handle != 0);
    uint64_t again = 0;
    CHECK(registry.Define(&good.item, &again) == SHIP_NATIVE_INVALID_ARGUMENT);
    uint64_t found = 0;
    CHECK(registry.Find("test.nei.seeds", &found) == SHIP_NATIVE_OK && found == handle);
    CHECK(registry.Find("test.nei.none", &found) == SHIP_NATIVE_UNSUPPORTED && found == 0);
    CHECK(registry.Equip(handle, 2) == SHIP_NATIVE_LIMIT); // sem posse
    CHECK(registry.Equip(handle, 4) == SHIP_NATIVE_INVALID_ARGUMENT);
    CHECK(registry.SetLevel(handle, 2) == SHIP_NATIVE_INVALID_ARGUMENT);
    registry.Detach();
    CHECK(FakeItems::items.empty());
}

void TestGiveUseAndLevels() {
    FakeItems::Reset();
    FakeSave::hasBlock = false;
    FakeSave::writes = 0;
    FakeSave::slot = 1;
    gUses = gReceived = 0;
    LinkSpanNei::Registry registry;
    registry.Attach(&FakeItems::table, &FakeSave::table, 7);
    registry.OnSaveLoaded();
    Definition definition;
    uint64_t handle = 0;
    CHECK(registry.Define(&definition.item, &handle) == SHIP_NATIVE_OK);
    NeiItemStateV1 state{ sizeof(NeiItemStateV1) };
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK);
    const uint8_t runtime = state.runtime_id;
    CHECK(!state.owned && state.count == 0 && state.max_count == 10 && state.button == LINKSPAN_NEI_NO_BUTTON);
    CHECK(FakeItems::items.at(runtime).icon == "textures/a");
    CHECK(FakeItems::items.at(runtime).ammo == 0 && FakeItems::items.at(runtime).full == 10);

    // Botão restaurado pelo host sem posse: apertar não chama o mod.
    FakeItems::buttons[1] = runtime;
    CHECK(FakeItems::Press(1) == SHIP_NATIVE_OK && gUses == 0);

    // Armar para o randomizer grava o get-item sem entregar.
    FakeItems::lastGiven = 0xFF;
    CHECK(registry.ArmGetItem(handle) == SHIP_NATIVE_OK && FakeItems::lastGiven == 0xFF &&
          FakeItems::items.at(runtime).message == "You got seeds!");
    CHECK(registry.ArmGetItem(0) == SHIP_NATIVE_INVALID_ARGUMENT);

    FakeItems::language = LINKSPAN_OOT_LANGUAGE_GERMAN;
    CHECK(registry.Give(handle) == SHIP_NATIVE_OK && FakeItems::lastGiven == runtime);
    CHECK(FakeItems::items.at(runtime).message == "Samen!");
    FakeItems::language = LINKSPAN_OOT_LANGUAGE_FRENCH;
    CHECK(registry.Give(handle) == SHIP_NATIVE_OK && FakeItems::items.at(runtime).message == "You got seeds!");
    FakeItems::FinishGive();
    CHECK(gReceived == 1);
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && state.owned && state.count == 5);
    CHECK(FakeItems::items.at(runtime).ammo == 5);
    CHECK(FakeSave::writes >= 1 && FakeSave::block.find("\"test.nei.seeds\"") != std::string::npos);

    CHECK(registry.Equip(handle, 3) == SHIP_NATIVE_OK);
    CHECK(FakeItems::buttons[3] == runtime && FakeItems::buttons[1] == 0xFF);
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && state.button == 3);
    CHECK(FakeItems::Press(3) == SHIP_NATIVE_OK && gUses == 1);

    uint16_t result = 0;
    CHECK(registry.AddCount(handle, -5, &result) == SHIP_NATIVE_OK && result == 0);
    CHECK(FakeItems::Press(3) == SHIP_NATIVE_OK && gUses == 1); // sem munição
    CHECK(registry.AddCount(handle, -1, &result) == SHIP_NATIVE_LIMIT && result == 0);
    CHECK(registry.AddCount(handle, 50, &result) == SHIP_NATIVE_OK && result == 10);

    CHECK(Name(registry, handle, LINKSPAN_OOT_LANGUAGE_GERMAN) == "Samentasche");
    CHECK(Name(registry, handle, LINKSPAN_OOT_LANGUAGE_FRENCH) == "Seed Pouch");
    CHECK(registry.SetLevel(handle, 1) == SHIP_NATIVE_OK);
    CHECK(FakeItems::items.at(runtime).icon == "textures/b" && FakeItems::items.at(runtime).full == 20);
    CHECK(Name(registry, handle, LINKSPAN_OOT_LANGUAGE_GERMAN) == "Big Seed Pouch");
    CHECK(registry.AddCount(handle, 50, &result) == SHIP_NATIVE_OK && result == 20);
    CHECK(registry.SetLevel(handle, 0) == SHIP_NATIVE_OK);
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && state.count == 10 && state.max_count == 10);
    uint32_t size = 99;
    CHECK(registry.GetName(handle, 0, nullptr, 0, &size) == SHIP_NATIVE_OK && size == 10);
    char tiny[4];
    CHECK(registry.GetName(handle, 0, tiny, sizeof(tiny), &size) == SHIP_NATIVE_LIMIT);
    CHECK(registry.SetLevel(handle, 1) == SHIP_NATIVE_OK && registry.SetCount(handle, 12) == SHIP_NATIVE_OK);

    // Changing a rune/mode icon must preserve the item, ammo, level and equipped button.
    const uint32_t iconWrites = FakeSave::writes;
    CHECK(registry.UpdateIcon(handle, "textures/rune/stasis") == SHIP_NATIVE_OK);
    CHECK(FakeItems::items.at(runtime).icon == "textures/rune/stasis");
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && state.owned && state.count == 12 &&
          state.level == 1 && state.button == 3 && state.runtime_id == runtime);
    CHECK(FakeSave::writes == iconWrites);
    CHECK(registry.UpdateIcon(handle, "__OTR__textures/bad") == SHIP_NATIVE_INVALID_ARGUMENT);
    CHECK(registry.UpdateIcon(handle, nullptr) == SHIP_NATIVE_INVALID_ARGUMENT);
    CHECK(registry.UpdateIcon(0, "textures/a") == SHIP_NATIVE_INVALID_ARGUMENT);

    // Revogar tira dos botões e zera o contador.
    CHECK(registry.Revoke(handle) == SHIP_NATIVE_OK);
    CHECK(FakeItems::buttons[3] == 0xFF);
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && !state.owned && state.count == 0);
    CHECK(registry.Grant(handle) == SHIP_NATIVE_OK && registry.Equip(handle, 1) == SHIP_NATIVE_OK);
    CHECK(registry.SetCount(handle, 12) == SHIP_NATIVE_OK);
    registry.Detach();
}

void TestSaveRoundTrip() {
    // O bloco do teste anterior: possuído, nível 1, 12 unidades, e um id que ninguém definiu.
    FakeSave::block = "{\"items\":{\"test.nei.seeds\":{\"owned\":true,\"count\":12,\"level\":1},"
                      "\"other.mod.thing\":{\"owned\":true,\"count\":3,\"level\":0}}}";
    FakeSave::hasBlock = true;
    FakeSave::slot = 0;
    FakeItems::Reset();
    LinkSpanNei::Registry registry;
    registry.Attach(&FakeItems::table, &FakeSave::table, 7);
    Definition definition;
    uint64_t handle = 0;
    CHECK(registry.Define(&definition.item, &handle) == SHIP_NATIVE_OK);
    NeiItemStateV1 state{ sizeof(NeiItemStateV1) };
    // Antes do load o estado é vazio (tela de título).
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && !state.owned);
    const uint8_t runtime = state.runtime_id;
    // O host restaura o botão pelo nome antes do hook oot.save.loaded.
    FakeItems::buttons[2] = runtime;
    registry.OnSaveLoaded();
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && state.owned && state.level == 1 &&
          state.count == 12 && state.max_count == 20 && state.button == 2);
    CHECK(FakeItems::items.at(runtime).icon == "textures/b" && FakeItems::items.at(runtime).ammo == 12);
    CHECK(FakeItems::Press(2) == SHIP_NATIVE_OK);

    // Uma mudança grava o bloco inteiro, preservando o id desconhecido.
    CHECK(registry.AddCount(handle, -2, nullptr) == SHIP_NATIVE_OK);
    CHECK(FakeSave::block.find("\"other.mod.thing\"") != std::string::npos);
    CHECK(FakeSave::block.find("\"count\":10") != std::string::npos);

    // Outro arquivo sem posse: o botão restaurado sai e o contador zera.
    FakeSave::block = "{\"items\":{}}";
    registry.OnSaveLoaded();
    CHECK(FakeItems::buttons[2] == 0xFF);
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && !state.owned && state.count == 0 &&
          state.level == 0 && state.max_count == 10);
    CHECK(FakeItems::items.at(runtime).icon == "textures/a");

    // JSON válido com tipo trocado num campo: só aquele campo vale o padrão; o resto do item e os outros ids
    // carregam e sobrevivem à próxima gravação.
    FakeSave::block = "{\"items\":{\"test.nei.seeds\":{\"owned\":\"sim\",\"count\":\"12\",\"level\":1},"
                      "\"other.mod.thing\":{\"owned\":true,\"count\":3,\"level\":0}}}";
    registry.OnSaveLoaded();
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && !state.owned && state.count == 0 &&
          state.level == 1);
    CHECK(registry.SetLevel(handle, 0) == SHIP_NATIVE_OK);
    CHECK(FakeSave::block.find("\"other.mod.thing\":{\"count\":3,\"level\":0,\"owned\":true}") !=
          std::string::npos);

    // Bloco corrompido é ignorado; sem arquivo carregado nada é gravado.
    FakeSave::block = "{nao e json";
    registry.OnSaveLoaded();
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && !state.owned);
    FakeSave::slot = -1;
    const uint32_t writes = FakeSave::writes;
    CHECK(registry.Grant(handle) == SHIP_NATIVE_OK && FakeSave::writes == writes);
    FakeSave::slot = 0;
    registry.Flush();
    CHECK(FakeSave::writes == writes + 1);

    CHECK(registry.Remove(handle) == SHIP_NATIVE_OK && FakeItems::items.empty());
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_INVALID_ARGUMENT);
    // Definir de novo recupera o estado em memória.
    CHECK(registry.Define(&definition.item, &handle) == SHIP_NATIVE_OK);
    CHECK(registry.GetState(handle, &state) == SHIP_NATIVE_OK && state.owned && state.count == 5);
    registry.Detach();
}

void TestForkSaveRoundTrip() {
    using Json = nlohmann::json;
    FakeSave::slot = 0;
    FakeSave::hasBlock = false;
    LinkSpanNei::ForkSave save;
    save.Attach(&FakeSave::table, 8);
    save.OnSaveLoaded();
    CHECK(gForkSave.ownedItems[0] == 0xFF && gForkSave.bottleSlots[0] == 0xFF);

    Json input = Json::object();
    input["ownedItems"] = Json::array();
    for (uint16_t i = 0; i < 48; ++i) {
        input["ownedItems"].push_back(i + 1);
    }
    input["bottleSlots"] = Json::array();
    for (uint8_t i = 0; i < 8; ++i) {
        input["bottleSlots"].push_back(i + 10);
    }
    input["caneSkills"] = 63;
    input["trirodEchoesHi"] = 305419896u;
    input["trirodLayoutVersion"] = 2;
    input["pictoFlags0"] = 17;
    input["lanternFireType"] = 1;
    input["lanternCapturedTypes"] = 2;
    input["wandRodsOwned"] = 63;
    input["slateRunesOwned"] = 15;
    input["seasonsOwned"] = 15;
    // A partial photo array from an older/imported save retains its valid pixels.
    input["pictoPhotoI5"] = Json::array({ 1, 2, 3 });
    FakeSave::block = input.dump();
    FakeSave::hasBlock = true;
    save.OnSaveLoaded();
    CHECK(gForkSave.ownedItems[0] == 1 && gForkSave.ownedItems[47] == 48);
    CHECK(gForkSave.bottleSlots[0] == 10 && gForkSave.bottleSlots[7] == 17);
    CHECK(gForkSave.caneSkills == 63 && gForkSave.trirodEchoesHi == 305419896u && gForkSave.pictoFlags0 == 17);
    CHECK(gRestoredLanternFire == 1 && gForkSave.lanternCapturedTypes == 2);
    const Json output = Json::parse(save.SerializeForTests());
    CHECK(output["ownedItems"].is_array() && output["ownedItems"].size() == 48 && output["ownedItems"][47] == 48);
    CHECK(output["bottleSlots"].is_array() && output["bottleSlots"].size() == 8 && output["caneSkills"] == 63);
    CHECK(output["pictoPhotoI5"].size() == sizeof(gForkSave.pictoPhotoI5));
    CHECK(output["pictoPhotoI5"][0] == 1 && output["pictoPhotoI5"][2] == 3);
    CHECK(output["wandRodsOwned"] == 63 && output["slateRunesOwned"] == 15 && output["seasonsOwned"] == 15);
    // Full captured image survives writing and reloading, including its final byte.
    std::memset(gForkSave.pictoPhotoI5, 255, sizeof(gForkSave.pictoPhotoI5));
    save.OnSaving();
    save.OnSaveLoaded();
    CHECK(gForkSave.pictoPhotoI5[0] == 255 && gForkSave.pictoPhotoI5[sizeof(gForkSave.pictoPhotoI5) - 1] == 255);

    // Um campo ruim volta ao inicial dele, sem impedir os seguintes de carregarem; array curto preenche o começo.
    FakeSave::block = "{\"ownedItems\":[1,\"x\"],\"bottleSlots\":\"nao\",\"caneSkills\":\"sim\","
                      "\"trirodEchoesHi\":7,\"trirodLayoutVersion\":2,\"season\":3}";
    save.OnSaveLoaded();
    CHECK(gForkSave.ownedItems[0] == 1 && gForkSave.ownedItems[1] == 0xFF && gForkSave.ownedItems[47] == 0xFF);
    CHECK(gForkSave.bottleSlots[0] == 0xFF && gForkSave.caneSkills == 0 && gForkSave.bottomlessContent == 0xFF);
    CHECK(gForkSave.trirodEchoesHi == 7 && gForkSave.season == 3);
    CHECK(gRestoredLanternFire == 0); // Loading a different save cannot retain the previous flame.

    // Máscara de ecos da tabela v1 é descartada, como no NeiSave_Load do fork.
    FakeSave::block = "{\"trirodEchoesLo\":5,\"trirodEchoesHi\":7,\"trirodSel\":2}";
    save.OnSaveLoaded();
    CHECK(gForkSave.trirodEchoesLo == 0 && gForkSave.trirodEchoesHi == 0 && gForkSave.trirodSel == 0);
    CHECK(gForkSave.trirodLayoutVersion == 2);
    FakeSave::block = "{\"trirodEchoesHi\":7,\"trirodLayoutVersion\":2}";
    save.OnSaveLoaded();

    const uint32_t writes = FakeSave::writes;
    save.OnSaving();
    CHECK(FakeSave::writes == writes + 1);
    CHECK(Json::parse(FakeSave::block)["trirodEchoesHi"] == 7);

    // Arquivo novo depois de outro carregado: sentinelas de novo, sem nada do arquivo anterior.
    FakeSave::block = input.dump();
    FakeSave::hasBlock = true;
    save.OnSaveLoaded();
    CHECK(gForkSave.ownedItems[0] == 1 && gForkSave.bottleSlots[0] == 10);
    save.ResetForNewSlot();
    CHECK(gForkSave.ownedItems[0] == 0xFF && gForkSave.bottleSlots[0] == 0xFF && save.StoredVersion() == 0);

    // Bloco de uma versão futura: o arquivo roda com os sentinelas e o save não regrava por cima dele.
    FakeSave::block = "{\"caneSkills\":9}";
    FakeSave::hasBlock = true;
    FakeSave::storedVersion = 2;
    save.OnSaveLoaded();
    CHECK(gForkSave.caneSkills == 0 && save.StoredVersion() == 2);
    const uint32_t kept = FakeSave::writes;
    save.OnSaving();
    CHECK(FakeSave::writes == kept && FakeSave::block == "{\"caneSkills\":9}");
    FakeSave::storedVersion = 1;
    save.Detach();
}

} // namespace

int main() {
    TestValidation();
    TestGiveUseAndLevels();
    TestSaveRoundTrip();
    TestForkSaveRoundTrip();
    if (gFailures) {
        std::fprintf(stderr, "nei core: %d falhas\n", gFailures);
        return EXIT_FAILURE;
    }
    std::printf("nei core: ok\n");
    return EXIT_SUCCESS;
}
