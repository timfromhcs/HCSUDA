# GitHub publication

Performed 2026-11-26 after explicit owner confirmation of the target repository.

* Repository: <https://github.com/timfromhcs/HCSUDA> (public, default branch `main`).
* The `HCSMedia` organization does not exist; `HCSMedia/HCSUDA` could not be resolved. The
  repository was therefore created under the authenticated account `timfromhcs`, not invented
  under a foreign namespace.
* Remote: `origin https://github.com/timfromhcs/HCSUDA.git`.
* Publication command used: `gh repo create timfromhcs/HCSUDA --public --source=. --remote=origin --push`.
* Secret scan before push: `scripts/check-secrets.ps1` → PASS (104 tracked files).
* Pushed commit at publication time: `02f6cfd`.
* CI workflow `CI` was triggered automatically by the push.

## Release created (explicit owner approval, 2026-11-28)

* Annotated tags `v0.1.0-rc.1` and `v0.1.0-rc.2` pushed to `origin`.
* Latest GitHub Release: <https://github.com/timfromhcs/HCSUDA/releases/tag/v0.1.0-rc.2>
  (previous: <https://github.com/timfromhcs/HCSUDA/releases/tag/v0.1.0-rc.1>).
* Assets verified: `HCSUDA-Setup-x64.exe`, `HCSUDA-Setup-x64.exe.sha256`, `sbom.json`.
  rc.2 EXE SHA-256: `1893992BC683CAF633E7E706D3514BCFFF8150EDCEF70B79D5F0CE26399047D2`.
* Cloud release: `https://hcsuda-webgpu-demo.vercel.app` (production alias, public, READY).
* Notes: `docs/release/RELEASE_NOTES.md`.

Commands used:

```powershell
git tag -a v0.1.0-rc.1 -m "HCSUDA 0.1.0 release candidate"
git push origin v0.1.0-rc.1
gh release create v0.1.0-rc.1 artifacts\HCSUDA-Setup-x64.exe artifacts\HCSUDA-Setup-x64.exe.sha256 docs\release\sbom.json --title "HCSUDA v0.1.0-rc.1" --notes-file docs\release\RELEASE_NOTES.md
```

## Still not performed

* No signed tag and no code signing certificate; the EXE is honestly unsigned with a published SHA-256.
* No clean-VM or cross-vendor hardware verification; see `docs/release/EXTERNAL_BLOCKERS.md`.
