/**
 * @file index.ts
 * @brief Primary entry point for the Xi Core library in JavaScript/TypeScript.
 */

import { loadXiWasm } from "./wasm-loader";

/**
 * @namespace Xi
 * @description The main Xi Core namespace.
 */
export interface XiInstance {
  String: any;
  Map: any;
  Tunnel: any;
  hash: (input: string | Uint8Array, length: number) => Uint8Array;
}

/**
 * @brief Asynchronously initializes the Xi Core WASM library.
 * @param wasmUrl Optional custom path to the .wasm file.
 * @returns A promise that resolves to the XiInstance.
 */
export async function initXi(wasmUrl?: string): Promise<XiInstance> {
  const mod = await loadXiWasm(wasmUrl);
  return {
    String: mod.Xi.String,
    Map: mod.Xi.Map,
    Tunnel: mod.Xi.Tunnel,
    hash: mod.Xi.hash
  };
}

export * from "./types";
