// Xi Render Test — Spinning Cube
// Uses the Xi library abstractions: GLDIL (Window + IO), Camera3, Renderable3

#include "Xi/Camera.hpp"
#include "Xi/Graphics.hpp"
#include "Xi/Mesh.hpp"
#include "Xi/Primitives.hpp"
#include "Xi/Shader.hpp"
#include "Xi/Time.hpp"
#include "Xi/Tree.hpp"
#include "Xi/Window.hpp"
#include <cstdio>

using namespace Xi;

// ---------------------------------------------------------------------------
// Cube Mesh Generator
// ---------------------------------------------------------------------------
static Mesh3 makeCube() {
  Mesh3 mesh;

  // 24 vertices (4 per face with unique normals)
  Vertex verts[] = {
      // Front face (z = +0.5)
      {-0.5f, -0.5f, 0.5f, 0, 1, 0, 0, 1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, -0.5f, 0.5f, 1, 1, 0, 0, 1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, 0.5f, 0.5f, 0, 0, 0, 0, 1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      // Back face (z = -0.5)
      {0.5f, -0.5f, -0.5f, 0, 1, 0, 0, -1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, -0.5f, -0.5f, 1, 1, 0, 0, -1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, 0.5f, -0.5f, 1, 0, 0, 0, -1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, 0.5f, -0.5f, 0, 0, 0, 0, -1, {0, 0, 0, 0}, {1, 0, 0, 0}},
      // Top face (y = +0.5)
      {-0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, 0.5f, 0.5f, 1, 1, 0, 1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, 0.5f, -0.5f, 0, 0, 0, 1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      // Bottom face (y = -0.5)
      {-0.5f, -0.5f, -0.5f, 0, 1, 0, -1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, -0.5f, -0.5f, 1, 1, 0, -1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, -0.5f, 0.5f, 1, 0, 0, -1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, -0.5f, 0.5f, 0, 0, 0, -1, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      // Right face (x = +0.5)
      {0.5f, -0.5f, 0.5f, 0, 1, 1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, -0.5f, -0.5f, 1, 1, 1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, 0.5f, -0.5f, 1, 0, 1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      // Left face (x = -0.5)
      {-0.5f, -0.5f, -0.5f, 0, 1, -1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, -0.5f, 0.5f, 1, 1, -1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, 0.5f, 0.5f, 1, 0, -1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
      {-0.5f, 0.5f, -0.5f, 0, 0, -1, 0, 0, {0, 0, 0, 0}, {1, 0, 0, 0}},
  };

  u32 idx[] = {
      0,  1,  2,  2,  3,  0,  // Front
      4,  5,  6,  6,  7,  4,  // Back
      8,  9,  10, 10, 11, 8,  // Top
      12, 13, 14, 14, 15, 12, // Bottom
      16, 17, 18, 18, 19, 16, // Right
      20, 21, 22, 22, 23, 20, // Left
  };

  for (auto &v : verts)
    mesh.vertices.push(v);
  for (auto &i : idx)
    mesh.indices.push(i);

  /// jjjj

  return mesh;
}

// ---------------------------------------------------------------------------
// Shaders (HLSL)
// ---------------------------------------------------------------------------
static const char *cubeVS = R"(
cbuffer Primitives {
    row_major float4x4 g_MVP;
    row_major float4x4 g_Model;
};


struct VSInput {
    float3 Pos    : ATTRIB0;
    float2 UV     : ATTRIB1;
    float3 Normal : ATTRIB2;
    uint4  Joints : ATTRIB3;
    float4 Weights: ATTRIB4;
};

struct PSInput {
    float4 Pos    : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV     : TEX_COORD;
};

void main(in VSInput VSIn, out PSInput PSOut) {
    PSOut.Pos    = mul(float4(VSIn.Pos, 1.0), g_MVP);
    PSOut.Normal = mul(float4(VSIn.Normal, 0.0), g_Model).xyz;
    PSOut.UV     = VSIn.UV;
}
)";

static const char *cubePS = R"(
struct PSInput {
    float4 Pos    : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV     : TEX_COORD;
};

void main(in PSInput PSIn, out float4 Color : SV_TARGET) {
    float3 lightDir = normalize(float3(0.5, 1.0, -0.3));
    float  NdotL    = saturate(dot(normalize(PSIn.Normal), lightDir));

    float3 baseColor = abs(PSIn.Normal) * 0.4 + 0.3;

    float3 ambient = baseColor * 0.25;
    float3 diffuse = baseColor * NdotL * 0.75;

    Color = float4(ambient + diffuse, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  // 1. Create GLFW+Diligent window via the new API
  Xi::Device *win = requestWindow();

  // 2. Configure window via DeviceScreen properties
  DeviceScreen *screen = win->screen();
  if (!screen) {
    printf("Error: Could not find IOScreen!\n");
    return 1;
  }
  screen->title = "Xi Render Test — Spinning Cube";
  // screen->width = 1280;
  // screen->height = 720;

  // 3. Build scene
  Mesh3 cubeMesh = makeCube();

  Shader cubeShader;
  cubeShader.vertexSource = cubeVS;
  cubeShader.pixelSource = cubePS;

  TreeItem root;
  Renderable3 *cube = new Renderable3();
  cube->name = "Cube";
  cube->mesh = &cubeMesh;
  cube->shader = &cubeShader;
  root.add(cube);

  // 4. Setup camera
  Camera3 camera;
  camera.root = &root;
  camera.setPosition({0.0f, 1.5f, -4.0f});
  camera.lookAt({0, 0, 0});
  camera.fov = 60.0f;

  // Use the screen's rendering device for the camera
  camera.device = screen->renderingDevice;

  // Link screen surface directly to camera surface (direct assignment)
  screen->surface = &camera.surface;

  // 5. Main loop
  i64 lastTime = millis();

  while (!win->shouldRelease) {
    i64 now = millis();
    f32 dt = (f32)(now - lastTime) / 1000.0f;
    lastTime = now;

    // Rotate the cube
    auto rot = cube->getRotation();
    rot.y += 0.8f * dt;
    cube->setRotation(rot);

    // Pulse camera forward/backward
    f32 time = (f32)now / 1000.0f;
    auto camPos = camera.getPosition();
    camPos.z = -5.0f + sinf(time) * 3.0f;
    camera.setPosition(camPos);
    camera.lookAt({0, 0, 0});

    // Sync camera texture size with screen dimensions

    camera.surfaceWidth = screen->screenWidth;
    camera.surfaceHeight = screen->screenHeight;

    // IMPORTANT: Perform rendering
    camera.render();

    // Poll, render to screen, present
    win->update();

    Xi::Time::sleep(1 / 60);

    // Check ESC via Device1D keys
    for (usz i = 0; i < win->devices.length(); ++i) {
      auto *key = dynamic_cast<Device1D *>(win->devices[i]);
      if (key && key->id == 256 && key->value > 0.0f) { // GLFW_KEY_ESCAPE
        win->shouldRelease = true;
      }
    }
  }

  delete win;
  return 0;
}
