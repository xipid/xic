/**
 * @file wasm-loader.ts
 * @brief Helper to load and initialize the Emscripten-generated WASM module.
 */

declare const XiModule: any;

export async function loadXiWasm(wasmUrl?: string): Promise<any> {
  return new Promise((resolve, reject) => {
    // If running in a setup where xi.js is already global
    if (typeof XiModule !== "undefined") {
      XiModule({
        locateFile: (path: string) => wasmUrl || path,
      }).then((mod: any) => resolve(mod));
      return;
    }

    // Dynamic import of the generated JS bridge
    import("../../dist/js/xi.js" as any)
      .then((m) => {
        m.default({
          locateFile: (path: string) => wasmUrl || path,
        }).then((mod: any) => resolve(mod));
      })
      .catch(reject);
  });
}
