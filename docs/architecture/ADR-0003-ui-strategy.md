# ADR-0003: Desktop UI strategy when Qt is unavailable

* Status: accepted with amendment — the WebView2 native shell was rejected; the V1 UI is
  a self-contained local page served by the REST service (implemented in Wave 10)
* Date: 2026-11-25

## Context

The specification prefers Qt 6 Quick/QML. The reference machine does not have Qt installed,
and bundling Qt adds a large redistributable surface plus LGPL obligations that need a
deliberate review. Windows 11 ships the WebView2 runtime (verified present), which can host a
local HTML/CSS/JS UI inside a native Win32 window.

## Options

| Option | Pros | Cons |
|---|---|---|
| Qt 6 Quick | Mature, native-feeling, strong charts | ~150 MB runtime, LGPL compliance work, not installed here |
| WebView2 shell | No bundled browser engine, modern UI, already present | Requires COM host code, Windows-only, WebView2 runtime dependency |
| Pure Win32 | Smallest dependency surface | Slow to build a polished, accessible UI |

## Proposal

Use a Win32 + WebView2 host for V1, with the UI implemented as static assets served from the
application directory (no remote content, no network). The native host exposes a narrow,
versioned message bridge to the service layer. All UI data must come from real runtime
queries; unavailable metrics render as "Not exposed by this driver/API".

Fallback: if WebView2 is missing, the app starts its local REST UI in the default browser and
shows a clear diagnostic. The installer will not install or replace a browser runtime
silently.

## Consequences

* No Qt license question in V1.
* The UI is Windows-only, consistent with the V1 platform scope.
* Accessibility and keyboard navigation must be validated in the WebView2 shell; this is a
  real risk and is tracked in Wave 10 acceptance tests.

## Amendment (2026-11-26)

The WebView2 host was not built. Instead, `src/api/rest/ui_page.cpp` serves a self-contained
HTML/CSS/JS console from `GET /ui/` on the localhost-only REST service. It uses only real API
data, renders "Not exposed by this driver/API" for unavailable metrics, and persists validated
settings through `/api/v1/settings`. No automated browser screenshot was produced, so no
screenshot is claimed as product evidence. This avoids Qt licensing and WebView2 COM hosting for
V1 while keeping every visible control connected to real state.
