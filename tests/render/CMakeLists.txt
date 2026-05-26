cmake_minimum_required(VERSION 3.14)
project(render_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# --- MOLD LINKER ---
execute_process(COMMAND mold -v RESULT_VARIABLE MOLD_PRESENT OUTPUT_QUIET)
if(MOLD_PRESENT EQUAL 0)
    add_link_options("-fuse-ld=mold")
    message(STATUS "Using mold linker.")
endif()

include(FetchContent)

# 1. Fetch Diligent Core
FetchContent_Declare(
  DiligentCore
  GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(DiligentCore)

# 2. Fetch GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 3. Fetch XiC — everything else is automatic
FetchContent_Declare(xic SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../)
FetchContent_MakeAvailable(xic)

# 4. Build
add_executable(render_test main.cpp)
target_link_libraries(render_test PRIVATE Xi)
