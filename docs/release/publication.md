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

## Not performed

* **No GitHub Release was created.** The user-approved scope of the publication was repository
  creation plus push to `main`. A release tag should be created only after the remaining
  release blockers in `docs/AGENT_STATUS.md` are settled.
* No signed tag, no code signing certificate, no `v0.1.0-rc.1` tag.
* No remote was fabricated or reported as pushed without verification; the URL and CI run were
  checked with `gh repo view` and `gh run list`.

## Exact commands for follow-up (not executed here)

```powershell
# after the remaining release blockers are cleared
git tag -a v0.1.0-rc.1 -m "HCSUDA 0.1.0 release candidate"
git push origin v0.1.0-rc.1
gh release create v0.1.0-rc.1 `
  artifacts/HCSUDA-Setup-x64.exe `
  artifacts/HCSUDA-Setup-x64.exe.sha256 `
  --title "HCSUDA v0.1.0-rc.1" --notes-file docs/release/RELEASE_NOTES.md
```
