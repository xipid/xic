import { initXi, Xi } from "../src/ts/xi.js";
import type { XiString, XiArray, XiMap } from "../src/ts/xi.js";

async function runTests() {
  console.log("🚀 Initializing Xi Wasm in TypeScript...");
  await initXi();

  console.log("\n🧪 Testing Xi::String...");
  const msg = "TypeScript + Wasm = ❤️";
  const s = new Xi.String(msg);

  console.log(`Content: "${s.toString()}"`);
  console.log(`Length (bytes): ${s.length} (expected 26)`);

  if (s.length !== 26) throw new Error("Byte length mismatch");
  if (s.toString() !== msg) throw new Error("Content mismatch");

  console.log("Testing slice(0, 10):", s.slice(0, 10).toString());
  console.log("Testing toUpperCase():", s.toUpperCase().toString());
  console.log("Testing indexOf('T'):", s.indexOf("T", 0));
  console.log("Testing indexOf('W'):", s.indexOf("W", 0));
  console.log("Testing indexOf('Wasm'):", s.indexOf("Wasm", 0));
  console.log("Testing includes('Wasm'):", s.includes("Wasm"));

  if (s.indexOf("Wasm", 0).toString() !== "13")
    throw new Error("indexOf failure (possible COW bug)");

  console.log("\n🧪 Testing Xi::Array (Proxy)...");
  const arr = new Xi.ArrayString() as XiArray<XiString>;
  arr.push(new Xi.String("Hello"));
  arr.push(new Xi.String("World"));

  console.log(`Array size: ${arr.size()}`);
  console.log(`arr[0]: ${arr[0]}`);
  console.log(`arr[1]: ${arr[1]}`);

  arr[1] = new Xi.String("TypeScript");
  console.log(`arr[1] after update: ${arr[1]}`);

  console.log("Mapping over array:");
  const upper = Array.from(arr).map((s) => s.toUpperCase().toString());
  console.log("Result:", upper);

  console.log("\n🧪 Testing Xi::Map (Proxy)...");
  const map = new Xi.MapU64String() as XiMap<bigint, XiString>;
  map.set(1n, new Xi.String("Value 1"));
  map[2n] = new Xi.String("Value 2");

  console.log(`Map size: ${map.size()}`);
  console.log(`map[1]: ${map[1n]}`);
  console.log(`map[2]: ${map[2n]}`);

  if (map[1n].toString() !== "Value 1") throw new Error("Map get @ 1 failure");
  if (map.get(2n).toString() !== "Value 2")
    throw new Error("Map get @ 2 failure");

  console.log("\n🧪 Testing Tunnel & Packet...");
  const tunnel = new Xi.Tunnel();
  console.log("Tunnel name:", tunnel.name.toString());

  const payload = new Xi.String("Encrypted Content");
  const packet = new Xi.Packet(payload, 42);
  console.log(`Packet ID: ${packet.id}, Channel: ${packet.channel}`);

  tunnel.push(packet);
  console.log("Packet successfully enqueued in Tunnel");

  console.log("\n🧪 Testing RailwayStation...");
  const station = new Xi.RailwayStation();
  station.name = new Xi.String("Main Hub");
  console.log("Station name:", station.name.toString());
  const r = station.enrail();
  console.log("Station rail ID:", r);

  console.log("\n✨ All TypeScript verification tests passed!");
}

runTests().catch((err) => {
  console.error("❌ Tests failed:", err);
  process.exit(1);
});
