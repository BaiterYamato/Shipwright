#include "bindings.h"
#include "oot_layout_id.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
__declspec(dllimport) void* __stdcall LoadLibraryA(const char* path);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
__declspec(dllimport) int __stdcall FreeLibrary(void* module);
}

namespace {
int failures = 0;
int checks = 0;
int registered = 0;
int removed = 0;
int killed = 0;
int animated = 0;
int drawn = 0;
int childDrawn = 0;
bool mmBase = false;
bool resource = false;
bool failSecond = false;
bool missingSymbol = false;
bool throwSkeleton = false;
ShipOotActorTypeSpecV1 specs[2]{};
ShipNativeFunction ids = nullptr;
void* idsUser = nullptr;
RomFile objects[OBJECT_ID_MAX]{};
s16 linkObjects[2] = { 0, 2 };
FlexSkeletonHeader* skeletons[2]{};
uintptr_t segments[16]{};

void Check(bool value, const char* message) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FALHA: %s\n", message);
    }
}

void Scale(Actor* actor, f32 scale) {
    actor->scale = { scale, scale, scale };
}
s32 Dma(void* ram, uintptr_t, size_t size, const char*, s32) {
    std::memset(ram, 0, size);
    return 0;
}
void InitSkeleton(PlayState*, SkelAnime* state, FlexSkeletonHeader*, LinkAnimationHeader*, s32, Vec3s* joints,
                  Vec3s* morph, s32) {
    if (throwSkeleton) {
        throw 1;
    }
    state->jointTable = joints;
    state->morphTable = morph;
}
s32 Animate(PlayState*, SkelAnime*) {
    ++animated;
    return 0;
}
void ChildDraw(PlayState*, void**, Vec3s*, s32, s32, s32, s32, s32, OverrideLimbDrawOpa, PostLimbDrawOpa, void*) {
    ++childDrawn;
}
void Setup(GraphicsContext*) {
}
void SetupPlay(PlayState*) {
}
void Segment(void*, int, uintptr_t) {
}
void Open(const void*, int) {
}
void Close() {
}
s32 Override(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*) {
    return 0;
}

ShipNativeStatus SHIP_NATIVE_CALL Kill(void*) {
    ++killed;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Register(const ShipOotActorTypeSpecV1* spec, int16_t* id) {
    if (failSecond && registered == 1) {
        return SHIP_NATIVE_FAILURE;
    }
    if (registered >= 2) {
        return SHIP_NATIVE_LIMIT;
    }
    specs[registered] = *spec;
    *id = static_cast<int16_t>(1000 + registered++);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Remove(int16_t) {
    ++removed;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Draw(void*, const char* path, uint8_t layer) {
    Check(std::strcmp(path, "mm/objects/gameplay_keep/gElegyShellHumanDL") == 0 && layer == 0,
          "estátua usa serviço genérico e caminho sem __OTR__");
    ++drawn;
    return SHIP_NATIVE_OK;
}
uint8_t SHIP_NATIVE_CALL HasFile(const char*) {
    return resource;
}
ShipOotActorsV1 actors{ sizeof(actors), Register, Remove, nullptr, Draw };
ShipOotEngineV1 engine{ sizeof(engine),
                        LINKSPAN_OOT_LAYOUT_ID,
                        sizeof(PlayState),
                        sizeof(Player),
                        sizeof(SaveContext),
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        Kill };
ShipOotResourcesV1 resources{ sizeof(resources), HasFile };
uint32_t audioSize = sizeof(uint32_t);

const void* SHIP_NATIVE_CALL Service(void*, const char* name, uint32_t version, uint32_t size) {
    if (version != 1)
        return nullptr;
    if (!std::strcmp(name, LINKSPAN_OOT_ACTORS_SERVICE) && size <= sizeof(actors))
        return &actors;
    if (!std::strcmp(name, LINKSPAN_OOT_ENGINE_SERVICE) && size <= sizeof(engine))
        return &engine;
    if (!std::strcmp(name, LINKSPAN_OOT_RESOURCES_SERVICE) && size <= sizeof(resources))
        return &resources;
    if (!std::strcmp(name, "linkspan.mm.audio") && mmBase && size <= sizeof(audioSize))
        return &audioSize;
    return nullptr;
}
ShipNativeStatus SHIP_NATIVE_CALL Resolve(void*, const char* name, uintptr_t* address) {
    *address = 0;
    if (missingSymbol)
        return SHIP_NATIVE_UNSUPPORTED;
#define SYMBOL(n, v)                               \
    if (!std::strcmp(name, n)) {                   \
        *address = reinterpret_cast<uintptr_t>(v); \
        return SHIP_NATIVE_OK;                     \
    }
    SYMBOL("Actor_SetScale", Scale);
    SYMBOL("DmaMgr_SendRequest1", Dma);
    SYMBOL("Gfx_SetupDL_25Opa", Setup);
    SYMBOL("LinkAnimation_Update", Animate);
    SYMBOL("Player_DrawImpl", ChildDraw);
    SYMBOL("SkelAnime_InitLink", InitSkeleton);
    SYMBOL("func_80093C80", SetupPlay);
    SYMBOL("gSPSegment", Segment);
    SYMBOL("gLinkObjectIds", linkObjects);
    SYMBOL("gObjectTable", objects);
    SYMBOL("gPlayerSkelHeaders", skeletons);
    SYMBOL("gSegments", segments);
    SYMBOL("Player_OverrideLimbDrawPause", Override);
    SYMBOL("FrameInterpolation_RecordOpenChild", Open);
    SYMBOL("FrameInterpolation_RecordCloseChild", Close);
#undef SYMBOL
    return SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL RegisterFunction(void*, const char* name, ShipNativeFunction function, void* user) {
    Check(!std::strcmp(name, "actor_ids"), "função local actor_ids");
    ids = function;
    idsUser = user;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Write(void* user, const char* bytes, uint32_t length) {
    static_cast<std::string*>(user)->append(bytes, length);
    return SHIP_NATIVE_OK;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    void* module = LoadLibraryA(argv[1]);
    if (!module)
        return 3;
    auto query = reinterpret_cast<ShipNativeQueryFn>(GetProcAddress(module, "ShipNative_Query"));
    if (!query)
        return 4;
    const auto* descriptor = query();
    Check(descriptor->abi_minor == 4, "ABI 1.4");
    ShipNativeRuntime runtime{};
    runtime.size = sizeof(runtime);
    runtime.abi_major = 1;
    runtime.abi_minor = 4;
    runtime.get_service = Service;
    runtime.resolve_symbol = Resolve;
    runtime.register_function = RegisterFunction;
    void* instance = nullptr;
    runtime.abi_minor = 3;
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_UNSUPPORTED && !instance, "ABI antiga recusada");
    runtime.abi_minor = 4;
    engine.layout_id = "outro-layout";
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_UNSUPPORTED, "layout divergente recusado");
    descriptor->shutdown(instance);
    engine.layout_id = LINKSPAN_OOT_LAYOUT_ID;
    missingSymbol = true;
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_UNSUPPORTED, "símbolo ausente recusa init");
    descriptor->shutdown(instance);
    missingSymbol = false;
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_OK, "inicializa sem MM Base");
    Check(registered == 2, "dois tipos registrados");
    Check(!std::strcmp(specs[0].name, "oot.link_child_puppet") && !std::strcmp(specs[1].name, "oot.mm_elegy_statue"),
          "nomes registrados preservados");
    Check(specs[0].instance_size == sizeof(Actor) && specs[0].object_id == OBJECT_GAMEPLAY_KEEP,
          "tipo usa só Actor e gameplay_keep");
    std::string json;
    Check(ids(idsUser, "", 0, Write, &json) == SHIP_NATIVE_OK && json.find("1000") != std::string::npos,
          "IDs disponíveis para o entrypoint Lua");
    void* duplicate = nullptr;
    Check(descriptor->init(&runtime, &duplicate) == SHIP_NATIVE_LIMIT && !duplicate, "instância duplicada recusada");
    auto play = std::make_unique<PlayState>();
    auto gfx = std::make_unique<GraphicsContext>();
    Gfx commands[8]{};
    gfx->polyOpa.p = commands;
    play->state.gfxCtx = gfx.get();
    Actor child{};
    objects[OBJECT_GAMEPLAY_KEEP].vromEnd = 64;
    objects[2].vromEnd = 32;
    segments[4] = 44;
    segments[6] = 66;
    specs[0].init(specs[0].user, &child, play.get());
    Check(killed == 0 && child.scale.x == 0.0064f, "Link criança independe de MM Base");
    Check(segments[4] == 44 && segments[6] == 66, "segmentos restaurados após init");
    specs[0].update(nullptr, &child, play.get());
    specs[0].draw(nullptr, &child, play.get());
    Check(animated == 1 && childDrawn == 1, "animação e desenho do Link criança pela ligação dinâmica");
    specs[0].destroy(nullptr, &child, play.get());
    specs[0].update(nullptr, &child, play.get());
    Check(animated == 1, "destroy remove estado do Link criança");
    throwSkeleton = true;
    specs[0].init(specs[0].user, &child, play.get());
    Check(killed == 1 && segments[4] == 44 && segments[6] == 66, "falha de carga restaura segmentos e mata ator");
    throwSkeleton = false;
    const int previousKills = killed;
    Actor statue{};
    resource = true;
    specs[1].init(specs[1].user, &statue, play.get());
    Check(killed == previousKills + 1, "recurso sozinho não autoriza estátua sem MM Base");
    mmBase = true;
    resource = false;
    specs[1].init(specs[1].user, &statue, play.get());
    Check(killed == previousKills + 2, "MM Base sozinho não autoriza recurso ausente");
    resource = true;
    specs[1].init(specs[1].user, &statue, play.get());
    specs[1].draw(specs[1].user, &statue, play.get());
    Check(killed == previousKills + 2 && drawn == 1 && statue.scale.x == 0.01f, "estátua com Base e recurso");
    mmBase = false;
    specs[1].draw(specs[1].user, &statue, play.get());
    specs[1].update(specs[1].user, &statue, play.get());
    Check(drawn == 1 && killed == previousKills + 3, "retirada de MM Base impede draw e mata a instância");
    specs[1].destroy(specs[1].user, &statue, play.get());
    descriptor->shutdown(instance);
    Check(removed == 2, "shutdown remove os dois tipos");
    registered = 0;
    removed = 0;
    failSecond = true;
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_FAILURE, "falha no segundo registro propagada");
    descriptor->shutdown(instance);
    Check(removed == 1, "init parcial remove o primeiro registro");
    failSecond = false;
    registered = 0;
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_OK, "reload após falha parcial");
    descriptor->shutdown(instance);
    descriptor->shutdown(nullptr);
    Check(FreeLibrary(module) != 0, "DLL descarrega após limpeza");
    std::printf("Bonecos: %d verificações, %d falhas (serviços simulados, sem jogo).\n", checks, failures);
    return failures ? 1 : 0;
}
