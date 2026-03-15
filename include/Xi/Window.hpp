#ifndef XI_WINDOW_HPP
#define XI_WINDOW_HPP

#if defined(__linux__) && !defined(PLATFORM_LINUX)
#define PLATFORM_LINUX 1
#endif
#if defined(_WIN32) && !defined(PLATFORM_WIN32)
#define PLATFORM_WIN32 1
#endif

#include "Device.hpp"
#include "Graphics.hpp"

// -------------------------------------------------------------------------
// GLFW availability check
// -------------------------------------------------------------------------
#ifdef __has_include
#if __has_include(<GLFW/glfw3.h>)
#define GLFW_AVAILABLE 1
#else
#define GLFW_AVAILABLE 0
#endif
#else
#define GLFW_AVAILABLE 0
#endif

#if GLFW_AVAILABLE

#if PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif PLATFORM_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Xi {

// -------------------------------------------------------------------------
// GLFWDiligentScreen — DeviceScreen backed by GLFW + Diligent swap chain
// -------------------------------------------------------------------------
class XI_EXPORT GLFWDiligentScreen : public DeviceScreen {
  GLFWwindow *_win = nullptr;
  SwapContext _swp;

  i32 _lastW = 800;
  i32 _lastH = 600;
  String _lastTitle = "Xi Window";

public:
  GLFWDiligentScreen(GLFWwindow *gw);
  ~GLFWDiligentScreen() = default;

  void update() override;
  void resizeSwapchain(int w, int h);
};

// -------------------------------------------------------------------------
// GLFWDiligentWindow — Device managing GLFW screen + Diligent device + inputs
// -------------------------------------------------------------------------
class XI_EXPORT GLFWDiligentWindow : public Device {
  GLFWwindow *_win = nullptr;
  GLFWDiligentScreen *_screen = nullptr;

public:
  GLFWDiligentWindow();
  ~GLFWDiligentWindow();

  void update() override;
  DeviceScreen *screen();
};

Device *requestWindow();

} // namespace Xi

#endif // GLFW_AVAILABLE
#endif // XI_WINDOW_HPP
