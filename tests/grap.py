import sys
from pathlib import Path

# Add src to path so we can import xi
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

import cppyy
import xi  # This will trigger `xi/__init__.py` (it sets up basic aliases)

# Include C++ Engine classes
root_dir = Path(__file__).resolve().parent.parent
cppyy.add_include_path(str(root_dir / "include"))

diligent_dir = root_dir / "tests/render/build/_deps/diligentengine-src/DiligentCore"
paths_to_add = [
    "Graphics/GraphicsEngine/interface",
    "Graphics/GraphicsEngineVulkan/interface",
    "Graphics/GraphicsEngineOpenGL/interface",
    "Common/interface",
    "Primitives/interface",
    "Platforms/interface",
    "Platforms/Linux/interface",
    "Platforms/Basic/interface"
]

for p in paths_to_add:
    cppyy.add_include_path(str(diligent_dir / p))

# Tell cppyy (Cling) what platform we are on so Diligent headers compile
cppyy.cppdef("#define PLATFORM_LINUX 1")

cppyy.include("Xi/Graphics.hpp")
cppyy.include("Xi/Window.hpp")
cppyy.include("Xi/Mesh.hpp")
cppyy.include("Xi/Camera.hpp")
cppyy.include("Xi/Time.hpp")

# Load compiled Graphics Engine symbols!
lib_path = Path(__file__).parent / "render" / "build" / "libxi_render.so"
if not lib_path.exists():
    print(f"Error: Could not find {lib_path}. Please cd to tests/render/build and run 'ninja'.")
    sys.exit(1)

cppyy.load_library(str(lib_path))

# Bring Xi namespace to local scope for convenience
Xi = cppyy.gbl.Xi

# ---------------------------------------------------------------------------
# Cube Mesh Generator
# ---------------------------------------------------------------------------
def makeCube():
    mesh = Xi.Mesh3()
    
    verts = [
        # Front face (z = +0.5)
        [-0.5, -0.5, 0.5, 0, 1, 0, 0, 1, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, -0.5, 0.5, 1, 1, 0, 0, 1, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, 0.5, 0.5, 1, 0, 0, 0, 1, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, 0.5, 0.5, 0, 0, 0, 0, 1, [0, 0, 0, 0], [1, 0, 0, 0]],
        # Back face (z = -0.5)
        [0.5, -0.5, -0.5, 0, 1, 0, 0, -1, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, -0.5, -0.5, 1, 1, 0, 0, -1, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, 0.5, -0.5, 1, 0, 0, 0, -1, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, 0.5, -0.5, 0, 0, 0, 0, -1, [0, 0, 0, 0], [1, 0, 0, 0]],
        # Top face (y = +0.5)
        [-0.5, 0.5, 0.5, 0, 1, 0, 1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, 0.5, 0.5, 1, 1, 0, 1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, 0.5, -0.5, 1, 0, 0, 1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, 0.5, -0.5, 0, 0, 0, 1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        # Bottom face (y = -0.5)
        [-0.5, -0.5, -0.5, 0, 1, 0, -1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, -0.5, -0.5, 1, 1, 0, -1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, -0.5, 0.5, 1, 0, 0, -1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, -0.5, 0.5, 0, 0, 0, -1, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        # Right face (x = +0.5)
        [0.5, -0.5, 0.5, 0, 1, 1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, -0.5, -0.5, 1, 1, 1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, 0.5, -0.5, 1, 0, 1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [0.5, 0.5, 0.5, 0, 0, 1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        # Left face (x = -0.5)
        [-0.5, -0.5, -0.5, 0, 1, -1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, -0.5, 0.5, 1, 1, -1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, 0.5, 0.5, 1, 0, -1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
        [-0.5, 0.5, -0.5, 0, 0, -1, 0, 0, [0, 0, 0, 0], [1, 0, 0, 0]],
    ]
    
    idx = [
        0,  1,  2,  2,  3,  0,  # Front
        4,  5,  6,  6,  7,  4,  # Back
        8,  9,  10, 10, 11, 8,  # Top
        12, 13, 14, 14, 15, 12, # Bottom
        16, 17, 18, 18, 19, 16, # Right
        20, 21, 22, 22, 23, 20, # Left
    ]

    for v in verts:
        vertex = Xi.Vertex()
        vertex.x, vertex.y, vertex.z = v[0], v[1], v[2]
        vertex.u, vertex.v = v[3], v[4]
        vertex.nx, vertex.ny, vertex.nz = v[5], v[6], v[7]
        mesh.vertices.push(vertex)

    for i in idx:
        mesh.indices.push(i)

    return mesh

# ---------------------------------------------------------------------------
# Shaders (HLSL)
# ---------------------------------------------------------------------------
cubeVS = """
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
"""

cubePS = """
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
"""

def main():
    # 1. Create GLFW+Diligent window via the new API
    win = Xi.requestWindow()

    # 2. Configure window via DeviceScreen properties
    screen = win.screen()
    if not screen:
        print("Error: Could not find IOScreen!")
        return 1

    # Need to convert python strings to Xi strings
    screen.title = xi.String("Xi Python Render Test — Spinning Cube")
    
    # 3. Build scene
    cubeMesh = makeCube()

    cubeShader = Xi.Shader()
    cubeShader.vertexSource = xi.String(cubeVS)
    cubeShader.pixelSource = xi.String(cubePS)

    root = Xi.TreeItem()
    cube = Xi.Renderable3()
    cube.name = xi.String("Cube")
    cube.mesh = cubeMesh
    cube.shader = cubeShader
    
    # Actually cppyy can pass object references transparently, but C++ expects pointers in some places
    # Wait, `root.add(Renderable*)` takes a pointer.
    root.add(cube)

    # 4. Setup camera
    camera = Xi.Camera3()
    camera.root = root
    
    vpo = Xi.Vector3()
    vpo.x, vpo.y, vpo.z = 0.0, 1.5, -4.0
    camera.setPosition(vpo)
    
    vlo = Xi.Vector3()
    vlo.x, vlo.y, vlo.z = 0, 0, 0
    camera.lookAt(vlo)
    
    camera.fov = 60.0

    camera.device = screen.renderingDevice
    
    # Link screen surface directly to camera surface
    screen.surface = camera.surface

    # 5. Main loop
    import math

    lastTime = Xi.Time.millis()

    while not win.shouldRelease:
        now = Xi.Time.millis()
        dt = (now - lastTime) / 1000.0
        lastTime = now

        # Rotate the cube
        rot = cube.getRotation()
        rot.y += 0.8 * dt
        cube.setRotation(rot)

        # Pulse camera
        time_sec = now / 1000.0
        camPos = camera.getPosition()
        camPos.z = -5.0 + math.sin(time_sec) * 3.0
        camera.setPosition(camPos)
        
        vlo2 = Xi.Vector3()
        vlo2.x, vlo2.y, vlo2.z = 0, 0, 0
        camera.lookAt(vlo2)

        camera.surfaceWidth = screen.screenWidth
        camera.surfaceHeight = screen.screenHeight

        # Render
        camera.render()

        # Present
        win.update()

        Xi.Time.sleep(int(1/60 * 1000) if hasattr(Xi.Time.sleep, 'argtypes') else 0)

        # Check ESC
        for i in range(win.devices.length()):
            dev = win.devices.at(i)
            # Try to downcast or just access
            try:
                # If it's a Device1D pointer, we should cast. 
                # Let's dynamically cast in cppyy:
                dev1d = cppyy.bind_object(dev, 'Xi::Device1D')
                if dev1d.id == 256 and dev1d.value > 0.0:
                    win.shouldRelease = True
            except Exception:
                pass
                
    # cleanup
    # python garbage collection handles `win` normally, but we can do it explicitly
    del win

if __name__ == "__main__":
    main()
