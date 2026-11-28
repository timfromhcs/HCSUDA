// Browser demo: WebGPU vecadd vs WASM CPU reference. Truthful scope: no native
// HCSUDA runtime, no fabricated metrics, no performance claim.
const SHADER = `
@group(0) @binding(0) var<storage, read> a : array<f32>;
@group(0) @binding(1) var<storage, read> b : array<f32>;
@group(0) @binding(2) var<storage, read_write> c : array<f32>;
struct Params { n : u32, alpha : f32, pad0 : u32, pad1 : u32 }
@group(0) @binding(3) var<uniform> params : Params;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
  let i = gid.x;
  if (i >= params.n) { return; }
  c[i] = a[i] + params.alpha * b[i];
}`;

const N = 1001;
const ALPHA = 1.5;

function inputs() {
  const a = new Float32Array(N), b = new Float32Array(N);
  for (let i = 0; i < N; i++) { a[i] = i * 0.25; b[i] = 2.0; }
  return { a, b };
}

async function wasmReference({ a, b }) {
  const bytes = await (await fetch('./kernel.wasm')).arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const memory = new Float32Array(instance.exports.memory.buffer);
  memory.set(a, 0);
  memory.set(b, N);
  instance.exports.hcs_vecadd_ref(N, 0, N * 4, N * 8, ALPHA);
  return memory.slice(2 * N, 3 * N);
}

async function webgpuResult({ a, b }) {
  if (!navigator.gpu) throw new Error('WebGPU is not available in this browser');
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error('No WebGPU adapter');
  const device = await adapter.requestDevice();
  const bytes = N * 4;
  const mk = (data, usage) => {
    const buf = device.createBuffer({ size: bytes, usage });
    device.queue.writeBuffer(buf, 0, data);
    return buf;
  };
  const bufA = mk(a, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const bufB = mk(b, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const bufC = device.createBuffer({ size: bytes, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
  const out = device.createBuffer({ size: bytes, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const params = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
  const p = new ArrayBuffer(16);
  new Uint32Array(p)[0] = N;
  new Float32Array(p)[1] = ALPHA;
  device.queue.writeBuffer(params, 0, p);

  const layout = device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
    { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
    { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } }
  ]});
  const pipeline = device.createComputePipeline({
    layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
    compute: { module: device.createShaderModule({ code: SHADER }), entryPoint: 'main' }
  });
  const bind = device.createBindGroup({ layout, entries: [
    { binding: 0, resource: { buffer: bufA } }, { binding: 1, resource: { buffer: bufB } },
    { binding: 2, resource: { buffer: bufC } }, { binding: 3, resource: { buffer: params } }
  ]});
  const enc = device.createCommandEncoder();
  const pass = enc.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bind);
  pass.dispatchWorkgroups(Math.ceil(N / 64));
  pass.end();
  enc.copyBufferToBuffer(bufC, 0, out, 0, bytes);
  device.queue.submit([enc.finish()]);
  await out.mapAsync(GPUMapMode.READ);
  const result = new Float32Array(out.getMappedRange()).slice();
  out.unmap();
  return result;
}

document.getElementById('run').addEventListener('click', async () => {
  const status = document.getElementById('status');
  const detail = document.getElementById('detail');
  const error = document.getElementById('error');
  error.textContent = '';
  status.textContent = 'Running…';
  try {
    const data = inputs();
    const ref = await wasmReference(data);
    const gpu = await webgpuResult(data);
    let worst = 0;
    for (let i = 0; i < N; i++) worst = Math.max(worst, Math.abs(gpu[i] - ref[i]));
    if (worst > 1e-4) throw new Error(`mismatch: worst=${worst}`);
    status.textContent = 'PASS — WebGPU result matches the WASM CPU reference.';
    detail.textContent = `n=${N}, alpha=${ALPHA}, worst absolute difference=${worst.toExponential(2)}`;
  } catch (e) {
    status.textContent = 'Not available.';
    error.textContent = e.message;
  }
});
