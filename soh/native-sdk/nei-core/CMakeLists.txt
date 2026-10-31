cmake_minimum_required(VERSION 3.26)
project(LinkSpanNeiCore LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OOT_NATIVE_SDK "" CACHE FILEPATH "OotNativeSdk.cmake gerado pelo host")
if(NOT EXISTS "${OOT_NATIVE_SDK}")
    message(FATAL_ERROR "Informe -DOOT_NATIVE_SDK=<build-do-host>/native-sdk/Release/OotNativeSdk.cmake")
endif()
include("${OOT_NATIVE_SDK}")

# Código de itens do fork NEI (fase F): sai do commit fixo pelo fork/sync.py e chama o host pelo escape hatch.
get_filename_component(host_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(NEI_HOST_SYMBOLS "${host_root}/x64/Release/soh.symbols" CACHE FILEPATH "soh.symbols do host alvo")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(nei_fork_dir "${CMAKE_BINARY_DIR}/nei-fork")
set(fork_src "${CMAKE_CURRENT_SOURCE_DIR}/fork")
file(GLOB nei_fork_patches CONFIGURE_DEPENDS "${fork_src}/patches/*.patch")
# Mudanças nos fontes mesclados também precisam disparar um novo configure.
file(STRINGS "${host_root}/soh/hookable_sources.txt" nei_overlay_host_sources ENCODING UTF-8 REGEX "^[ \t]*[^# \t]")
foreach(nei_overlay_host_source IN LISTS nei_overlay_host_sources)
    string(STRIP "${nei_overlay_host_source}" nei_overlay_host_source)
    if(nei_overlay_host_source AND NOT nei_overlay_host_source MATCHES "^#")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${host_root}/soh/${nei_overlay_host_source}")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${fork_src}/sync.py" "${fork_src}/gen_compat.py" "${fork_src}/extract_additions.py" "${fork_src}/extract.txt"
    "${fork_src}/gen_stubs.py" "${fork_src}/stub-names.txt" "${fork_src}/extracted-fixes.txt"
    "${fork_src}/gen_item_models.py" "${fork_src}/cmodels.py" "${fork_src}/overlay.py" "${fork_src}/overlay-exclude.txt"
    "${host_root}/soh/hookable_sources.txt" "${NEI_HOST_SYMBOLS}" ${nei_fork_patches})
# As resoluções à mão do overlay valem por assinatura das três versões; trocar uma refaz a extração.
file(GLOB nei_overlay_resolutions CONFIGURE_DEPENDS "${fork_src}/overlay-merges/*.c" "${fork_src}/overlay-extra/*.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${nei_overlay_resolutions})
# Função do overlay em conflito fica sem desvio; por padrão o configure falha. ON só avisa (host em obras).
option(NEI_ALLOW_OVERLAY_CONFLICTS "Extrair o fork NEI mesmo com funções do overlay em conflito" OFF)
set(nei_sync_flags "")
if(NEI_ALLOW_OVERLAY_CONFLICTS)
    set(nei_sync_flags "--permitir-conflitos")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${fork_src}/sync.py" "${host_root}" "${nei_fork_dir}" "${NEI_HOST_SYMBOLS}"
    ${nei_sync_flags} RESULT_VARIABLE nei_sync_result)
if(nei_sync_result EQUAL 3)
    message(FATAL_ERROR "fork/sync.py: funções do overlay em conflito (lista acima); resolva em fork/overlay-merges/ "
        "ou configure com -DNEI_ALLOW_OVERLAY_CONFLICTS=ON")
elseif(NOT nei_sync_result EQUAL 0)
    message(FATAL_ERROR "fork/sync.py falhou; confira se o commit do fork está no git do host")
endif()
# Funções que o fork acrescentou em arquivos do host; a do z_player.c (extracted/unit) entra no player_unit.c.
file(GLOB nei_fork_extracted "${nei_fork_dir}/extracted/*.c")

add_library(nei_fork OBJECT "${fork_src}/player_unit.c" "${fork_src}/actor_guard.c" "${fork_src}/host_additions.c"
    "${fork_src}/inventory_unit.c" "${fork_src}/fork_items.c" "${fork_src}/progression.c"
    "${fork_src}/kaleido_shim.c" "${fork_src}/pipeline_probe.c" "${fork_src}/stubs.c" "${fork_src}/nei_save_bridge.c"
    "${fork_src}/resource_guard.c" "${fork_src}/backend_unit.c"
    "${fork_src}/world_unit.cpp"
    "${nei_fork_dir}/fork/soh/mods/items/custom_bottles.cpp"
    "${nei_fork_dir}/fork/soh/mods/items/objects/object_tornado.cpp"
    "${nei_fork_dir}/nei_stubs.c" "${nei_fork_dir}/nei_item_models.c" "${nei_fork_dir}/nei_cmodels.c"
    "${nei_fork_dir}/nei_overlays.c" ${nei_fork_extracted})
target_include_directories(nei_fork PRIVATE "${fork_src}" "${nei_fork_dir}" "${nei_fork_dir}/compat"
    "${nei_fork_dir}/compat/src" "${nei_fork_dir}/fork/soh" "${nei_fork_dir}/fork/soh/mods" ${OOT_NATIVE_INCLUDE_DIRS})
target_compile_definitions(nei_fork PRIVATE ${OOT_NATIVE_COMPILE_DEFINITIONS})
# C4047/C4024/C4090/C4133: o jogo passa caminhos de asset como ponteiros de recurso e Player* como Actor*.
# C4013 é erro: função sem protótipo volta como int e trunca ponteiro ou perde f32 (nei_fork_protos.h).
target_compile_options(nei_fork PRIVATE /utf-8 /W3 /wd4047 /wd4024 /wd4090 /wd4133 /wd4244 /wd4005 /bigobj
    /we4013 /Zc:preprocessor /std:c17 /FI${fork_src}/fork_cpp_compat.h)

# Thunks das funções e ponteiros das variáveis do host que os objetos do fork pedem (gen_imports.py).
# O .asm fica na raiz do build: o ml64 do MSBuild não cria a subpasta do objeto (nei-fork/) como o cl cria.
set(nei_imports_asm "${CMAKE_BINARY_DIR}/nei_host_imports.asm")
set(nei_imports_c "${nei_fork_dir}/nei_host_imports.c")
add_custom_command(OUTPUT "${nei_imports_asm}" "${nei_imports_c}"
    COMMAND "${Python3_EXECUTABLE}" "${fork_src}/gen_imports.py"
        "--obj;$<JOIN:$<TARGET_OBJECTS:nei_fork>,;--obj;>"
        --symbols "${NEI_HOST_SYMBOLS}" --out-asm "${nei_imports_asm}" --out-c "${nei_imports_c}"
        --report "${nei_fork_dir}/nei_host_imports.txt"
    DEPENDS nei_fork $<TARGET_OBJECTS:nei_fork> "${fork_src}/gen_imports.py" "${NEI_HOST_SYMBOLS}"
    COMMAND_EXPAND_LISTS VERBATIM)

add_library(linkspan_nei_core SHARED nei_core.cpp assets.cpp registry.cpp attribute_source.cpp text.cpp fork/fork_glue.cpp fork/backend_glue.cpp fork/kaleido_glue.cpp
    fork/overlay_glue.cpp fork_save.cpp
    $<TARGET_OBJECTS:nei_fork> "${nei_imports_asm}" "${nei_imports_c}")
# O .map traduz o RVA que o crash handler do host mostra (a DLL não leva PDB) na função do fork.
target_link_options(linkspan_nei_core PRIVATE /MAP)
add_library(linkspan_nei_demo SHARED demo/nei_demo.cpp)
add_executable(linkspan_nei_core_tests tests.cpp registry.cpp attribute_source.cpp fork_save.cpp "${fork_src}/nei_save_bridge.c")
add_executable(linkspan_nei_lantern_tests lantern_light_tests.cpp)
add_executable(linkspan_nei_extended_tests extended_items_tests.cpp)
# Extrai corpos reais para fixtures pequenas, como o teste de Roc's Cape abaixo.
# Os limites nomeados falham no configure se um fonte deixar de ter o trecho esperado.
# Quarto argumento opcional: outro .inc de saída (trecho de dentro de uma função).
function(nei_test_section path first last)
    set(out "rc11_inventory_under_test.inc")
    if(ARGC GREATER 3)
        set(out "${ARGV3}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" body)
    string(FIND "${body}" "${first}" begin)
    string(FIND "${body}" "${last}" end)
    if(begin LESS 0 OR end LESS begin)
        message(FATAL_ERROR "Trecho de regressão RC11 não encontrado: ${path}: ${first}")
    endif()
    math(EXPR size "${end} - ${begin}")
    string(SUBSTRING "${body}" ${begin} ${size} body)
    file(APPEND "${CMAKE_BINARY_DIR}/${out}" "${body}\n")
endfunction()
file(WRITE "${CMAKE_BINARY_DIR}/rc11_inventory_under_test.inc" "")
file(WRITE "${CMAKE_BINARY_DIR}/rc11_boots_migration.inc" "")
set(rc11_mods "${nei_fork_dir}/fork/soh/mods")
nei_test_section("${rc11_mods}/extended_inventory.h" "// RC11: keep deferred" "// Skijer's NEI — item-action")
nei_test_section("${rc11_mods}/extended_inventory.c" "uint16_t gExtInvSessionItems[72];" "// Page 2 item layout")
nei_test_section("${rc11_mods}/extended_inventory.c" "uint8_t Wand_ModeOwned(" "uint8_t Wand_ModeNeighbor(")
# A DLL compila o nei_save.c extraído do blob do fork (com o extracted-fixes), não o nei_save.cpp.
set(rc11_nei_save "${nei_fork_dir}/extracted/nei_save.c")
nei_test_section("${rc11_nei_save}" "#define NEI_CANE_SKILL_MAX" "/* Prototipos locais")
nei_test_section("${rc11_nei_save}" "extern uint8_t Nei_CaneHasSkill(uint8_t skill) {"
    "extern uint8_t Nei_TrirodEchoLearned(uint8_t idx) {")
nei_test_section("${rc11_mods}/items/logic/item_cane_of_somaria.c" "u8 Cane_GiveSkill(" "u8 Cane_GiveByExtItem(")
nei_test_section("${rc11_mods}/extended_equipment.c" "    // Migrate the old boots layout" "    // Clamp to valid range"
    rc11_boots_migration.inc)
nei_test_section("${rc11_mods}/extended_equipment.c" "static void ExtEquip_SetCurrentByType(" "// Ownership")
nei_test_section("${rc11_mods}/extended_equipment.c" "static u32 ExtEquip_GetBit(" "// The single writer")
nei_test_section("${rc11_mods}/extended_equipment.c" "void ExtEquip_SetSlot(" "// Child<->adult")
nei_test_section("${rc11_mods}/extended_equipment.c" "void ExtEquip_RemoveItem(" "// Upgrade-column passives")
nei_test_section("${fork_src}/backend_unit.c" "static const char* kEquipmentNames" "// Player_Update remains")
nei_test_section("${fork_src}/backend_unit.c" "u8 NeiBottles_WheelsEnabled(" "u8 NeiBottles_UpdateItem(")
nei_test_section("${fork_src}/inventory_unit.c" "static uint8_t LogicalSlot(" "int NeiInv_HasItem(")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${fork_src}/progression.c")
file(READ "${fork_src}/progression.c" rc11_progress)
string(REGEX REPLACE "#include[^\n]*\n" "" rc11_progress "${rc11_progress}")
file(APPEND "${CMAKE_BINARY_DIR}/rc11_inventory_under_test.inc" "${rc11_progress}\n")
file(WRITE "${CMAKE_BINARY_DIR}/rc11_inventory_constants.inc" "")
foreach(path "${nei_fork_dir}/compat/nei_compat.h" "${rc11_mods}/extended_equipment.h" "${rc11_mods}/extended_inventory.h")
    file(STRINGS "${path}" rc11_defines REGEX "^#define (ITEM_|SLOT_|EXT_ITEM_)")
    foreach(line IN LISTS rc11_defines)
        file(APPEND "${CMAKE_BINARY_DIR}/rc11_inventory_constants.inc" "${line}\n")
    endforeach()
endforeach()
file(STRINGS "${rc11_mods}/items/logic/item_cane_of_somaria.h" rc11_defines
     REGEX "^#define CANE_SKILL_(SOMARIA|PACCI|MAX|TYPE|SLOT|BIT)")
file(STRINGS "${rc11_mods}/extended_equipment.c" rc11_pendant REGEX "^#define TRADE_ADULT_PENDANT ")
list(APPEND rc11_defines ${rc11_pendant})
foreach(line IN LISTS rc11_defines)
    file(APPEND "${CMAKE_BINARY_DIR}/rc11_inventory_constants.inc" "${line}\n")
endforeach()
add_executable(linkspan_nei_inventory_controls_tests inventory_controls_tests.cpp
    "${rc11_mods}/items/custom_bottles.cpp")
target_include_directories(linkspan_nei_inventory_controls_tests PRIVATE "${CMAKE_BINARY_DIR}"
    "${nei_fork_dir}/compat" "${nei_fork_dir}/fork/soh")
# Exercita o handler real já patchado; remove somente seus includes, cujas
# dependências são fixtures delimitadas no teste (sem outra implementação).
file(READ "${nei_fork_dir}/fork/soh/mods/items/logic/item_rocscape.c" rocs_test_source)
string(REGEX REPLACE "#include[^\n]*\n" "" rocs_test_source "${rocs_test_source}")
file(WRITE "${CMAKE_BINARY_DIR}/rocscape_under_test.inc" "${rocs_test_source}")
add_executable(linkspan_nei_motion_visuals_tests motion_visuals_tests.cpp)
target_include_directories(linkspan_nei_motion_visuals_tests PRIVATE "${CMAKE_BINARY_DIR}")
add_executable(linkspan_nei_backend_tests backend_tests.cpp "${nei_fork_dir}/fork/soh/mods/items/custom_bottles.cpp")
target_include_directories(linkspan_nei_backend_tests PRIVATE "${nei_fork_dir}/compat" "${nei_fork_dir}/fork/soh")

foreach(target linkspan_nei_core linkspan_nei_demo linkspan_nei_core_tests linkspan_nei_lantern_tests linkspan_nei_extended_tests linkspan_nei_backend_tests linkspan_nei_inventory_controls_tests)
    target_include_directories(${target} PRIVATE ${OOT_NATIVE_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${target} PRIVATE ${OOT_NATIVE_COMPILE_DEFINITIONS})
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/Zc:preprocessor>")
    endif()
endforeach()

# O teste liga somente a ponte do save, com Nei_Save falso; não puxa o objeto inteiro do fork nem seus imports.
target_include_directories(linkspan_nei_core_tests PRIVATE "${nei_fork_dir}" "${nei_fork_dir}/compat"
    "${nei_fork_dir}/compat/src" "${nei_fork_dir}/fork/soh" "${nei_fork_dir}/fork/soh/mods")
set_target_properties(linkspan_nei_core_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES)

set(provider_dir "${CMAKE_BINARY_DIR}/mod/provider")
foreach(target linkspan_nei_core linkspan_nei_demo)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${provider_dir}")
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" upper)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${provider_dir}")
    endforeach()
endforeach()

enable_testing()
add_test(NAME linkspan_nei_core_tests COMMAND linkspan_nei_core_tests)
add_test(NAME linkspan_nei_lantern_tests COMMAND linkspan_nei_lantern_tests)
add_test(NAME linkspan_nei_extended_tests COMMAND linkspan_nei_extended_tests)
add_test(NAME linkspan_nei_backend_tests COMMAND linkspan_nei_backend_tests)
add_test(NAME linkspan_nei_inventory_controls_tests COMMAND linkspan_nei_inventory_controls_tests)
add_test(NAME linkspan_nei_motion_visuals_tests COMMAND linkspan_nei_motion_visuals_tests)
# readapt.py refaz resoluções do overlay quando só o host mudou (repositórios git temporários, sem rede).
add_test(NAME linkspan_nei_readapt_tests COMMAND "${Python3_EXECUTABLE}" -B -m unittest test_readapt
         WORKING_DIRECTORY "${fork_src}")
