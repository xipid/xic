/**
 * @file Window.hpp
 * @brief Window management and screen abstractions for the Xi Graphics module.

 */

#ifndef XI_GRAPHICS_WINDOW_HPP
#define XI_GRAPHICS_WINDOW_HPP

#if defined(__linux__) && !defined(PLATFORM_LINUX)
#define PLATFORM_LINUX 1
#endif
#if defined(_WIN32) && !defined(PLATFORM_WIN32)
#define PLATFORM_WIN32 1
#endif

#include "Screen.hpp"
#include <Input/Input.hpp>
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

#define Time X11Time

#if PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif PLATFORM_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#undef Time

/**
 * @namespace Graphics
 * @brief Contains rendering, windowing, and graphics primitives.
 */
namespace Graphics {

using namespace Xi;
using namespace Collection;

/**
 * @class GLFWDiligentScreen
 * @brief A screen implementation backed by a GLFW window and a Diligent swap
 * chain.
 */
class XI_EXPORT GLFWDiligentScreen : public Screen {
private:
  GLFWwindow *_win = nullptr; ///< Pointer to the GLFW window.
  SwapContext _swp;           ///< Diligent Engine swap context.

  i32 _lastW = 800;                ///< Last recorded width.
  i32 _lastH = 600;                ///< Last recorded height.
  String _lastTitle = "Xi Window"; ///< Last recorded window title.

public:
  /**
   * @brief Constructs a screen from an existing GLFW window.
   * @param gw The GLFW window pointer.
   */
  GLFWDiligentScreen(GLFWwindow *gw);
  virtual ~GLFWDiligentScreen() = default;

  /**
   * @brief Updates the screen state, polling events and managing the swap
   * chain.
   */
  void update() override;

  /**
   * @brief Resizes the underlying swap chain.
   * @param w New width.
   * @param h New height.
   */
  void resizeSwapchain(int w, int h);
};

/**
 * @class GLFWDiligentWindow
 * @brief A high-level device representing a window with a rendering context.
 */
class XI_EXPORT GLFWDiligentWindow {
private:
  GLFWwindow *_win = nullptr; ///< The underlying GLFW window.
  GLFWDiligentScreen *_screen =
      nullptr; ///< The screen associated with this window.
  GLFWDiligentRenderingDevice *_gpuDevice = nullptr;

public:
  bool shouldRelease = false;
  Collection::Array<Input::InputDevice *> inputs;

  /**
   * @brief Creates a new GLFW window and initializes the rendering context.
   */
  GLFWDiligentWindow();
  virtual ~GLFWDiligentWindow();

  /**
   * @brief Polls window events and updates the internal state.
   */
  void update();

  /**
   * @brief Retrieves the screen associated with this window.
   * @return Pointer to the Screen.
   */
  Screen *screen();
};

/**
 * @brief Factory function to request a new window.
 * @return A pointer to the created window.
 */
XI_EXPORT GLFWDiligentWindow *requestWindow();

} // namespace Graphics

#endif // GLFW_AVAILABLE
#endif // XI_GRAPHICS_WINDOW_HPP
