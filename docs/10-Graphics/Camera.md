# 3D Scene Rendering

The **Camera3** class is the primary engine for rendering 3D scenes in the xic framework. It transforms a hierarchical `Tree` of objects into a 2D surface that can be displayed on screen.

---

## Precision Camera Setup

A camera requires a **Root** (the scene tree), a **Device** (the GPU interface), and a **Surface** (the output buffer).

```cpp
using namespace Graphics;

Camera3 camera;

// 1. Link to the scene
camera.root = &mySceneRoot;

// 2. Link to the hardware
camera.device = screen->gpu;

// 3. Link to the output window
screen->surface = &camera.surface;
```

---

## Controlling the View

`Camera3` inherits from `Transform`, providing a fluent API for positioning and orientation.

```cpp
camera.setPosition({0.0f, 5.0f, -10.0f});
camera.lookAt({0, 0, 0});
camera.fov = 60.0f;

// Sync surface dimensions with window size
camera.surfaceWidth = screen->screenWidth;
camera.surfaceHeight = screen->screenHeight;
```

---

## Renderables and Meshes

Objects in the scene are represented by **Renderable3** nodes. Every renderable points to a **Mesh** (the geometry) and a **Shader** (the material).

```cpp
Renderable3 *obj = new Renderable3();
obj->mesh = myMesh;
obj->shader = myShader;
obj->setPosition({2, 0, 0});

camera.root->add(obj);
```

---

## The Render Call

Rendering is synchronous and explicit. Call `render()` when your scene and camera parameters are ready for a frame update.

```cpp
// IMPORTANT: Perform the actual rendering
camera.render();
```

---

## Best Practices

1.  **Surface Syncing**: Re-assign `surfaceWidth` and `surfaceHeight` before every `render()` call if your window is resizable to maintain a sharp image.
2.  **Clipping Planes**: Adjust `clipStart` and `clipEnd` to prevent flickering (Z-fighting) in very large or very small scenes.
3.  **Root Management**: Ensure the `root` pointer points to a valid `TreeItem` that contains your `Renderable3` hierarchy.
