# External blockers — owner action playbook

These four items cannot be closed by code or by this agent. Each has an exact action and evidence
requirement. Nothing here is marked green until the action is actually performed.

## 1. Code signing (no certificate configured)

Current state: `artifacts/HCSUDA-Setup-x64.exe` is unsigned; SHA-256 is the verification mechanism.

Owner action:

1. Obtain a code-signing certificate (OV/EV) and install it in the user/machine certificate store.
2. Locate the thumbprint: `Get-ChildItem Cert:\CurrentUser\My | Where-Object EnhancedKeyUsageList -match 'Code Signing'`.
3. Sign the built EXE with `signtool sign /sha1 <thumbprint> /fd SHA256 /tr <RFC3161 timestamp URL> artifacts\HCSUDA-Setup-x64.exe`.
4. Verify: `signtool verify /pa artifacts\HCSUDA-Setup-x64.exe`.
5. Record thumbprint (not the private key), timestamp and verification output in this file.

Never fabricate a signature or embed a certificate/private key in the repository.

## 2. Clean Windows 11 VM install

Current state: staged install/uninstall, in-place and cross-version upgrades pass on the reference
machine (INS-01..04). No Windows 11 VM image exists on this machine (only a Linux ISO).

Owner action:

1. Create a disposable Windows 11 x64 VM with no developer tools, Vulkan SDK or Visual Studio.
2. Copy `HCSUDA-Setup-x64.exe` and its `.sha256` into the VM; verify the checksum.
3. Run the EXE, launch installed `hcsdoctor.exe --json`, run one `hcsbench.exe --device 0` sample.
4. Confirm Start Menu shortcut, per-user paths, no driver/loader change; then uninstall and confirm
   user data policy.
5. Record VM build, driver state and outputs in `docs/TEST_MATRIX.md` as INS-05.

## 3. GitHub Release / tag — CLOSED 2026-11-28

Owner explicitly approved the release. Annotated tag `v0.1.0-rc.1` pushed and GitHub Release created
with `HCSUDA-Setup-x64.exe`, `.sha256` and `sbom.json`:
<https://github.com/timfromhcs/HCSUDA/releases/tag/v0.1.0-rc.1>. Exact commands are recorded in
`docs/release/publication.md`. This item is no longer open.

## 4. Cross-vendor hardware verification

Current state: AMD Radeon 680M (Vulkan 1.4.315) + CPU backend tested. Intel and NVIDIA not verified.

Owner action:

1. On an Intel Arc/Iris machine and an NVIDIA machine with current drivers, run `hcsdoctor --json`,
   `ctest --preset windows-tests` and `hcsbench --device 0 --reps 25 --warmup 5 --json`.
2. Confirm base compute, memory budget reporting and the six verified kernels on each device.
3. Record vendor/device IDs, driver versions and results as VK-07/VK-08 in `docs/TEST_MATRIX.md`.
4. Only then may cross-vendor language be strengthened; generic Vulkan support remains
   capability-dependent regardless.

---

All four items are tracked as unchecked in `docs/release/RELEASE_CHECKLIST.md`.
