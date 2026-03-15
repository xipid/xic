import { execSync } from "child_process";
import fs from "fs";
import path from "path";

const targets = process.argv.slice(2);
const allTargets = ["wasm", "python", "website"];
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

async function buildWasm() {
  const binDir = "dist/bin/wasm";
  const jsDir = "dist/js";
  if (!fs.existsSync(binDir)) fs.mkdirSync(binDir, { recursive: true });
  if (!fs.existsSync(jsDir)) fs.mkdirSync(jsDir, { recursive: true });

  const includePaths = "-Iinclude -Ipackages/monocypher";
  const emccBin = "emcc";

  run("python3 src/stubgen.py", "Generating Typescript .d.ts and Python .pyi stubs from C++ Headers");

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
  
  run("python3 src/stubgen.py", "Generating Typescript .d.ts and Python .pyi stubs from C++ Headers");

  try {
    run("python3 -m build", "Executing PEP 517 build", { allowFail: true });
  } catch (e) {
    log(
      "\n⚠️  PEP 517 build failed (is 'build' module installed?).",
      colors.yellow,
    );
    log("Installing 'build' module and retrying...", colors.blue);
    try {
      run("python3 -m pip install build", "Installing build module");
      run("python3 -m build", "Retrying PEP 517 build");
    } catch (e2) {
      log(
        "❌ Failed to build python package. Please ensure 'build' is available.",
        colors.red,
      );
      throw e2;
    }
  }
  log("✅ Python build complete.", colors.green);
}

async function buildWebsite() {
  run("pnpm run site:build", "Building Website (Vite)");
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

  log(
    "\n✨ Build process finished successfully.",
    colors.bright + colors.green,
  );
}

main();

