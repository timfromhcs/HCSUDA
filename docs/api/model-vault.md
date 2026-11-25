# ModelVault — implemented storage behaviour (Wave 8, first slice)

This describes the code in `src/model_store/` today. There is **no inference engine** in this
build; every indexed model is `stored_only` (`runnable = false`).

## What it does

* Creates a user-selected model directory and a bounded JSON-lines catalog (`catalog.jsonl`).
* Imports a local file after resolving its canonical path with `std::filesystem::weakly_canonical`.
* Rejects paths outside the configured directory (HDD paths are supported as long as they are
  the configured directory), `..` in filenames, missing files, empty files and files above
  64 GiB.
* Computes SHA-256 with an in-tree FIPS 180-4 implementation (no external crypto dependency),
  streaming in 64 KiB chunks.
* Detects GGUF by magic and version 2/3, and classifies `.onnx`, `.safetensors`, `.pt/.pth/.bin`
  by extension. Other files are `unknown`.
* Records a quantization hint from the filename (`q4_k_m`, `q8_0`, `f16`, ...). The hint is
  metadata only and is never treated as proof of a supported precision.
* Persists the catalog atomically (temporary file + rename) and reloads it on the next
  `initialize()`. Maximum 512 models.
* `scan()` rechecks file existence and size; if a file changed it recomputes the checksum and
  updates the entry with an explicit note.

## What it deliberately does not do

* No downloads, no update checks, no network calls, no telemetry.
* No mapping of model bytes into GPU memory and no claim that HDD is execution memory.
* No model execution. GGUF is stored/indexed only; there is no llama.cpp adapter in V1 yet.
* No Blender/PyTorch pickle parsing; `.pt/.pth/.bin` are catalogued as stored-only.
* No global install paths: the caller chooses the directory, and all paths are validated against
  it.

## API surface (internal C++)

```cpp
hcs::models::ModelVault vault(userChosenDirectory);
vault.initialize();
hcs::models::ModelEntry entry;
vault.add(modelPath, &entry);      // hashes + validates + persists
std::vector<hcs::models::ModelEntry> models;
vault.list(&models);
vault.scan();                      // integrity recheck
vault.remove(entry.id);
```

The public C ABI and the service layer do not expose this yet; that follows with REST/MCP.

## Evidence

`tests/unit/test_model_vault.cpp`: SHA-256 vectors (empty, "abc", split updates), GGUF import
and catalog persistence, path-traversal/corrupt-file rejection, and change detection on rescan.
All 59 tests pass in both normal and validation-layer presets.
