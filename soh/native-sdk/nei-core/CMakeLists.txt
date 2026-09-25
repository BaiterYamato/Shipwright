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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${fork_src}/sync.py" "${fork_src}/gen_compat.py" "${fork_src}/extract_additions.py" "${fork_src}/extract.txt"
    "${fork_src}/gen_stubs.py" "${fork_src}/stub-names.txt" "${fork_src}/extracted-fixes.txt"
    "${fork_src}/gen_item_models.py" "${fork_src}/cmodels.py" "${fork_src}/overlay.py" "${fork_src}/overlay-exclude.txt"
    "${host_root}/soh/hookable_sources.txt" "${NEI_HOST_SYMBOLS}" ${nei_fork_patches})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${fork_src}/sync.py" "${host_root}" "${nei_fork_dir}" "${NEI_HOST_SYMBOLS}"
    RESULT_VARIABLE nei_sync_result)
if(NOT nei_sync_result EQUAL 0)
    message(FATAL_ERROR "fork/sync.py falhou; confira se o commit do fork está no git do host")
endif()
# Funções que o fork acrescentou em arquivos do host; a do z_player.c (extracted/unit) entra no player_unit.c.
file(GLOB nei_fork_extracted "${nei_fork_dir}/extracted/*.c")

add_library(nei_fork OBJECT "${fork_src}/player_unit.c" "${fork_src}/actor_guard.c" "${fork_src}/host_additions.c"
    "${fork_src}/inventory_unit.c" "${fork_src}/fork_items.c"
    "${fork_src}/kaleido_shim.c" "${fork_src}/pipeline_probe.c" "${fork_src}/stubs.c" "${fork_src}/nei_save_bridge.c"
    "${nei_fork_dir}/nei_stubs.c" "${nei_fork_dir}/nei_item_models.c" "${nei_fork_dir}/nei_cmodels.c"
    "${nei_fork_dir}/nei_overlays.c" ${nei_fork_extracted})
target_include_directories(nei_fork PRIVATE "${fork_src}" "${nei_fork_dir}" "${nei_fork_dir}/compat"
    "${nei_fork_dir}/compat/src" "${nei_fork_dir}/fork/soh" "${nei_fork_dir}/fork/soh/mods" ${OOT_NATIVE_INCLUDE_DIRS})
target_compile_definitions(nei_fork PRIVATE ${OOT_NATIVE_COMPILE_DEFINITIONS})
# C4047/C4024/C4090/C4133: o jogo passa caminhos de asset como ponteiros de recurso e Player* como Actor*.
# C4013 é erro: função sem protótipo volta como int e trunca ponteiro ou perde f32 (nei_fork_protos.h).
target_compile_options(nei_fork PRIVATE /utf-8 /W3 /wd4047 /wd4024 /wd4090 /wd4133 /wd4244 /wd4005 /bigobj
    /we4013 /Zc:preprocessor /std:c17 "/FI${nei_fork_dir}/compat/nei_compat.h" "/FI${fork_src}/nei_host_overrides.h"
    "/FI${fork_src}/nei_fork_protos.h")

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

add_library(linkspan_nei_core SHARED nei_core.cpp assets.cpp registry.cpp fork/fork_glue.cpp fork/kaleido_glue.cpp
    fork/overlay_glue.cpp fork_save.cpp
    $<TARGET_OBJECTS:nei_fork> "${nei_imports_asm}" "${nei_imports_c}")
# O .map traduz o RVA que o crash handler do host mostra (a DLL não leva PDB) na função do fork.
target_link_options(linkspan_nei_core PRIVATE /MAP)
add_library(linkspan_nei_demo SHARED demo/nei_demo.cpp)
add_executable(linkspan_nei_core_tests tests.cpp registry.cpp fork_save.cpp "${fork_src}/nei_save_bridge.c")

foreach(target linkspan_nei_core linkspan_nei_demo linkspan_nei_core_tests)
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
