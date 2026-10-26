cmake_minimum_required(VERSION 3.26)
project(OotItemSource LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(OOT_NATIVE_SDK "" CACHE FILEPATH "OotNativeSdk.cmake gerado pelo host")
if(NOT EXISTS "${OOT_NATIVE_SDK}")
    message(FATAL_ERROR "Informe OOT_NATIVE_SDK gerado pelo host")
endif()
include("${OOT_NATIVE_SDK}")
if(NOT DEFINED OOT_NATIVE_LAYOUT_ID OR OOT_NATIVE_LAYOUT_ID STREQUAL "")
    message(FATAL_ERROR "SDK sem OOT_NATIVE_LAYOUT_ID")
endif()
find_path(LINKSPAN_CORE_INCLUDE_DIR linkspan/attributes/linkspan_attributes.h
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/ship-lua/include" ${OOT_NATIVE_INCLUDE_DIRS}
    DOC "Include do core com o contrato linkspan.attributes" REQUIRED)

add_library(oot_item_source SHARED provider.c oot_item_table.c)
target_include_directories(oot_item_source PRIVATE ${OOT_NATIVE_INCLUDE_DIRS}
    "${LINKSPAN_CORE_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(oot_item_source PRIVATE ${OOT_NATIVE_COMPILE_DEFINITIONS})
if(MSVC)
    target_compile_options(oot_item_source PRIVATE /utf-8 /Zc:preprocessor)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/oot_item_source_layout.h"
    "#define OOT_ITEM_SOURCE_LAYOUT_ID \"${OOT_NATIVE_LAYOUT_ID}\"\n")
set_target_properties(oot_item_source PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mod/provider"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mod/provider")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(oot_item_source PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/mod/provider"
        LIBRARY_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/mod/provider")
endforeach()
configure_file(main.lua "${CMAKE_BINARY_DIR}/mod/main.lua" COPYONLY)
configure_file(manifest.toml "${CMAKE_BINARY_DIR}/mod/manifest.toml" COPYONLY)
configure_file(attributes.toml "${CMAKE_BINARY_DIR}/mod/attributes.toml" COPYONLY)

enable_testing()
add_executable(oot_item_table_tests item_table_tests.c oot_item_table.c)
target_include_directories(oot_item_table_tests PRIVATE "${LINKSPAN_CORE_INCLUDE_DIR}")
if(MSVC)
    target_compile_options(oot_item_table_tests PRIVATE /utf-8)
endif()
add_test(NAME oot_item_table_tests COMMAND oot_item_table_tests)
