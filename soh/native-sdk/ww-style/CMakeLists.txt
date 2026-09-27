cmake_minimum_required(VERSION 3.26)
project(LinkSpanWindWakerStyle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OOT_NATIVE_SDK "" CACHE FILEPATH "OotNativeSdk.cmake gerado pelo host")
if(NOT EXISTS "${OOT_NATIVE_SDK}")
    message(FATAL_ERROR "Informe -DOOT_NATIVE_SDK=<build-do-host>/native-sdk/Release/OotNativeSdk.cmake")
endif()
include("${OOT_NATIVE_SDK}")

add_library(linkspan_ww_style SHARED
    src/ww_style.cpp
    src/settings.cpp
    src/toon.cpp
    src/fairy.cpp)
target_include_directories(linkspan_ww_style PRIVATE ${OOT_NATIVE_INCLUDE_DIRS})
target_compile_definitions(linkspan_ww_style PRIVATE ${OOT_NATIVE_COMPILE_DEFINITIONS})
if(MSVC)
    target_compile_options(linkspan_ww_style PRIVATE /utf-8 /Zc:preprocessor)
endif()
set(provider_dir "${CMAKE_BINARY_DIR}/mod/provider")
set_target_properties(linkspan_ww_style PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${provider_dir}")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(linkspan_ww_style PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${provider_dir}")
endforeach()

# Teste do parser do snapshot, sem host.
add_executable(linkspan_ww_style_tests tests/settings_tests.cpp src/settings.cpp)
target_include_directories(linkspan_ww_style_tests PRIVATE src)
if(MSVC)
    target_compile_options(linkspan_ww_style_tests PRIVATE /utf-8)
endif()
