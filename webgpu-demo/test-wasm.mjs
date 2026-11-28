// Node gate for every WASM CPU reference used by the browser demo.
import { readFile } from 'node:fs/promises';

const bytes = await readFile(new URL('./kernel.wasm', import.meta.url));
if (!WebAssembly.validate(bytes)) throw new Error('kernel.wasm is not valid WebAssembly');

async function load() {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  return { exports: instance.exports, mem: new Float32Array(instance.exports.memory.buffer) };
}
function check(label, got, want, tol = 1e-4) {
  const diff = Math.abs(got - want);
  if (!(diff <= tol)) throw new Error(`${label}: got=${got} want=${want} diff=${diff}`);
}

// vecadd
{
  const { exports: e, mem } = await load();
  const n = 1001, alpha = 1.5;
  for (let i = 0; i < n; i++) { mem[i] = i * 0.25; mem[n + i] = 2; }
  e.hcs_vecadd_ref(n, 0, n * 4, n * 8, alpha);
  for (let i = 0; i < n; i++) check(`vecadd[${i}]`, mem[2 * n + i], i * 0.25 + alpha * 2);
}
// scale
{
  const { exports: e, mem } = await load();
  const n = 512, alpha = 0.5;
  for (let i = 0; i < n; i++) mem[i] = i - 100;
  e.hcs_scale_ref(n, 0, n * 4, alpha);
  for (let i = 0; i < n; i++) check(`scale[${i}]`, mem[n + i], (i - 100) * alpha);
}
// reduce
{
  const { exports: e, mem } = await load();
  const n = 1000, groups = 8, chunk = Math.ceil(n / groups);
  for (let i = 0; i < n; i++) mem[i] = 1;
  e.hcs_reduce_ref(n, 0, n * 4, groups);
  for (let g = 0; g < groups; g++) {
    const want = Math.min(n, (g + 1) * chunk) - g * chunk;
    check(`reduce[${g}]`, mem[n + g], want);
  }
}
// scan
{
  const { exports: e, mem } = await load();
  const n = 16, groups = 4, chunk = 4;
  for (let i = 0; i < n; i++) mem[i] = 2;
  e.hcs_scan_ref(n, 0, n * 4, groups);
  for (let i = 0; i < n; i++) check(`scan[${i}]`, mem[n + i], (i % chunk) * 2);
}
// histogram
{
  const { exports: e, mem } = await load();
  const n = 500, hOff = 2048;
  const u32 = new Uint32Array(instanceMemoryBuffer(e));
  for (let i = 0; i < n; i++) u32[i] = (i * 37) & 255;
  e.hcs_histogram_ref(n, 0, hOff);
  const expected = new Uint32Array(256);
  for (let i = 0; i < n; i++) expected[(i * 37) & 255]++;
  const actual = new Uint32Array(e.memory.buffer, hOff, 256);
  for (let b = 0; b < 256; b++) if (actual[b] !== expected[b]) throw new Error(`hist[${b}]`);
}
// matmul
{
  const { exports: e, mem } = await load();
  const m = 8, n = 8, k = 8, aOff = 0, bOff = 256, cOff = 512;
  for (let i = 0; i < m * k; i++) mem[aOff / 4 + i] = (i % 5) - 2;
  for (let i = 0; i < k * n; i++) mem[bOff / 4 + i] = (i % 3) - 1;
  e.hcs_matmul_ref(m, n, k, aOff, bOff, cOff);
  for (let r = 0; r < m; r++) for (let c = 0; c < n; c++) {
    let want = 0;
    for (let t = 0; t < k; t++) want += mem[aOff / 4 + r * k + t] * mem[bOff / 4 + t * n + c];
    check(`matmul[${r},${c}]`, mem[cOff / 4 + r * n + c], want, 1e-3);
  }
}
// transpose
{
  const { exports: e, mem } = await load();
  const rows = 7, cols = 5, bOff = rows * cols * 4;
  for (let i = 0; i < rows * cols; i++) mem[i] = i * 0.5;
  e.hcs_transpose_ref(rows, cols, 0, bOff);
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++)
    check(`transpose[${r},${c}]`, mem[bOff / 4 + c * rows + r], mem[r * cols + c]);
}

function instanceMemoryBuffer(exports) { return exports.memory.buffer; }
console.log('WASM_TEST_PASS kernels=7');
