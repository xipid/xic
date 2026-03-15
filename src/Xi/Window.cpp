#include "../../include/Xi/Window.hpp"

#if GLFW_AVAILABLE

#if PLATFORM_LINUX
#undef GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#endif

namespace Xi {

GLFWDiligentScreen::GLFWDiligentScreen(GLFWwindow *gw) : _win(gw) {
  name = "Screen";
  outputIntended = true;
  writable = true;

  glfwGetFramebufferSize(_win, &width, &height);
  screenWidth = width;
  screenHeight = height;
  _lastW = width;
  _lastH = height;

#if PLATFORM_LINUX
  _swp.setDisp(glfwGetX11Display());
  _swp.setWin((void *)glfwGetX11Window(_win));
#endif
}

void GLFWDiligentScreen::update() {
  if (!_win)
    return;

  if (width != _lastW || height != _lastH) {
    glfwSetWindowSize(_win, width, height);
    _lastW = width;
    _lastH = height;
  }
  if (title != _lastTitle) {
    glfwSetWindowTitle(_win, title.c_str());
    _lastTitle = title;
  }

  if (!surface)
    return;

  void *windowRTV = _swp.getRTV();
  void *windowDSV = _swp.getDSV();

  if (windowRTV) {
    void *sceneSRV = surface->deviceView(0);
    if (sceneSRV) {
      gContext.bindResources(windowRTV, windowDSV, screenWidth, screenHeight);
      _swp.drawFullscreen(sceneSRV);
    }
    _swp.present();
  }
}

void GLFWDiligentScreen::resizeSwapchain(int w, int h) { _swp.resize(w, h); }

GLFWDiligentWindow::GLFWDiligentWindow() {
  name = "Xi Window";

#if PLATFORM_LINUX
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
  if (!glfwInit())
    return;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  _win = glfwCreateWindow(800, 600, "Xi Window", nullptr, nullptr);
  if (!_win)
    return;

  // Create rendering device
  auto *dev = new GLFWDiligentRenderingDevice();
  devices.push((Device *)dev);

  // Create screen
  _screen = new GLFWDiligentScreen(_win);
  _screen->renderingDevice = dev;
  devices.push((Device *)_screen);

  glfwSetWindowUserPointer(_win, this);

  glfwSetKeyCallback(_win, [](GLFWwindow *w, int k, int s, int a, int m) {
    auto *self = static_cast<GLFWDiligentWindow *>(glfwGetWindowUserPointer(w));
    if (!self)
      return;

    for (usz i = 0; i < self->devices.length(); ++i) {
      auto *key = dynamic_cast<Device1D *>(self->devices[i]);
      if (key && key->id == k) {
        key->value = (a != GLFW_RELEASE) ? 1.0f : 0.0f;
        return;
      }
    }

    if (a == GLFW_PRESS) {
      auto *key = new Device1D();
      key->id = k;
      key->name = "Key";
      key->value = 1.0f;
      key->readable = true;
      self->devices.push((Device *)key);
    }
  });

  glfwSetFramebufferSizeCallback(_win, [](GLFWwindow *w, int width,
                                          int height) {
    auto *self = static_cast<GLFWDiligentWindow *>(glfwGetWindowUserPointer(w));
    if (self && self->_screen) {
      self->_screen->screenWidth = width;
      self->_screen->screenHeight = height;
      self->_screen->resizeSwapchain(width, height);
    }
  });
}

GLFWDiligentWindow::~GLFWDiligentWindow() {
  for (usz i = 0; i < devices.length(); ++i)
    delete devices[i];
  if (_win)
    glfwDestroyWindow(_win);
  glfwTerminate();
}

void GLFWDiligentWindow::update() {
  glfwPollEvents();
  if (_win) {
    shouldRelease = glfwWindowShouldClose(_win);
  }
  for (usz i = 0; i < devices.length(); ++i)
    devices[i]->update();
}

DeviceScreen *GLFWDiligentWindow::screen() { return _screen; }

Device *requestWindow() { return new GLFWDiligentWindow(); }

} // namespace Xi

#endif // GLFW_AVAILABLE
