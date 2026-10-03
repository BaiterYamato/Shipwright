cmake_minimum_required(VERSION 3.26)
project(LinkSpanUnboundFactory LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OOT_NATIVE_SDK "" CACHE FILEPATH "OotNativeSdk.cmake gerado pelo host")
if(NOT EXISTS "${OOT_NATIVE_SDK}")
    message(FATAL_ERROR "Informe -DOOT_NATIVE_SDK=<build-do-host>/native-sdk/Release/OotNativeSdk.cmake")
endif()
include("${OOT_NATIVE_SDK}")

add_library(linkspan_unbound_core SHARED unbound_core.cpp json_merge.cpp scene_registry.cpp room_actors.cpp
    unbound_format.cpp transcode.cpp converter.cpp unbound_docs.cpp actor_registry.cpp)
add_library(linkspan_unbound_consumer SHARED unbound_consumer.cpp)
add_executable(linkspan_unbound_json_tests tests.cpp json_merge.cpp scene_registry.cpp room_actors.cpp unbound_format.cpp actor_registry.cpp)
add_executable(linkspan_unbound_format_tests format_tests.cpp unbound_format.cpp transcode.cpp unbound_docs.cpp actor_registry.cpp)
add_executable(linkspan_unbound_convert converter_cli.cpp converter.cpp unbound_format.cpp transcode.cpp unbound_docs.cpp
    json_merge.cpp scene_registry.cpp actor_registry.cpp)
add_executable(linkspan_unbound_converter_tests converter_tests.cpp converter.cpp unbound_format.cpp transcode.cpp actor_registry.cpp)
add_executable(linkspan_unbound_actor_tests actor_tests.cpp actor_registry.cpp transcode.cpp unbound_format.cpp room_actors.cpp)
# Demo do registro de cenas; reaproveita a busca de assets/ do exemplo Dynamic Movement.
add_library(linkspan_unbound_scene_demo SHARED scene-demo/scene_demo.cpp ../example/package_assets.cpp)
target_include_directories(linkspan_unbound_scene_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../example")
# Demo do patch de atores de sala (slice D1): mesmo código da demo de cenas, com outro destino e assets.
add_library(linkspan_unbound_field_demo SHARED scene-demo/scene_demo.cpp ../example/package_assets.cpp)
target_include_directories(linkspan_unbound_field_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../example")
# Fixtures da fase E (cenas JSON, texto, mundo amplo, materialAnims): roteiro de viagens por hotkey.
add_library(linkspan_unbound_e_fixtures SHARED e-fixtures/e_fixtures.cpp ../example/package_assets.cpp)
target_include_directories(linkspan_unbound_e_fixtures PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../example")

foreach(target linkspan_unbound_core linkspan_unbound_consumer linkspan_unbound_json_tests linkspan_unbound_format_tests
               linkspan_unbound_actor_tests
               linkspan_unbound_converter_tests linkspan_unbound_convert
               linkspan_unbound_scene_demo
               linkspan_unbound_field_demo
               linkspan_unbound_e_fixtures)
    target_include_directories(${target} PRIVATE ${OOT_NATIVE_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${target} PRIVATE ${OOT_NATIVE_COMPILE_DEFINITIONS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /Zc:preprocessor)
    endif()
endforeach()

set(provider_dir "${CMAKE_BINARY_DIR}/mod/provider")
foreach(target linkspan_unbound_core linkspan_unbound_consumer linkspan_unbound_scene_demo linkspan_unbound_field_demo
               linkspan_unbound_e_fixtures)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${provider_dir}")
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" upper)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${provider_dir}")
    endforeach()
endforeach()

enable_testing()
add_test(NAME linkspan_unbound_json_tests COMMAND linkspan_unbound_json_tests)
add_test(NAME linkspan_unbound_format_tests COMMAND linkspan_unbound_format_tests)
add_test(NAME linkspan_unbound_converter_tests COMMAND linkspan_unbound_converter_tests)
add_test(NAME linkspan_unbound_actor_tests COMMAND linkspan_unbound_actor_tests)
