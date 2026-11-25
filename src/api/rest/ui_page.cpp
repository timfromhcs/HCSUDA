#include "api/rest/ui_page.h"

namespace hcs::rest {
namespace {

std::string escapeScript(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == '\'' || c == '<') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

} // namespace

std::string uiPage(const std::string& token)
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>HCSUDA — HCS Unified Compute Runtime</title>
<style>
  :root { color-scheme: dark; --bg:#10141b; --panel:#1a212c; --line:#2b3648; --text:#e6edf7; --accent:#5cc8ff; --warn:#ffcc66; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.5 "Segoe UI", system-ui, sans-serif; }
  header { padding:18px 28px; border-bottom:1px solid var(--line); }
  h1 { margin:0; font-size:20px; letter-spacing:.3px; }
  .tag { color:var(--accent); font-size:12px; }
  main { max-width:1100px; margin:0 auto; padding:24px; }
  section { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:18px; margin-bottom:18px; }
  h2 { margin:0 0 12px; font-size:16px; }
  table { width:100%; border-collapse:collapse; font-size:14px; }
  th,td { text-align:left; padding:7px 10px; border-bottom:1px solid var(--line); }
  th { color:var(--accent); font-weight:600; }
  button { background:var(--accent); color:#06202d; border:0; border-radius:6px; padding:8px 14px; font-weight:700; cursor:pointer; }
  code { color:var(--warn); }
  .note { color:#a9b7cc; font-size:13px; }
  .error { color:#ff8f8f; }
</style>
</head>
<body>
<header><h1>HCSUDA <span class="tag">HCS Unified Compute Runtime</span></h1>
<div class="note">Local-only runtime console. Made with love by HCSMedia &amp; timfromhcs.</div></header>
<main>
 <section>
  <h2>Dashboard</h2>
  <div id="status">Loading…</div>
  <p><button id="refresh">Refresh</button></p>
  <p class="note">Every value below is queried from the running runtime. Unavailable metrics are
  shown as “Not exposed by this driver/API” — no invented GPU utilization or temperature.</p>
  <p class="error" id="error"></p>
 </section>
 <section><h2>Devices</h2><div id="devices">Loading…</div></section>
 <section><h2>Memory budget</h2><div id="budget">Loading…</div></section>
 <section><h2>Settings (persisted locally)</h2>
  <div id="settings">Loading…</div>
  <p><button id="saveSettings">Save settings</button> <button id="resetSettings">Reset to defaults</button></p>
  <p class="note">Every setting has a default, is validated by the service and can be reset.
  The API token is intentionally not persisted.</p>
 </section>
 <section><h2>Compatibility notice</h2>
  <p>HCSUDA is an independent Vulkan compute runtime with CUDA-source migration tooling. It is
  not affiliated with NVIDIA, does not replace any GPU driver, and cannot execute CUBIN/SASS
  binaries on AMD hardware.</p>
 </section>
</main>
<script>
const TOKEN = ')HTML" + escapeScript(token) + R"HTML(';
async function get(path) {
  const r = await fetch(path, { headers: { 'Authorization': 'Bearer ' + TOKEN } });
  if (!r.ok) throw new Error(path + ' -> HTTP ' + r.status);
  return r.json();
}
function table(rows, columns) {
  if (!rows.length) return '<p class="note">No data.</p>';
  return '<table><thead><tr>' + columns.map(c => '<th>' + c.label + '</th>').join('') +
    '</tr></thead><tbody>' + rows.map(r => '<tr>' + columns.map(c =>
      '<td>' + (r[c.key] === undefined || r[c.key] === null ? 'Not exposed by this driver/API' : r[c.key]) +
      '</td>').join('') + '</tr>').join('') + '</tbody></table>';
}
async function refresh() {
  document.getElementById('error').textContent = '';
  try {
    const health = await get('/api/v1/health');
    const version = await get('/api/v1/version');
    const status = await get('/api/v1/runtime/status');
    document.getElementById('status').innerHTML =
      'Runtime: <code>' + health.status + '</code> · Product: ' + version.product +
      ' · Version: ' + version.version + ' · C ABI: ' + version.api_version +
      ' · Devices: ' + status.devices.length;
    const devices = status.devices;
    document.getElementById('devices').innerHTML = table(devices, [
      { key: 'index', label: 'Index' }, { key: 'name', label: 'Device' },
      { key: 'backend', label: 'Backend' }, { key: 'kind', label: 'Kind' }
    ]);
    const budget = await get('/api/v1/memory/budget');
    document.getElementById('budget').innerHTML = table(budget.devices, [
      { key: 'index', label: 'Device' },
      { key: 'heap_bytes', label: 'Heap bytes (reported)' },
      { key: 'budget_bytes', label: 'Budget bytes (reported)' },
      { key: 'usage_bytes', label: 'Usage bytes (reported)' }
    ]) + '<p class="note">Heap/budget/usage come from Vulkan and are not a VRAM size.</p>';
  } catch (e) {
    document.getElementById('error').textContent = 'Request failed: ' + e.message;
  }
}
async function loadSettings() {
  try {
    const s = await get('/api/v1/settings');
    document.getElementById('settings').innerHTML =
      '<label>Backend policy <select id="pol"><option value="0">Auto</option>' +
      '<option value="1">Vulkan</option><option value="2">CPU</option></select></label> ' +
      'Memory cap (MiB) <input id="cap" type="number" min="0" max="65536" value="' + s.memory_cap_mib + '"> ' +
      'CPU threads <input id="threads" type="number" min="0" max="256" value="' + s.cpu_threads + '"> ' +
      'Theme <select id="theme"><option value="dark">Dark</option>' +
      '<option value="light">Light</option></select> ' +
      'Prefetch <select id="pref"><option value="off">Off</option>' +
      '<option value="conservative">Conservative</option><option value="adaptive">Adaptive</option></select> ' +
      '<p class="note">Model dir: ' + (s.model_dir || 'not set') + ' · API port: ' + s.api_port +
      ' · Start with Windows: ' + s.start_with_windows + ' · Update check: ' + s.update_check + '</p>';
    document.getElementById('pol').value = String(s.backend_policy);
    document.getElementById('theme').value = s.theme;
    document.getElementById('pref').value = s.prefetch_policy;
  } catch (e) { document.getElementById('settings').textContent = 'Request failed: ' + e.message; }
}
async function postSettings(path, body) {
  try {
    const r = await fetch(path, { method: 'POST',
      headers: { 'Authorization': 'Bearer ' + TOKEN, 'Content-Type': 'application/json' },
      body: body });
    const text = await r.text();
    if (!r.ok) throw new Error('HTTP ' + r.status + ' ' + text);
    await loadSettings();
  } catch (e) { document.getElementById('settings').textContent = 'Save failed: ' + e.message; }
}
document.getElementById('saveSettings').addEventListener('click', () => {
  const body = JSON.stringify({
    backend_policy: Number(document.getElementById('pol').value),
    memory_cap_mib: Number(document.getElementById('cap').value),
    cpu_threads: Number(document.getElementById('threads').value),
    theme: document.getElementById('theme').value,
    prefetch_policy: document.getElementById('pref').value
  });
  postSettings('/api/v1/settings', body);
});
document.getElementById('resetSettings').addEventListener('click', () =>
  postSettings('/api/v1/settings/reset', ''));
document.getElementById('refresh').addEventListener('click', () => { refresh(); loadSettings(); });
refresh();
loadSettings();
</script>
</body>
</html>
)HTML";
}

} // namespace hcs::rest
