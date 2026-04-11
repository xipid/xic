import { execSync } from "child_process";
import fs from "fs";
import os from "os";
import path from "path";

const targets = process.argv.slice(2);
const allTargets = ["wasm", "python", "website", "docs"];
const activeTargets = targets.length > 0 ? targets : ["all"];

const colors = {
  reset: "\x1b[0m",
  bright: "\x1b[1m",
  green: "\x1b[32m",
  yellow: "\x1b[33m",
  blue: "\x1b[34m",
  red: "\x1b[31m",
};

function log(msg, color = colors.reset) {
  console.log(`${color}${msg}${colors.reset}`);
}

function run(cmd, desc, options = {}) {
  log(`\n🚀 ${desc}...`, colors.bright + colors.blue);
  log(`$ ${cmd}`, colors.yellow);
  try {
    execSync(cmd, { stdio: "inherit" });
  } catch (e) {
    if (options.allowFail) {
      throw e;
    }
    log(`❌ Failed: ${desc}`, colors.red);
    process.exit(1);
  }
}

const pythonBin = process.platform === "win32" ? ".venv/Scripts/python" : ".venv/bin/python3";

async function setupVenv() {
  if (!fs.existsSync(".venv")) {
    run("python3 -m venv .venv", "Creating Python Virtual Environment");
  }
  
  run(`${pythonBin} -m pip install -r requirements-build.txt`, "Synchronizing Venv Build Dependencies");
}

async function ensureEmsdk() {
  log("Checking for Emscripten SDK...", colors.yellow);
  
  // 1. Check if emcc is already in PATH
  try {
    execSync("emcc --version", { stdio: "ignore" });
    log("✅ Emscripten already available in system PATH.", colors.green);
    return;
  } catch (e) {
    // Not in PATH, continue to cache check
  }

  const homeDir = os.homedir();
  const sdkBase = path.join(homeDir, ".cache", "xic", "emsdk");
  const emccPath = path.join(sdkBase, "upstream", "emscripten", "emcc");

  if (!fs.existsSync(emccPath)) {
    log("Installing Emscripten to external cache...", colors.bright + colors.blue);
    const parentDir = path.dirname(sdkBase);
    if (!fs.existsSync(parentDir)) fs.mkdirSync(parentDir, { recursive: true });

    run(`git clone --depth 1 https://github.com/emscripten-core/emsdk.git ${sdkBase}`, "Cloning EMSDK");
    
    const emsdkBin = path.join(sdkBase, "emsdk");
    run(`${emsdkBin} install latest`, "Installing Emscripten Logic (This may take several minutes)");
    run(`${emsdkBin} activate latest`, "Activating Toolchain");
  }

  // Inject PATH variables for the current process
  const pathsToAdd = [
    path.join(sdkBase, "upstream", "emscripten"),
    path.join(sdkBase, "node", "20.18.0_64bit", "bin"), // Approximation, emsdk activates its own node
    sdkBase
  ];

  process.env.PATH = pathsToAdd.join(path.delimiter) + path.delimiter + process.env.PATH;
  log("✅ Emscripten PATH injected successfully.", colors.green);
}

async function buildWasm() {
  const binDir = "dist/bin/wasm";
  const jsDir = "dist/js";
  if (!fs.existsSync(binDir)) fs.mkdirSync(binDir, { recursive: true });
  if (!fs.existsSync(jsDir)) fs.mkdirSync(jsDir, { recursive: true });

  const includePaths = "-Iinclude -Ipackages/monocypher";
  const emccBin = "emcc";

  await setupVenv();
  await ensureEmsdk();
  run(`${pythonBin} src/stubgen.py`, "Generating Typescript .d.ts and Python .pyi stubs from C++ Headers");

  const excludedWasmFiles = ["Camera.cpp", "Graphics.cpp", "Window.cpp"];
  const sources = [
    "packages/monocypher/monocypher.c",
    ...fs.readdirSync("src/Xi")
        .filter(f => f.endsWith(".cpp") && !excludedWasmFiles.includes(f))
        .map(f => `src/Xi/${f}`),
    ...fs.readdirSync("src/Hardware")
        .filter(f => f.endsWith(".cpp") && !excludedWasmFiles.includes(f))
        .map(f => `src/Hardware/${f}`)
  ];

  const objects = [];

  for (const src of sources) {
    const outRel = src.replace(/\.(c|cpp)$/, ".o");
    const obj = path.join(binDir, outRel);
    const objDir = path.dirname(obj);
    if (!fs.existsSync(objDir)) fs.mkdirSync(objDir, { recursive: true });

    objects.push(obj);
    
    run(
      `${emccBin} -O3 -c ${src} ${includePaths} ${src.endsWith(".cpp") ? "-std=c++17" : ""} -o ${obj}`,
      `Compiling ${src} with Emscripten`
    );
  }

  run(
    `${emccBin} -O3 ${objects.join(" ")} -o ${jsDir}/xi.js`,
    "Linking with Emscripten Base (Execution Mode)"
  );

  log("✅ WASM Native execution environment built at dist/js/xi.js", colors.green);
}

async function buildPython() {
  log("Building Python Package (Sdist & Wheel)...", colors.yellow);
  
  await setupVenv();
  run(`${pythonBin} src/stubgen.py`, "Generating Typescript .d.ts and Python .pyi stubs from C++ Headers");

  run(`${pythonBin} -m build`, "Executing PEP 517 build via venv");
  log("✅ Python build complete.", colors.green);
}

async function buildDocs() {
  log("Generating Documentation...", colors.yellow);
  run("node src/build.js website", "Rebuilding Website for Docs");
  log("✅ Documentation build complete.", colors.green);
}

async function buildWebsite() {
  run("vite build", "Building Website (Vite)");
  log("✅ Website build complete.", colors.green);
}

async function main() {
  log("🛠️ Starting XiC Professional Build...", colors.bright + colors.green);

  if (activeTargets.includes("all") || activeTargets.includes("wasm")) {
    await buildWasm();
  }

  if (activeTargets.includes("all") || activeTargets.includes("python")) {
    await buildPython();
  }

  if (activeTargets.includes("all") || activeTargets.includes("website")) {
    await buildWebsite();
  }

  if (activeTargets.includes("all") || activeTargets.includes("docs")) {
    await buildDocs();
  }

  log(
    "\n✨ Build process finished successfully.",
    colors.bright + colors.green,
  );
}

main();

