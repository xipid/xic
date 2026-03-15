cmake_minimum_required(VERSION 3.14)
project(Xi VERSION 1.0.0 LANGUAGES C CXX)

option(XI_BUILD_GRAPHICS "Build Graphics Support (Diligent Engine)" OFF)

add_library(Xi 
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/monocypher/monocypher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Cert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Crypto.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Regex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/String.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Primitives.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Math.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/File.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Spatial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Time.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Random.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Spatial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/MPU.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/GPS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/DHT.cpp
)

if(XI_BUILD_GRAPHICS)
    target_sources(Xi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Graphics.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Xi/Camera.cpp
    )
endif()


target_include_directories(Xi PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(Xi PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/monocypher
)
target_compile_features(Xi PUBLIC cxx_std_17)

add_library(Xi::Xi ALIAS Xi)
