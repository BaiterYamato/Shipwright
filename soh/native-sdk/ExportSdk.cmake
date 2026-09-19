# SDK local de headers; o mod é um projeto separado e não linka o jogo.
# O fingerprint cobre apenas a superfície real da ABI nativa: as structs z64
# expostas pelo serviço engine (soh/include), os contratos de serviço
# (soh/soh/native) e a ABI do SDK (shiplua/native). Headers internos de
# soh/src, soh/soh e libultraship não atravessam a fronteira C e não podem
# invalidar mods já compilados.
file(GLOB_RECURSE layout_headers CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/soh/include/*.h"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/*.h"
    "${LINKSPAN_SDK_SOURCE_DIR}/include/shiplua/native/*.h")
list(SORT layout_headers)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${layout_headers})
set(layout_material "oot-native-v2;${CMAKE_SIZEOF_VOID_P};${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_GENERATOR_PLATFORM};${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_RELEASE}")
foreach(header IN LISTS layout_headers)
    file(SHA256 "${header}" header_hash)
    string(APPEND layout_material "${header_hash}")
endforeach()
string(SHA256 layout_id "${layout_material}")
set(native_sdk_dir "${CMAKE_BINARY_DIR}/native-sdk")
file(MAKE_DIRECTORY "${native_sdk_dir}")
file(WRITE "${native_sdk_dir}/oot_layout_id.h"
    "#pragma once\n#define LINKSPAN_OOT_LAYOUT_ID \"${layout_id}\"\n")
target_include_directories(soh PRIVATE "${native_sdk_dir}")
file(GENERATE OUTPUT "${native_sdk_dir}/$<CONFIG>/OotNativeSdk.cmake" CONTENT
"set(OOT_NATIVE_INCLUDE_DIRS [==[$<TARGET_PROPERTY:soh,INCLUDE_DIRECTORIES>;${CMAKE_SOURCE_DIR}/soh/soh/native]==])
set(OOT_NATIVE_COMPILE_DEFINITIONS [==[$<TARGET_PROPERTY:soh,COMPILE_DEFINITIONS>]==])
")

add_executable(oot_native_engine_tests
    "${CMAKE_SOURCE_DIR}/soh/native-sdk/tests/EngineTests.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeEngine.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeScenes.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeHooks.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeSave.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeItems.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeView.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeWorld.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeSkeletons.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeText.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeJsonTypes.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeEscape.cpp")
target_include_directories(oot_native_engine_tests PRIVATE
    "$<TARGET_PROPERTY:soh,INCLUDE_DIRECTORIES>"
    "${CMAKE_SOURCE_DIR}/soh/soh/native")
target_compile_definitions(oot_native_engine_tests PRIVATE "$<TARGET_PROPERTY:soh,COMPILE_DEFINITIONS>")
target_link_libraries(oot_native_engine_tests PRIVATE shiplua)
if(MSVC)
    set_property(TARGET oot_native_engine_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME oot_native_engine_tests COMMAND oot_native_engine_tests)

add_executable(oot_native_registry_tests
    "${CMAKE_SOURCE_DIR}/soh/native-sdk/tests/RegistryTests.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeRegistry.cpp")
target_include_directories(oot_native_registry_tests PRIVATE
    "$<TARGET_PROPERTY:soh,INCLUDE_DIRECTORIES>"
    "${CMAKE_SOURCE_DIR}/soh/soh/native")
target_compile_definitions(oot_native_registry_tests PRIVATE "$<TARGET_PROPERTY:soh,COMPILE_DEFINITIONS>")
target_link_libraries(oot_native_registry_tests PRIVATE shiplua)
if(MSVC)
    set_property(TARGET oot_native_registry_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME oot_native_registry_tests COMMAND oot_native_registry_tests)

add_executable(oot_native_scenes_tests
    "${CMAKE_SOURCE_DIR}/soh/native-sdk/tests/SceneRegistryTests.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeScenes.cpp"
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeJsonTypes.cpp")
target_include_directories(oot_native_scenes_tests PRIVATE
    "$<TARGET_PROPERTY:soh,INCLUDE_DIRECTORIES>"
    "${CMAKE_SOURCE_DIR}/soh/soh/native")
target_compile_definitions(oot_native_scenes_tests PRIVATE "$<TARGET_PROPERTY:soh,COMPILE_DEFINITIONS>")
target_link_libraries(oot_native_scenes_tests PRIVATE shiplua)
if(MSVC)
    set_property(TARGET oot_native_scenes_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME oot_native_scenes_tests COMMAND oot_native_scenes_tests)
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_test(NAME oot_native_independent_mod COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/soh/native-sdk/tests/test_independent.py"
        --probe $<TARGET_FILE:oot_native_engine_tests> --host $<TARGET_FILE:soh>
        --sdk "${native_sdk_dir}/$<CONFIG>/OotNativeSdk.cmake"
        --example "${CMAKE_SOURCE_DIR}/soh/native-sdk/example"
        --output "${CMAKE_BINARY_DIR}/native-packages" --cmake "${CMAKE_COMMAND}")
endif()

# Escape hatch (COREEXT-008, RFC 0023): MinHook v1.3.4 compilada direto das fontes, sem o
# CMakeLists dela (que mexe em BUILD_SHARED_LIBS e instala), e soh.symbols gerado do PDB.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    include(FetchContent)
    FetchContent_Declare(linkspan_minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
        SOURCE_SUBDIR linkspan-sem-cmake)
    FetchContent_MakeAvailable(linkspan_minhook)
    add_library(linkspan_minhook STATIC
        "${linkspan_minhook_SOURCE_DIR}/src/buffer.c"
        "${linkspan_minhook_SOURCE_DIR}/src/hook.c"
        "${linkspan_minhook_SOURCE_DIR}/src/trampoline.c"
        "${linkspan_minhook_SOURCE_DIR}/src/hde/hde64.c")
    target_include_directories(linkspan_minhook
        PUBLIC "${linkspan_minhook_SOURCE_DIR}/include"
        PRIVATE "${linkspan_minhook_SOURCE_DIR}/src" "${linkspan_minhook_SOURCE_DIR}/src/hde")
    get_target_property(soh_runtime soh MSVC_RUNTIME_LIBRARY)
    if(soh_runtime)
        set_property(TARGET linkspan_minhook PROPERTY MSVC_RUNTIME_LIBRARY "${soh_runtime}")
    endif()
    target_link_libraries(soh PRIVATE linkspan_minhook psapi)

    add_executable(linkspan_symdump "${CMAKE_SOURCE_DIR}/soh/native-sdk/tools/symdump.cpp")
    target_compile_features(linkspan_symdump PRIVATE cxx_std_17)
    target_link_libraries(linkspan_symdump PRIVATE dbghelp bcrypt)
    # Fora do ALL: rode antes de empacotar ou testar o escape hatch.
    add_custom_target(soh_symbols
        COMMAND linkspan_symdump "$<TARGET_FILE:soh>" "$<TARGET_FILE_DIR:soh>/soh.symbols"
        DEPENDS soh linkspan_symdump
        VERBATIM)
endif()
