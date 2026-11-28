// Standalone WebGPU/WASM demo. Truthful scope: no native HCSUDA runtime, no fabricated metrics.
const WASM_URL = './kernel.wasm';
const KERNELS = {
  vecadd_f32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<f32>;
@group(0) @binding(1) var<storage, read> b: array<f32>;
@group(0) @binding(2) var<storage, read_write> c: array<f32>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let i = g.x; if (i >= p.n) { return; } c[i] = a[i] + p.alpha * b[i];
}`,
    groups: o => Math.ceil(o.n / 64),
    wasm: (e, mem, o, aOff, cOff) => { e.hcs_vecadd_ref(o.n, aOff, o.n * 4, cOff, o.alpha); return cOff; },
    count: o => o.n
  },
  scale_f32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<f32>;
@group(0) @binding(1) var<storage, read> b: array<f32>;
@group(0) @binding(2) var<storage, read_write> c: array<f32>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let i = g.x; if (i >= p.n) { return; } c[i] = p.alpha * a[i];
}`,
    groups: o => Math.ceil(o.n / 64),
    wasm: (e, mem, o, aOff, cOff) => { e.hcs_scale_ref(o.n, aOff, cOff, o.alpha); return cOff; },
    count: o => o.n
  },
  reduce_sum_f32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<f32>;
@group(0) @binding(1) var<storage, read> b: array<f32>;
@group(0) @binding(2) var<storage, read_write> c: array<f32>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(256) fn main(@builtin(local_invocation_id) l: vec3<u32>, @builtin(workgroup_id) w: vec3<u32>) {
  if (l.x != 0u) { return; }
  let chunk = (p.n + p.groups - 1u) / p.groups;
  let first = w.x * chunk; var last = first + chunk; if (last > p.n) { last = p.n; }
  var sum = 0.0; for (var i = first; i < last; i = i + 1u) { sum = sum + a[i]; }
  c[w.x] = sum;
}`,
    groups: o => o.groups,
    wasm: (e, mem, o, aOff, cOff) => { e.hcs_reduce_ref(o.n, aOff, cOff, o.groups); return cOff; },
    count: o => o.groups
  },
  scan_f32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<f32>;
@group(0) @binding(1) var<storage, read> b: array<f32>;
@group(0) @binding(2) var<storage, read_write> c: array<f32>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(256) fn main(@builtin(local_invocation_id) l: vec3<u32>, @builtin(workgroup_id) w: vec3<u32>) {
  if (l.x != 0u) { return; }
  let chunk = (p.n + p.groups - 1u) / p.groups;
  let first = w.x * chunk; var last = first + chunk; if (last > p.n) { last = p.n; }
  var prefix = 0.0; for (var i = first; i < last; i = i + 1u) { c[i] = prefix; prefix = prefix + a[i]; }
}`,
    groups: o => o.groups,
    wasm: (e, mem, o, aOff, cOff) => { e.hcs_scan_ref(o.n, aOff, cOff, o.groups); return cOff; },
    count: o => o.n
  },
  histogram_u32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<u32>;
@group(0) @binding(1) var<storage, read> b: array<u32>;
@group(0) @binding(2) var<storage, read_write> h: array<atomic<u32>>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let i = g.x; if (i >= p.n) { return; } atomicAdd(&h[a[i] & 255u], 1u);
}`,
    groups: o => Math.ceil(o.n / 64),
    wasm: (e, mem, o, aOff, cOff) => { e.hcs_histogram_ref(o.n, aOff, cOff); return cOff; },
    count: () => 256,
    u32: true
  },
  matmul_f32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<f32>;
@group(0) @binding(1) var<storage, read> b: array<f32>;
@group(0) @binding(2) var<storage, read_write> c: array<f32>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(16,16) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let row = g.y; let col = g.x; if (row >= p.m || col >= p.n2) { return; }
  var sum = 0.0; for (var t = 0u; t < p.k; t = t + 1u) { sum = sum + a[row*p.k+t] * b[t*p.n2+col]; }
  c[row*p.n2+col] = sum;
}`,
    groups: o => [Math.ceil(o.n2 / 16), Math.ceil(o.m / 16), 1],
    wasm: (e, mem, o, aOff, cOff) => {
      const bOff = o.m * o.k * 4;
      e.hcs_matmul_ref(o.m, o.n2, o.k, aOff, bOff, cOff); return cOff;
    },
    count: o => o.m * o.n2
  },
  transpose_f32: {
    wgsl: `@group(0) @binding(0) var<storage, read> a: array<f32>;
@group(0) @binding(1) var<storage, read> b: array<f32>;
@group(0) @binding(2) var<storage, read_write> c: array<f32>;
struct P { n:u32, alpha:f32, groups:u32, m:u32, n2:u32, k:u32, p0:u32, p1:u32 }
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(16,16) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let row = g.y; let col = g.x; if (row >= p.m || col >= p.n2) { return; }
  c[col*p.m+row] = a[row*p.n2+col];
}`,
    groups: o => [Math.ceil(o.n2 / 16), Math.ceil(o.m / 16), 1],
    wasm: (e, mem, o, aOff, cOff) => { e.hcs_transpose_ref(o.m, o.n2, aOff, cOff); return cOff; },
    count: o => o.m * o.n2
  }
};

let wasmBytes = null;
let lastRun = null;
let benchRows = [];

function options() {
  const n = Math.min(8192, Math.max(1, Number(document.getElementById('n').value) || 1));
  const cols = Math.min(64, Math.max(1, Number(document.getElementById('cols').value) || 1));
  const k = Math.min(64, Math.max(1, Number(document.getElementById('k').value) || 1));
  return {
    kernel: document.getElementById('kernel').value,
    n, cols, k, m: n, n2: cols,
    alpha: Number(document.getElementById('alpha').value) || 1,
    groups: Math.min(256, Math.max(1, Number(document.getElementById('groups').value) || 1)),
    warmup: Math.min(20, Math.max(0, Number(document.getElementById('warmup').value) || 0)),
    reps: Math.min(50, Math.max(1, Number(document.getElementById('reps').value) || 1)),
    backend: document.getElementById('backend').value
  };
}

function inputs(o) {
  const a = new Float32Array(Math.max(o.n, o.m * o.k, o.m * o.n2));
  const b = new Float32Array(Math.max(o.n, o.k * o.n2, o.m * o.n2));
  for (let i = 0; i < a.length; i++) a[i] = ((i % 17) - 8) * 0.125;
  for (let i = 0; i < b.length; i++) b[i] = ((i % 7) - 3) * 0.25;
  if (o.kernel === 'histogram_u32') {
    const u = new Uint32Array(a.buffer);
    for (let i = 0; i < o.n; i++) u[i] = (i * 37) & 255;
  }
  return { a, b };
}

function paramsBytes(o) {
  const buf = new ArrayBuffer(32);
  const u = new Uint32Array(buf), f = new Float32Array(buf);
  u[0] = o.n; f[1] = o.alpha; u[2] = o.groups; u[3] = o.m; u[4] = o.n2; u[5] = o.k;
  return buf;
}

async function gpuRun(device, kernel, o, data, count, reps) {
  const def = KERNELS[kernel];
  const size = Math.max(64, count * 4);
  const mk = (typed, usage) => {
    const buf = device.createBuffer({ size, usage });
    device.queue.writeBuffer(buf, 0, typed);
    return buf;
  };
  const inA = mk(data.a, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const inB = mk(data.b, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const out = device.createBuffer({ size, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
  const read = device.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const pu = device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
  device.queue.writeBuffer(pu, 0, paramsBytes(o));
  const layout = device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
    { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
    { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } }
  ]});
  const pipeline = device.createComputePipeline({
    layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
    compute: { module: device.createShaderModule({ code: def.wgsl }), entryPoint: 'main' }
  });
  const bind = device.createBindGroup({ layout, entries: [
    { binding: 0, resource: { buffer: inA } }, { binding: 1, resource: { buffer: inB } },
    { binding: 2, resource: { buffer: out } }, { binding: 3, resource: { buffer: pu } }
  ]});
  const g = def.groups(o);
  const times = [];
  for (let r = 0; r < o.warmup + reps; r++) {
    const enc = device.createCommandEncoder();
    const pass = enc.beginComputePass();
    pass.setPipeline(pipeline); pass.setBindGroup(0, bind);
    pass.dispatchWorkgroups(g[0] || g, g[1] || 1, g[2] || 1); pass.end();
    enc.copyBufferToBuffer(out, 0, read, 0, size);
    const t0 = performance.now();
    device.queue.submit([enc.finish()]);
    await read.mapAsync(GPUMapMode.READ);
    times.push(performance.now() - t0);
    const last = new Float32Array(read.getMappedRange()).slice(0, count);
    read.unmap();
    if (r === o.warmup + reps - 1) return { result: last, times: times.slice(o.warmup) };
  }
}

async function wasmRun(o, data, count) {
  const { instance } = await WebAssembly.instantiate(wasmBytes, {});
  const e = instance.exports;
  const mem = new Float32Array(e.memory.buffer);
  const u32 = new Uint32Array(e.memory.buffer);
  const aOff = 0, cOff = 4096;
  if (KERNELS[o.kernel].u32) u32.set(new Uint32Array(data.a.buffer, 0, o.n), aOff / 4);
  else mem.set(data.a, aOff / 4);
  const start = performance.now();
  const retOffset = KERNELS[o.kernel].wasm(e, mem, o, aOff, cOff);
  const ms = performance.now() - start;
  return { result: mem.slice(retOffset / 4, retOffset / 4 + count), times: [ms] };
}

function stats(times) {
  const s = [...times].sort((a, b) => a - b);
  const pick = q => s[Math.min(s.length - 1, Math.floor(q * (s.length - 1)))];
  return { median: pick(0.5), p10: pick(0.1), p90: pick(0.9) };
}

function worst(a, b) {
  let w = 0;
  for (let i = 0; i < Math.min(a.length, b.length); i++) w = Math.max(w, Math.abs(a[i] - b[i]));
  return w;
}

function draw(data, label) {
  const c = document.getElementById('heatmap'); const ctx = c.getContext('2d');
  ctx.fillStyle = '#0b1420'; ctx.fillRect(0, 0, c.width, c.height);
  if (benchRows.length) {
    const max = Math.max(...benchRows.map(b => b.median), 1e-6);
    benchRows.forEach((b, i) => {
      ctx.fillStyle = i === 0 ? '#4aa8ff' : '#20d7c8';
      ctx.fillRect(8 + i * 180, 4, 160 * (b.median / max), 14);
      ctx.fillStyle = '#e8f2ff'; ctx.font = '11px sans-serif';
      ctx.fillText(`${b.backend} ${b.median.toFixed(3)} ms`, 10 + i * 180, 15);
    });
  }
  if (!data.length) return;
  const sample = Array.from(data.slice(0, 512));
  let lo = Math.min(...sample), hi = Math.max(...sample);
  if (lo === hi) { lo -= 1; hi += 1; }
  const w = c.width / sample.length;
  sample.forEach((v, i) => {
    const t = (v - lo) / (hi - lo);
    ctx.fillStyle = `rgb(${Math.round(40 + 200 * t)},${Math.round(180 - 120 * t)},${Math.round(255 - 180 * t)})`;
    ctx.fillRect(i * w, 28, Math.max(1, w - 1), c.height - 38);
  });
  document.getElementById('vizNote').textContent =
    `${label}: ${data.length} measured values, range ${lo.toFixed(4)}..${hi.toFixed(4)} (demo colour mapping).`;
}

function download(name, text, type) {
  const blob = new Blob([text], { type });
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name;
  a.click(); URL.revokeObjectURL(a.href);
}

async function mainRun(mode) {
  const error = document.getElementById('error'); error.textContent = '';
  const status = document.getElementById('status'); status.textContent = 'Running…';
  try {
    const o = options(); const def = KERNELS[o.kernel];
    const data = inputs(o); const count = def.count(o);
    if (!wasmBytes) wasmBytes = await (await fetch(WASM_URL)).arrayBuffer();
    const cpu = await wasmRun(o, data, count);
    let gpu = null;
    if (o.backend !== 'wasm') {
      if (!navigator.gpu) throw new Error('WebGPU not available');
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) throw new Error('No WebGPU adapter');
      const device = await adapter.requestDevice();
      gpu = await gpuRun(device, o.kernel, o, data, count, mode === 'bench' ? 1 : o.reps);
    }
    const diff = gpu ? worst(gpu.result, cpu.result) : 0;
    const ok = !gpu || diff < 1e-3;
    const gpuStats = gpu ? stats(gpu.times) : null;
    const cpuStats = stats(cpu.times.length ? cpu.times : [0]);
    lastRun = { o, count, diff, ok, cpuStats, gpuStats, cpu: cpu.result, gpu: gpu && gpu.result };
    document.getElementById('results').innerHTML =
      `<tr><td>${o.kernel}</td><td>${o.backend}</td><td>${count}</td><td>${diff.toExponential(2)}</td>
       <td class="${ok ? 'ok' : 'error'}">${ok ? 'PASS' : 'FAIL'}</td></tr>`;
    benchRows = [
      { backend: 'WASM', ...cpuStats },
      ...(gpuStats ? [{ backend: 'WebGPU', ...gpuStats }] : [])
    ];
    document.getElementById('benchRows').innerHTML = benchRows.map(b =>
      `<tr><td>${b.backend}</td><td>${b.median.toFixed(3)}</td><td>${b.p10.toFixed(3)}</td><td>${b.p90.toFixed(3)}</td></tr>`).join('');
    draw(gpu ? gpu.result : cpu.result, gpu ? 'WebGPU output' : 'WASM output');
    status.textContent = mode === 'bench' ? 'Benchmark complete.' : (ok ? 'PASS — results match.' : 'FAIL — mismatch.');
  } catch (e) { status.textContent = 'Not available.'; error.textContent = e.message; }
}

for (const k of Object.keys(KERNELS)) {
  const opt = document.createElement('option'); opt.value = k; opt.textContent = k;
  document.getElementById('kernel').appendChild(opt);
}
// Shareable configuration via URL parameters: ?kernel=scale_f32&backend=wasm&n=512
const query = new URLSearchParams(location.search);
for (const id of ['kernel', 'n', 'cols', 'k', 'alpha', 'groups', 'warmup', 'reps', 'backend']) {
  const value = query.get(id);
  if (value !== null && document.getElementById(id)) document.getElementById(id).value = value;
}
document.getElementById('run').addEventListener('click', () => mainRun('run'));
document.getElementById('bench').addEventListener('click', () => mainRun('bench'));
document.getElementById('race').addEventListener('click', async () => {
  await mainRun('bench');
  if (!lastRun || !lastRun.gpuStats) return;
  const gpu = lastRun.gpuStats.median, cpu = lastRun.cpuStats.median;
  const winner = gpu < cpu ? 'WebGPU' : 'WASM';
  const scores = JSON.parse(localStorage.getItem('hcsuda-race') || '{"WASM":0,"WebGPU":0}');
  scores[winner] = (scores[winner] || 0) + 1;
  localStorage.setItem('hcsuda-race', JSON.stringify(scores));
  document.getElementById('status').textContent =
    `Demo race winner: ${winner} (${Math.min(gpu, cpu).toFixed(3)} ms). ` +
    `Score WASM ${scores.WASM} : WebGPU ${scores.WebGPU}. Demo/synthetic, no performance claim.`;
});
document.getElementById('exportJson').addEventListener('click', () => {
  if (!lastRun) return;
  download('hcsuda-web-demo-result.json', JSON.stringify(lastRun, null, 2), 'application/json');
});
document.getElementById('exportCsv').addEventListener('click', () => {
  if (!lastRun) return;
  const lines = ['backend,median_ms,p10_ms,p90_ms',
    ...benchRows.map(b => `${b.backend},${b.median},${b.p10},${b.p90}`)];
  download('hcsuda-web-demo-benchmark.csv', lines.join('\n'), 'text/csv');
});
mainRun('run');
