#!/bin/bash
set -e

# Curated HSL-like sleek styling for modern premium look
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0;m' # No Color

log() {
  echo -e "${GREEN}$1${NC}"
}

error() {
  echo -e "${RED}Error: $1${NC}" >&2
}

python_bin=".venv/bin/python3"

setup_venv() {
  if [ ! -d ".venv" ]; then
    echo -e "${BLUE}Creating Python Virtual Environment...${NC}"
    python3 -m venv .venv
  fi
  echo -e "${BLUE}Synchronizing Venv Build Dependencies...${NC}"
  $python_bin -m pip install -r requirements-build.txt
}

ensure_emsdk() {
  echo -e "${YELLOW}Checking for Emscripten SDK...${NC}"
  if command -v emcc &> /dev/null; then
    echo -e "${GREEN}✅ Emscripten already available in system PATH.${NC}"
    return 0
  fi

  local sdk_base="$HOME/.cache/xic/emsdk"
  local emcc_path="$sdk_base/upstream/emscripten/emcc"

  if [ ! -f "$emcc_path" ]; then
    echo -e "${BLUE}Installing Emscripten to external cache (This may take several minutes)...${NC}"
    mkdir -p "$(dirname "$sdk_base")"
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$sdk_base"
    "$sdk_base/emsdk" install latest
    "$sdk_base/emsdk" activate latest
  fi

  # Source emsdk environment
  if [ -f "$sdk_base/emsdk_env.sh" ]; then
    source "$sdk_base/emsdk_env.sh" > /dev/null 2>&1
    echo -e "${GREEN}✅ Emscripten PATH injected successfully.${NC}"
  else
    error "Failed to locate emsdk_env.sh"
    exit 1
  fi
}

build_wasm() {
  setup_venv
  ensure_emsdk

  echo -e "${BLUE}Generating Typescript .d.ts and Python .pyi stubs from C++ Headers...${NC}"
  $python_bin src/stubgen.py

  local bin_dir="dist/bin/wasm"
  local js_dir="dist/js"
  mkdir -p "$bin_dir" "$js_dir"

  # Gather source files dynamically
  local sources=()
  sources+=("packages/monocypher/monocypher.c")

  for f in src/Xi/*.cpp; do
    [ -e "$f" ] || continue
    local base=$(basename "$f")
    if [ "$base" != "Camera.cpp" ] && [ "$base" != "Graphics.cpp" ] && [ "$base" != "Window.cpp" ]; then
      sources+=("$f")
    fi
  done

  for f in src/Hardware/*.cpp; do
    [ -e "$f" ] || continue
    local base=$(basename "$f")
    if [ "$base" != "Camera.cpp" ] && [ "$base" != "Graphics.cpp" ] && [ "$base" != "Window.cpp" ]; then
      sources+=("$f")
    fi
  done

  local objects=()
  for src in "${sources[@]}"; do
    local rel_obj="${src%.*}.o"
    local obj="$bin_dir/$rel_obj"
    mkdir -p "$(dirname "$obj")"
    objects+=("$obj")

    echo -e "${YELLOW}Compiling $src with Emscripten...${NC}"
    local std_flag=""
    if [[ "$src" == *.cpp ]]; then
      std_flag="-std=c++17"
    fi

    emcc -O3 -c "$src" -Iinclude -Ipackages/monocypher $std_flag -o "$obj"
  done

  echo -e "${BLUE}Linking with Emscripten Base (Execution Mode)...${NC}"
  emcc -O3 "${objects[@]}" -o "$js_dir/xi.js"
  echo -e "${GREEN}✅ WASM Native execution environment built at dist/js/xi.js${NC}"
}

build_python() {
  setup_venv
  echo -e "${BLUE}Generating Typescript .d.ts and Python .pyi stubs...${NC}"
  $python_bin src/stubgen.py

  echo -e "${BLUE}Executing PEP 517 build via venv...${NC}"
  $python_bin -m build
  echo -e "${GREEN}✅ Python build complete.${NC}"
}

build_website() {
  echo -e "${BLUE}Building Website (Vite)...${NC}"
  npx vite build
  echo -e "${GREEN}✅ Website build complete.${NC}"
}

build_docs() {
  echo -e "${BLUE}Generating Documentation...${NC}"
  ./build.sh website
  echo -e "${GREEN}✅ Documentation build complete.${NC}"
}

# Main routing logic
echo -e "${CYAN}================================================================${NC}"
echo -e "${CYAN}               XiC Professional C++ Builder                     ${NC}"
echo -e "${CYAN}================================================================${NC}"

TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
  TARGETS=("all")
fi

for target in "${TARGETS[@]}"; do
  case "$target" in
    wasm)
      build_wasm
      ;;
    python)
      build_python
      ;;
    website)
      build_website
      ;;
    docs)
      build_docs
      ;;
    all)
      build_wasm
      build_python
      build_website
      build_docs
      ;;
    *)
      error "Unknown build target: $target"
      echo "Usage: $0 [wasm|python|website|docs]"
      exit 1
      ;;
  esac
done

echo -e "${CYAN}================================================================${NC}"
echo -e "${GREEN}✨ Build process finished successfully.${NC}"
echo -e "${CYAN}================================================================${NC}"
