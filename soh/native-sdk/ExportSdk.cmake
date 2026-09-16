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
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeSave.cpp")
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
    "${CMAKE_SOURCE_DIR}/soh/soh/native/OotNativeScenes.cpp")
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
