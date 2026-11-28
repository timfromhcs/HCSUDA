// Node gate for the WASM reference kernel used by the browser demo.
import { readFile } from 'node:fs/promises';

const bytes = await readFile(new URL('./kernel.wasm', import.meta.url));
if (!WebAssembly.validate(bytes)) throw new Error('kernel.wasm is not valid WebAssembly');
const { instance } = await WebAssembly.instantiate(bytes, {});
const memory = new Float32Array(instance.exports.memory.buffer);
const n = 1001;
const alpha = 1.5;
for (let i = 0; i < n; i++) { memory[i] = i * 0.25; memory[n + i] = 2.0; }
instance.exports.hcs_vecadd_ref(n, 0, n * 4, n * 8, alpha);
let worst = 0;
for (let i = 0; i < n; i++) {
  const expected = i * 0.25 + alpha * 2.0;
  worst = Math.max(worst, Math.abs(memory[2 * n + i] - expected));
}
if (worst > 1e-5) throw new Error(`WASM reference mismatch: ${worst}`);
console.log(`WASM_TEST_PASS n=${n} worst=${worst}`);
