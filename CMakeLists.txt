cmake_minimum_required(VERSION 3.14)
project(Xi VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(XI_BUILD_GRAPHICS "Build Graphics Support (Diligent Engine)" ON)
option(XI_BUILD_HARDWARE "Build Hardware Support (DHT/MPU/GPS)" ON)
option(XI_BUILD_TLS "Build TLS/HTTP Support (mbedTLS)" ON)
option(XI_BUILD_DEFLATE "Build DEFLATE Compression (miniz)" ON)
option(XI_BUILD_LZ4 "Build LZ4 Compression" ON)
option(XI_BUILD_ZSTD "Build Zstandard Compression" ON)

# Source Groups
set(XI_CORE_SOURCES
    packages/monocypher/monocypher.c
    src/Xi/Device.cpp
    src/Xi/Math.cpp
    src/Xi/Primitives.cpp
    src/Xi/Random.cpp
    src/Xi/Time.cpp
    src/Xi/Transform.cpp
)

set(XI_COLLECTION_SOURCES
    src/Collection/String.cpp
)

set(XI_ENCODING_SOURCES
    src/Encoding/Regex.cpp
    src/Encoding/Yaml.cpp
)

set(XI_EXECUTION_SOURCES
    src/Execution/Routine.cpp
)

set(XI_LLT_SOURCES
    src/LLT/Crypto.cpp
)

set(XI_RESOURCE_SOURCES
    src/Resource/Archive.cpp
    src/Resource/File.cpp
    src/Resource/Network.cpp
    src/Resource/Socket.cpp
)

set(XI_TERMINAL_SOURCES
    src/Terminal/Command.cpp
    src/Terminal/Format.cpp
    src/Terminal/Prompt.cpp
)

set(XI_HARDWARE_SOURCES
    src/Hardware/DHT.cpp
    src/Hardware/GPS.cpp
    src/Hardware/GS.cpp
    src/Hardware/MPU.cpp
)

set(XI_TLS_SOURCES
    src/Resource/TLS.cpp
    src/Resource/HTTP.cpp
)

set(GRAPHICS_SOURCES
    src/Graphics/Camera.cpp
    src/Graphics/Graphics.cpp
    src/Graphics/Window.cpp
)

# Main Library
add_library(Xi 
    ${XI_CORE_SOURCES} 
    ${XI_COLLECTION_SOURCES}
    ${XI_ENCODING_SOURCES}
    ${GRAPHICS_SOURCES}
    #${XI_EXECUTION_SOURCES}
    ${XI_LLT_SOURCES}
    ${XI_RESOURCE_SOURCES}
    ${XI_TERMINAL_SOURCES}
)

if(XI_BUILD_HARDWARE)
    target_sources(Xi PRIVATE ${XI_HARDWARE_SOURCES})
endif()

# Graphics: Auto-detect Diligent & GLFW (FetchContent or system)
if(XI_BUILD_GRAPHICS)
    # Check if DiligentCore was already fetched (FetchContent)
    if(TARGET Diligent-GraphicsEngineVk-static OR TARGET Diligent-GraphicsEngineOpenGL-static)
        message(STATUS "Diligent Engine detected (FetchContent target). Enabling Graphics.")
        set(XI_DILIGENT_FOUND TRUE)

        # Auto-wire Diligent include directories from FetchContent source dir
        if(DEFINED diligentcore_SOURCE_DIR)
            target_include_directories(Xi PUBLIC ${diligentcore_SOURCE_DIR})
        endif()

    else()
        # Fallback: search for installed Diligent headers
        find_path(DILIGENT_CORE_INTERFACE_DIR NAMES CommandList.h
                  PATH_SUFFIXES DiligentCore/Graphics/GraphicsEngine/interface
                                Graphics/GraphicsEngine/interface)
        if(DILIGENT_CORE_INTERFACE_DIR)
            message(STATUS "Diligent Engine found at ${DILIGENT_CORE_INTERFACE_DIR}. Enabling Graphics.")
            set(XI_DILIGENT_FOUND TRUE)
            get_filename_component(DILIGENT_CORE_ROOT "${DILIGENT_CORE_INTERFACE_DIR}/../../.." ABSOLUTE)
            target_include_directories(Xi PUBLIC
                ${DILIGENT_CORE_ROOT}
                ${DILIGENT_CORE_ROOT}/Graphics/GraphicsEngine/interface
                ${DILIGENT_CORE_ROOT}/Graphics/GraphicsEngineVulkan/interface
                ${DILIGENT_CORE_ROOT}/Graphics/GraphicsEngineOpenGL/interface
                ${DILIGENT_CORE_ROOT}/Common/interface
                ${DILIGENT_CORE_ROOT}/Primitives/interface
                ${DILIGENT_CORE_ROOT}/Platforms/interface
                ${DILIGENT_CORE_ROOT}/Platforms/Linux/interface
                ${DILIGENT_CORE_ROOT}/Platforms/Basic/interface
            )
        else()
            message(WARNING "Diligent Engine not found. Skipping Graphics.")
            set(XI_DILIGENT_FOUND FALSE)
        endif()
    endif()

    if(XI_DILIGENT_FOUND)
        target_sources(Xi PRIVATE ${GRAPHICS_SOURCES})
        target_compile_definitions(Xi PUBLIC XI_GRAPHICS_ENABLED)

        # Platform definitions
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(Xi PUBLIC PLATFORM_LINUX=1)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            target_compile_definitions(Xi PUBLIC PLATFORM_WIN32=1)
        endif()

        # Vulkan support
        if(TARGET Diligent-GraphicsEngineVk-static)
            target_compile_definitions(Xi PUBLIC VULKAN_SUPPORTED=1)
            target_link_libraries(Xi PUBLIC
                Diligent-GraphicsEngineVk-static
                Diligent-Common
                Diligent-Primitives
            )
        endif()

        # GLFW: auto-detect target (from FetchContent or system)
        if(TARGET glfw)
            message(STATUS "GLFW detected. Linking to Xi.")
            target_link_libraries(Xi PUBLIC glfw)
            # Ensure Xi can find glfw3native.h
            if(DEFINED glfw_SOURCE_DIR)
                target_include_directories(Xi PRIVATE ${glfw_SOURCE_DIR}/include)
            endif()
        else()
            find_package(glfw3 QUIET)
            if(glfw3_FOUND)
                target_link_libraries(Xi PUBLIC glfw)
            endif()
        endif()
    endif()
endif()

# TLS/HTTP check (Reliant on mbedTLS presence)
if(XI_BUILD_TLS)
    # Search for the mbedTLS ssl.h header
    find_path(MBEDTLS_INCLUDE_DIR NAMES mbedtls/ssl.h)

    if(MBEDTLS_INCLUDE_DIR)
        message(STATUS "mbedTLS found at ${MBEDTLS_INCLUDE_DIR}. Enabling TLS/HTTP.")

        find_library(MBEDTLS_LIBRARY NAMES mbedtls)
        find_library(MBEDX509_LIBRARY NAMES mbedx509)
        find_library(MBEDCRYPTO_LIBRARY NAMES mbedcrypto)

        target_sources(Xi PRIVATE ${XI_TLS_SOURCES})
        target_compile_definitions(Xi PUBLIC XI_TLS_ENABLED)

        target_include_directories(Xi PUBLIC ${MBEDTLS_INCLUDE_DIR})

        if(MBEDTLS_LIBRARY AND MBEDX509_LIBRARY AND MBEDCRYPTO_LIBRARY)
            target_link_libraries(Xi PUBLIC
                ${MBEDTLS_LIBRARY}
                ${MBEDX509_LIBRARY}
                ${MBEDCRYPTO_LIBRARY}
            )
        endif()
    else()
        message(WARNING "mbedTLS not found (mbedtls/ssl.h missing). Skipping TLS/HTTP part.")
    endif()
endif()

find_path(MINIZ_INCLUDE_DIR NAMES miniz.h PATH_SUFFIXES miniz)
include_directories(${MINIZ_INCLUDE_DIR})

# DEFLATE check (Reliant on miniz presence)
if(XI_BUILD_DEFLATE)
    find_path(MINIZ_INCLUDE_DIR NAMES miniz.h)

    if(MINIZ_INCLUDE_DIR)
        message(STATUS "miniz found at ${MINIZ_INCLUDE_DIR}. Enabling DEFLATE.")

        target_sources(Xi PRIVATE src/LLT/Deflate.cpp src/Resource/ZIPArchive.cpp)
        target_compile_definitions(Xi PUBLIC XI_DEFLATE_ENABLED)
        target_include_directories(Xi PUBLIC ${MINIZ_INCLUDE_DIR})

        find_library(MINIZ_LIBRARY NAMES miniz)
        if(MINIZ_LIBRARY)
            target_link_libraries(Xi PUBLIC ${MINIZ_LIBRARY})
        endif()
    else()
        message(WARNING "miniz not found (miniz.h missing). Skipping DEFLATE.")
    endif()
endif()

# LZ4 check (Reliant on lz4 presence)
if(XI_BUILD_LZ4)
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h)

    if(LZ4_INCLUDE_DIR)
        message(STATUS "LZ4 found at ${LZ4_INCLUDE_DIR}. Enabling LZ4.")

        target_sources(Xi PRIVATE src/LLT/LZ4.cpp)
        target_compile_definitions(Xi PUBLIC XI_LZ4_ENABLED)
        target_include_directories(Xi PUBLIC ${LZ4_INCLUDE_DIR})

        find_library(LZ4_LIBRARY NAMES lz4)
        if(LZ4_LIBRARY)
            target_link_libraries(Xi PUBLIC ${LZ4_LIBRARY})
        endif()
    else()
        message(WARNING "LZ4 not found (lz4.h missing). Skipping LZ4.")
    endif()
endif()

# ZSTD check (Reliant on libzstd presence)
if(XI_BUILD_ZSTD)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)

    if(ZSTD_INCLUDE_DIR)
        message(STATUS "Zstandard found at ${ZSTD_INCLUDE_DIR}. Enabling ZSTD.")

        target_sources(Xi PRIVATE src/LLT/Zstd.cpp)
        target_compile_definitions(Xi PUBLIC XI_ZSTD_ENABLED)
        target_include_directories(Xi PUBLIC ${ZSTD_INCLUDE_DIR})

        find_library(ZSTD_LIBRARY NAMES zstd)
        if(ZSTD_LIBRARY)
            target_link_libraries(Xi PUBLIC ${ZSTD_LIBRARY})
        endif()
    else()
        message(WARNING "Zstandard not found (zstd.h missing). Skipping ZSTD.")
    endif()
endif()

# Include Directories
target_include_directories(Xi PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(Xi PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/monocypher
)

# Alias
add_library(Xi::Xi ALIAS Xi)

# Run Example / Test
#add_executable(test_xic tests/test_xic.cpp)
#target_link_libraries(test_xic PRIVATE Xi)
