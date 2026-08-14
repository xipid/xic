cmake_minimum_required(VERSION 3.14)
project(Xi VERSION 1.0.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

add_compile_options(-O3 -march=native -ffast-math -mavx2 -mfma -fno-sanitize=all)
if(OpenMP_CXX_FOUND)
    add_compile_options(${OpenMP_CXX_FLAGS})
    add_link_options(${OpenMP_CXX_FLAGS})
endif()
add_link_options(-fno-sanitize=all)

# Options
option(XI_BUILD_HARDWARE "Build Hardware Support (DHT/MPU/GPS)" ON)
option(XI_BUILD_TLS "Build TLS/HTTP Support (mbedTLS)" ON)
option(XI_BUILD_DEFLATE "Build DEFLATE Compression (miniz)" ON)
option(XI_BUILD_LZ4 "Build LZ4 Compression" ON)
option(XI_BUILD_ZSTD "Build Zstandard Compression" ON)
option(XI_BUILD_AS_OBJECTS "Build as an OBJECT library" ON)
# When ON, excludes src/Task/ from Xi so the consumer (e.g. task project)
# can provide its own Task implementation without duplicate symbols.
option(XI_EXCLUDE_TASK "Exclude src/Task sources from Xi (consumer provides Task)" OFF)

# --- Dependencies ---
include(FetchContent)
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
)
FetchContent_MakeAvailable(monocypher)

# --- Automatic Source Discovery ---
# Core components that are always included
file(GLOB_RECURSE CORE_SOURCES 
    "${monocypher_SOURCE_DIR}/src/monocypher.c"
    "src/Xi/*.cpp"
    "src/Collection/*.cpp"
    "src/Encoding/*.cpp"
    "src/Task/*.cpp"
    "src/Task/arch/*.S"
    "src/Security/*.cpp"
    "src/Resource/*.cpp"
    "src/Terminal/*.cpp"
)

# Filter out optional sources from the core list to avoid double-inclusion or missing dependencies
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Hardware/.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Graphics/.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Resource/TLS.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Resource/HTTP.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Resource/ZIPArchive.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Resource/Deflate.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Resource/LZ4.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/Resource/Zstd.cpp")

set(XI_SOURCES ${CORE_SOURCES})

# Exclude Task sources if the consumer provides its own implementation
if(XI_EXCLUDE_TASK)
    list(FILTER XI_SOURCES EXCLUDE REGEX "src/Task/.*")
    message(STATUS "XI_EXCLUDE_TASK=ON: src/Task sources excluded from Xi")
endif()

# --- Main Target Definition ---
if(XI_BUILD_AS_OBJECTS)
    add_library(Xi OBJECT ${XI_SOURCES})
    # If building as OBJECT, we need PIC for shared libs that might link to us
    set_target_properties(Xi PROPERTIES POSITION_INDEPENDENT_CODE ON)
else()
    add_library(Xi STATIC ${XI_SOURCES})
endif()

# Basic Include Directories
target_include_directories(Xi PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(Xi PRIVATE 
    ${monocypher_SOURCE_DIR}/src
)

# --- Fail-safe Dependency Handling ---

# Graphics (Vulkan & GLFW)
# Graphics disabled in core xic (moved to ray)

# TLS/HTTP (mbedTLS)
if(XI_BUILD_TLS)
    find_path(MBEDTLS_INCLUDE_DIR NAMES mbedtls/ssl.h)
    if(MBEDTLS_INCLUDE_DIR)
        message(STATUS "mbedTLS found. Enabling TLS/HTTP.")
        target_sources(Xi PRIVATE src/Resource/TLS.cpp src/Resource/HTTP.cpp)
        target_compile_definitions(Xi PUBLIC XI_TLS_ENABLED)
        target_include_directories(Xi PUBLIC ${MBEDTLS_INCLUDE_DIR})
        
        find_library(MBEDTLS_LIB NAMES mbedtls QUIET)
        find_library(MBEDX509_LIB NAMES mbedx509 QUIET)
        find_library(MBEDCRYPTO_LIB NAMES mbedcrypto QUIET)
        if(MBEDTLS_LIB AND MBEDX509_LIB AND MBEDCRYPTO_LIB)
            target_link_libraries(Xi PUBLIC ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
        endif()
    else()
        message(STATUS "mbedTLS not found. Skipping TLS/HTTP.")
    endif()
endif()

# DEFLATE (miniz)
if(XI_BUILD_DEFLATE)
    find_path(MINIZ_INCLUDE_DIR NAMES miniz.h PATH_SUFFIXES miniz)
    if(MINIZ_INCLUDE_DIR)
        message(STATUS "miniz found. Enabling DEFLATE.")
        target_sources(Xi PRIVATE src/Resource/Deflate.cpp src/Resource/ZIPArchive.cpp)
        target_compile_definitions(Xi PUBLIC XI_DEFLATE_ENABLED)
        target_include_directories(Xi PUBLIC ${MINIZ_INCLUDE_DIR})
        
        find_library(MINIZ_LIB NAMES miniz QUIET)
        if(MINIZ_LIB)
            target_link_libraries(Xi PUBLIC ${MINIZ_LIB})
        endif()
    else()
        message(STATUS "miniz not found. Skipping DEFLATE.")
    endif()
endif()

# LZ4
if(XI_BUILD_LZ4)
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h)
    if(LZ4_INCLUDE_DIR)
        message(STATUS "LZ4 found. Enabling LZ4.")
        target_sources(Xi PRIVATE src/Resource/LZ4.cpp)
        target_compile_definitions(Xi PUBLIC XI_LZ4_ENABLED)
        target_include_directories(Xi PUBLIC ${LZ4_INCLUDE_DIR})
        
        find_library(LZ4_LIB NAMES lz4 QUIET)
        if(LZ4_LIB)
            target_link_libraries(Xi PUBLIC ${LZ4_LIB})
        endif()
    else()
        message(STATUS "LZ4 not found. Skipping LZ4.")
    endif()
endif()

# ZSTD
if(XI_BUILD_ZSTD)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
    if(ZSTD_INCLUDE_DIR)
        message(STATUS "Zstandard found. Enabling ZSTD.")
        target_sources(Xi PRIVATE src/Resource/Zstd.cpp)
        target_compile_definitions(Xi PUBLIC XI_ZSTD_ENABLED)
        target_include_directories(Xi PUBLIC ${ZSTD_INCLUDE_DIR})
        
        find_library(ZSTD_LIB NAMES zstd QUIET)
        if(ZSTD_LIB)
            target_link_libraries(Xi PUBLIC ${ZSTD_LIB})
        endif()
    else()
        message(STATUS "Zstandard not found. Skipping ZSTD.")
    endif()
endif()

# Hardware
if(XI_BUILD_HARDWARE)
    file(GLOB HARDWARE_SOURCES "src/Hardware/*.cpp")
    target_sources(Xi PRIVATE ${HARDWARE_SOURCES})
endif()

# Task subsystem platform dependencies (Linux: pthread + rt for POSIX timers)
if(UNIX AND NOT APPLE AND NOT ESP_PLATFORM)
    find_library(PTHREAD_LIB pthread)
    find_library(RT_LIB rt)
    if(PTHREAD_LIB)
        target_link_libraries(Xi PUBLIC ${PTHREAD_LIB})
    endif()
    if(RT_LIB)
        target_link_libraries(Xi PUBLIC ${RT_LIB})
    endif()
endif()

# Alias for consistent usage
if(NOT TARGET Xi::Xi)
    add_library(Xi::Xi ALIAS Xi)
endif()

option(XI_BUILD_TESTS "Build Xi tests" ON)

if(XI_BUILD_TESTS)

    # Build tree_yaml_test (unit verification of tree nodes & YAML)
    add_executable(tree_yaml_test tests/tree_yaml_test.cpp)
    target_link_libraries(tree_yaml_test PRIVATE Xi::Xi )
    set_target_properties(tree_yaml_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
