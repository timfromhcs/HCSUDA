# HCSUDA WebGPU / WebAssembly browser demo

A **standalone browser demo** of the documented `vecadd_f32` kernel shape: WebGPU executes the
compute shader, a small WebAssembly module computes the CPU reference, and the page compares both.

This is **not** the native HCSUDA runtime. It does not use the Vulkan backend, the C ABI, the model
vault or any HCSUDA service. It makes no performance claim and invents no metrics. If WebGPU is
unavailable, the page says so explicitly.

Build and test:

```powershell
powershell -File build.ps1
node test-wasm.mjs
```

Serve this directory as static files (`index.html`, `app.js`, `kernel.wasm`). Deployment target:
Vercel project `hcsuda-webgpu-demo` under the authenticated `timfromhcs` account.
