cmake_minimum_required(VERSION 3.14)
project(Xi VERSION 1.0.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-fno-sanitize=all)
add_link_options(-fno-sanitize=all)

# Options
option(XI_BUILD_GRAPHICS "Build Graphics Support (Diligent Engine)" ON)
option(XI_BUILD_HARDWARE "Build Hardware Support (DHT/MPU/GPS)" ON)
option(XI_BUILD_TLS "Build TLS/HTTP Support (mbedTLS)" ON)
option(XI_BUILD_DEFLATE "Build DEFLATE Compression (miniz)" ON)
option(XI_BUILD_LZ4 "Build LZ4 Compression" ON)
option(XI_BUILD_ZSTD "Build Zstandard Compression" ON)
option(XI_BUILD_AS_OBJECTS "Build as an OBJECT library" ON)

# --- Automatic Source Discovery ---
# Core components that are always included
file(GLOB_RECURSE CORE_SOURCES 
    "packages/monocypher/monocypher.c"
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
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/monocypher
)

# --- Fail-safe Dependency Handling ---

# Graphics (Diligent Engine)
if(XI_BUILD_GRAPHICS)
    if(TARGET Diligent-GraphicsEngineVk-static OR TARGET Diligent-GraphicsEngineOpenGL-static)
        set(XI_DILIGENT_FOUND TRUE)
        if(DEFINED diligentcore_SOURCE_DIR)
            target_include_directories(Xi PUBLIC ${diligentcore_SOURCE_DIR})
        endif()
    else()
        find_path(DILIGENT_CORE_INTERFACE_DIR NAMES CommandList.h
                  PATH_SUFFIXES DiligentCore/Graphics/GraphicsEngine/interface
                                Graphics/GraphicsEngine/interface)
        if(DILIGENT_CORE_INTERFACE_DIR)
            set(XI_DILIGENT_FOUND TRUE)
            get_filename_component(DILIGENT_CORE_ROOT "${DILIGENT_CORE_INTERFACE_DIR}/../../.." ABSOLUTE)
            target_include_directories(Xi PUBLIC
                ${DILIGENT_CORE_ROOT}
                ${DILIGENT_CORE_ROOT}/Graphics/GraphicsEngine/interface
                ${DILIGENT_CORE_ROOT}/Common/interface
                ${DILIGENT_CORE_ROOT}/Primitives/interface
                ${DILIGENT_CORE_ROOT}/Platforms/interface
            )
        endif()
    endif()

    if(XI_DILIGENT_FOUND)
        file(GLOB GRAPHICS_SOURCES "src/Graphics/*.cpp")
        target_sources(Xi PRIVATE ${GRAPHICS_SOURCES})
        target_compile_definitions(Xi PUBLIC XI_GRAPHICS_ENABLED)
        
        if(TARGET glfw)
            target_link_libraries(Xi PUBLIC glfw)
        else()
            find_package(glfw3 QUIET)
            if(glfw3_FOUND)
                target_link_libraries(Xi PUBLIC glfw)
            endif()
        endif()
    else()
        message(STATUS "Diligent Engine not found. Skipping Graphics.")
    endif()
endif()

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

# Compile internal guest binary using musl-clang
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tests/task/internal"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/task"
    COMMAND musl-clang -x c -O2 -fPIE -pie -Wl,-Ttext-segment=0x50000000 -Wl,-Ttext=0x50001000 -Wl,-e,main -o "${CMAKE_CURRENT_BINARY_DIR}/tests/task/internal" "${CMAKE_CURRENT_SOURCE_DIR}/tests/task/internal.cpp"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/task/internal.cpp"
    COMMENT "Compiling guest binary internal using musl-clang"
)
add_custom_target(internal ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/tests/task/internal")

# Build runner host executable (which loads the internal binary)
add_executable(runner tests/task/runner.cpp)
target_link_libraries(runner PRIVATE Xi::Xi)
set_target_properties(runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/task")

# Build example (realistic developer use-case test)
add_executable(example tests/task/example.cpp)
target_link_libraries(example PRIVATE Xi::Xi)
set_target_properties(example PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/task")

# Build main (functional unit test suite)
add_executable(main tests/task/main.cpp)
target_link_libraries(main PRIVATE Xi::Xi)
set_target_properties(main PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/task")

