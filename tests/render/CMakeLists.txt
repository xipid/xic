cmake_minimum_required(VERSION 3.30)

set(OpenGL_GL_PREFERENCE GLVND)
cmake_policy(SET CMP0072 NEW)
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

# 1. Force Clang
find_program(CLANG_EXECUTABLE clang++)
if(NOT CLANG_EXECUTABLE)
    message(FATAL_ERROR "Clang++ not found!")
endif()
set(CMAKE_CXX_COMPILER ${CLANG_EXECUTABLE})
set(CMAKE_C_COMPILER clang)

# 2. Project Setup
project(XiRenderTest LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

# 3. Diligent Options
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "Disable D3D11" FORCE)
set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "Disable D3D12" FORCE)
set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "Disable Samples" FORCE)
set(DILIGENT_BUILD_TOOLS OFF CACHE BOOL "Disable Tools" FORCE)
set(DILIGENT_NO_FORMATTING ON CACHE BOOL "" FORCE)
set(DILIGENT_CLANG_TIDY OFF CACHE BOOL "" FORCE)

# 4. Linker Configuration
find_program(MOLD_PROGRAM mold)
if(MOLD_PROGRAM)
    add_link_options(-fuse-ld=mold)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-march=native -stdlib=libc++)
add_link_options(-stdlib=libc++)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -flto=thin)
    add_link_options(-flto=thin)
else()
    add_compile_options(-O1 -g)
endif()

include(FetchContent)

# --- Dependencies ---

# Xi (local, two levels up)
set(XI_BUILD_GRAPHICS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  Xi
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
FetchContent_MakeAvailable(Xi)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# Diligent Engine
FetchContent_Declare(
    DiligentEngine
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentEngine
    GIT_TAG        master
)
FetchContent_MakeAvailable(DiligentEngine)

# --- Application ---

add_executable(render_test main.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bindings.cpp "#include <Xi/Graphics.hpp>\n#include <Xi/Window.hpp>\n#include <Xi/Mesh.hpp>\n#include <Xi/Camera.hpp>\n")
add_library(xi_render SHARED ${CMAKE_CURRENT_BINARY_DIR}/bindings.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(render_test PRIVATE PLATFORM_LINUX=1)
    target_compile_definitions(xi_render PRIVATE PLATFORM_LINUX=1)
    target_compile_definitions(Xi PUBLIC PLATFORM_LINUX=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(render_test PRIVATE PLATFORM_WIN32=1)
    target_compile_definitions(xi_render PRIVATE PLATFORM_WIN32=1)
    target_compile_definitions(Xi PUBLIC PLATFORM_WIN32=1)
endif()

# Linking
set(LINK_LIBS
    Xi::Xi
    Diligent-GraphicsEngineVk-static
    Diligent-GraphicsEngineOpenGL-static
    Diligent-Common
    glfw
    GL
    GLX
    X11
)

target_link_libraries(render_test PRIVATE ${LINK_LIBS})

# For Python cppyy to be able to see all Diligent and Xi symbols, we need to force
# the linker to include all symbols from the static libraries into the shared library.
target_link_libraries(xi_render PRIVATE 
    -Wl,--whole-archive
    Xi::Xi
    Diligent-GraphicsEngineVk-static
    Diligent-GraphicsEngineOpenGL-static
    Diligent-Common
    -Wl,--no-whole-archive
    glfw
    GL
    GLX
    X11
)

# Includes (Pass to Xi so it can compile its new .cpp files)
target_include_directories(Xi PUBLIC
    ${DiligentCore_SOURCE_DIR}/Graphics/GraphicsEngine/interface
    ${DiligentCore_SOURCE_DIR}/Graphics/GraphicsEngineVulkan/interface
    ${DiligentCore_SOURCE_DIR}/Graphics/GraphicsEngineOpenGL/interface
    ${DiligentCore_SOURCE_DIR}/Common/interface
    ${DiligentCore_SOURCE_DIR}/Primitives/interface
    ${DiligentCore_SOURCE_DIR}/Platforms/interface
    ${DiligentCore_SOURCE_DIR}/Platforms/Linux/interface
)
