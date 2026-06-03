/**
 * @file main.cpp
 * @brief XiC Render Test — A self-contained cube with inline shaders.
 *
 * Demonstrates:
 *   - requestWindow() for GLFW + Diligent initialization
 *   - Mesh3 / Vertex for GPU geometry
 *   - Shader with inline HLSL (vertex + pixel)
 *   - Camera3 with scene-tree rendering
 *   - Concrete Renderable3 subclass as a scene-graph node
 *   - Delta-time rotation and orbiting camera
 *   - Quit on 'Q' via GLFW polling
 */

#define PLATFORM_WIN32 0
#define PLATFORM_LINUX 1

#include <Graphics/Camera.hpp>
#include <Graphics/Window.hpp>
#include <Xi/Math.hpp>
#include <Xi/Time.hpp>

using namespace Xi;
using namespace Collection;
using namespace Graphics;

// ─── Inline Shaders ──────────────────────────────────────────────────────────

static const char *VS_SOURCE = R"(
cbuffer Primitives {
    float4x4 g_MVP;
    float4x4 g_World;
};

struct VSInput {
    float3 Pos   : ATTRIB0;
    float2 UV    : ATTRIB1;
    float3 Norm  : ATTRIB2;
    uint4  Joints: ATTRIB3;
    float4 Wts   : ATTRIB4;
};

struct PSInput {
    float4 Pos   : SV_POSITION;
    float3 Norm  : NORMAL;
    float3 Color : COLOR;
};

void main(in VSInput vs, out PSInput ps) {
    ps.Pos   = mul(float4(vs.Pos, 1.0), g_MVP);
    ps.Norm  = mul(float4(vs.Norm, 0.0), g_World).xyz;
    ps.Color = abs(vs.Norm) * 0.5 + 0.5;
}
)";

static const char *PS_SOURCE = R"(
struct PSInput {
    float4 Pos   : SV_POSITION;
    float3 Norm  : NORMAL;
    float3 Color : COLOR;
};

float4 main(in PSInput ps) : SV_Target {
    float3 light = normalize(float3(1.0, 2.0, -1.5));
    float  NdotL = saturate(dot(normalize(ps.Norm), light));
    float3 col   = ps.Color * (0.3 + 0.7 * NdotL);
    return float4(col, 1.0);
}
)";

// ─── Concrete Scene Node ─────────────────────────────────────────────────────
//
// Renderable3 inherits TreeItem (pure virtual clone()) and Transform.
// We implement clone() and name storage directly — no TaggedTreeItem mixin,
// which would cause a diamond (two TreeItem bases).

struct SceneObject : public Renderable3 {
  String _name;

  String getName() const override { return _name; }
  void setName(const String &n) override { _name = n; }

  TreeItem *clone() const override {
    auto *c = new SceneObject();
    c->_name = _name;
    c->mesh = mesh;
    c->shader = shader;
    c->setPosition(getPosition());
    c->setRotation(getRotation());
    c->setScale(getScale());
    return c;
  }
};

// ─── Cube Geometry ───────────────────────────────────────────────────────────

static void buildCube(Mesh3 &mesh) {
  // 24 vertices (4 per face, unique normals)
  auto V = [](f32 x, f32 y, f32 z, f32 u, f32 v, f32 nx, f32 ny, f32 nz) {
    return Vertex{x, y, z, u, v, nx, ny, nz, {0, 0, 0, 0}, {0, 0, 0, 0}};
  };

  // Front  (+Z)
  mesh.vertices.push(V(-1, -1, 1, 0, 0, 0, 0, 1));
  mesh.vertices.push(V(1, -1, 1, 1, 0, 0, 0, 1));
  mesh.vertices.push(V(1, 1, 1, 1, 1, 0, 0, 1));
  mesh.vertices.push(V(-1, 1, 1, 0, 1, 0, 0, 1));
  // Back   (-Z)
  mesh.vertices.push(V(1, -1, -1, 0, 0, 0, 0, -1));
  mesh.vertices.push(V(-1, -1, -1, 1, 0, 0, 0, -1));
  mesh.vertices.push(V(-1, 1, -1, 1, 1, 0, 0, -1));
  mesh.vertices.push(V(1, 1, -1, 0, 1, 0, 0, -1));
  // Top    (+Y)
  mesh.vertices.push(V(-1, 1, 1, 0, 0, 0, 1, 0));
  mesh.vertices.push(V(1, 1, 1, 1, 0, 0, 1, 0));
  mesh.vertices.push(V(1, 1, -1, 1, 1, 0, 1, 0));
  mesh.vertices.push(V(-1, 1, -1, 0, 1, 0, 1, 0));
  // Bottom (-Y)
  mesh.vertices.push(V(-1, -1, -1, 0, 0, 0, -1, 0));
  mesh.vertices.push(V(1, -1, -1, 1, 0, 0, -1, 0));
  mesh.vertices.push(V(1, -1, 1, 1, 1, 0, -1, 0));
  mesh.vertices.push(V(-1, -1, 1, 0, 1, 0, -1, 0));
  // Right  (+X)
  mesh.vertices.push(V(1, -1, 1, 0, 0, 1, 0, 0));
  mesh.vertices.push(V(1, -1, -1, 1, 0, 1, 0, 0));
  mesh.vertices.push(V(1, 1, -1, 1, 1, 1, 0, 0));
  mesh.vertices.push(V(1, 1, 1, 0, 1, 1, 0, 0));
  // Left   (-X)
  mesh.vertices.push(V(-1, -1, -1, 0, 0, -1, 0, 0));
  mesh.vertices.push(V(-1, -1, 1, 1, 0, -1, 0, 0));
  mesh.vertices.push(V(-1, 1, 1, 1, 1, -1, 0, 0));
  mesh.vertices.push(V(-1, 1, -1, 0, 1, -1, 0, 0));

  // 36 indices (2 triangles per face)
  for (u32 face = 0; face < 6; ++face) {
    u32 b = face * 4;
    mesh.indices.push(b + 0);
    mesh.indices.push(b + 1);
    mesh.indices.push(b + 2);
    mesh.indices.push(b + 0);
    mesh.indices.push(b + 2);
    mesh.indices.push(b + 3);
  }

  mesh.dirty = true;
}

// ─── Entry Point ─────────────────────────────────────────────────────────────

int main() {
  // 1. Create Window
  GLFWDiligentWindow *win = requestWindow();
  if (!win)
    return -1;

  Screen *screen = win->screen();

  // 2. Build Geometry
  Mesh3 cubeMesh;
  buildCube(cubeMesh);

  // 3. Compile Shader
  Shader cubeShader;
  cubeShader.vertexSource = VS_SOURCE;
  cubeShader.pixelSource = PS_SOURCE;
  cubeShader.create();

  // 4. Create Scene Graph
  //    SceneObject is a concrete Renderable3 with TaggedTreeItem metadata.
  SceneObject cube;
  cube.mesh = &cubeMesh;
  cube.shader = &cubeShader;

  cube.setName("cube");

  TreeBranch sceneRoot;
  sceneRoot.add(&cube);

  // 5. Setup Camera
  Camera3 camera;
  camera.root = &sceneRoot;
  camera.device = screen->gpu;
  camera.fov = 60.0f;
  camera.setPosition({0.0f, 2.0f, -5.0f});
  camera.lookAt({0, 0, 0});

  // Link screen surface to camera
  screen->surface = &camera.surface;

  // 6. Main Loop
  i64 lastTime = millis();

  while (!win->shouldRelease) {
    i64 now = millis();
    f32 dt = (f32)(now - lastTime) / 1000.0f;
    lastTime = now;

    // Rotate Cube
    Vector3 r = cube.getRotation();
    r.y += 1.2f * dt;
    r.x += 0.4f * dt;
    cube.setRotation(r);

    // Orbit Camera
    f32 t = (f32)now / 1000.0f;
    camera.setPosition({sin(t) * 8.0f, 4.0f, cos(t) * 8.0f});
    camera.lookAt({0, 0, 0});

    // Sync camera surface dimensions with screen
    camera.surfaceWidth = screen->screenWidth;
    camera.surfaceHeight = screen->screenHeight;

    // Render and present
    camera.render();
    win->update();
  }

  // Detach before destruction (cube is stack-allocated, not owned by tree)
  sceneRoot.removeChild(&cube);

  return 0;
}
