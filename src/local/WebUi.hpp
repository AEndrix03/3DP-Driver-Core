#pragma once

namespace local {

/// Pagina di gestione, servita dal servizio stesso: usa solo le API /api/* (le stesse della CLI).
    inline const char *WEB_UI = R"HTML(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>3DP locale</title>
<style>
:root { --bg:#f4f5f7; --panel:#fff; --ink:#1c2330; --muted:#667085; --line:#d9dde4; --accent:#1f6feb; --danger:#c62828; --ok:#1a7f37; --warn:#b26a00; }
@media (prefers-color-scheme: dark) { :root { --bg:#12151b; --panel:#1b2029; --ink:#e6e9ef; --muted:#9aa4b2; --line:#2e3542; --accent:#58a6ff; --danger:#ff6b6b; --ok:#4ac26b; --warn:#e3b341; } }
* { box-sizing: border-box; }
body { margin:0; padding:16px; background:var(--bg); color:var(--ink); font:15px/1.4 system-ui, sans-serif; }
h1 { font-size:20px; margin:0 0 12px; }
h2 { font-size:14px; margin:0 0 10px; color:var(--muted); text-transform:uppercase; letter-spacing:.04em; }
.grid { display:grid; gap:12px; grid-template-columns:repeat(auto-fit, minmax(290px, 1fr)); }
.card { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:14px; }
.row { display:flex; gap:8px; flex-wrap:wrap; align-items:center; margin-bottom:8px; }
.kv { display:grid; grid-template-columns:auto 1fr; gap:4px 12px; }
.kv span:nth-child(odd) { color:var(--muted); }
button { font:inherit; padding:8px 12px; border-radius:8px; border:1px solid var(--line); background:var(--panel); color:var(--ink); cursor:pointer; }
button:hover { border-color:var(--accent); }
button.primary { background:var(--accent); border-color:var(--accent); color:#fff; }
button.danger { background:var(--danger); border-color:var(--danger); color:#fff; font-weight:600; }
input, select { font:inherit; padding:7px 9px; border-radius:8px; border:1px solid var(--line); background:var(--panel); color:var(--ink); min-width:0; }
input[type=number] { width:86px; }
.jog { display:grid; grid-template-columns:repeat(3, 56px); gap:6px; }
.jog button { padding:10px 0; }
.bar { height:10px; background:var(--line); border-radius:6px; overflow:hidden; }
.bar > div { height:100%; background:var(--accent); width:0; }
.badge { padding:2px 8px; border-radius:999px; border:1px solid var(--line); font-size:13px; }
.ok { color:var(--ok); } .bad { color:var(--danger); } .warn { color:var(--warn); }
table { width:100%; border-collapse:collapse; font-size:14px; }
th, td { text-align:left; padding:6px 8px; border-bottom:1px solid var(--line); }
#log { font:13px/1.35 ui-monospace, monospace; white-space:pre-wrap; max-height:160px; overflow:auto; color:var(--muted); }
.wrap { overflow-x:auto; }
</style>
</head>
<body>
<h1>3DP &mdash; gestione locale <span id="conn" class="badge">...</span></h1>
<div class="grid">
  <div class="card">
    <h2>Stato</h2>
    <div class="kv">
      <span>Macchina</span><span id="state">-</span>
      <span>Stampa</span><span id="job">-</span>
      <span>Ugello</span><span id="temp">-</span>
      <span>Posizione</span><span id="pos">-</span>
      <span>Coda</span><span id="queue">-</span>
      <span>Guasto</span><span id="fault">-</span>
      <span>Ultima stampa</span><span id="last">-</span>
    </div>
    <div class="bar" style="margin:10px 0"><div id="progress"></div></div>
    <div class="row">
      <button onclick="api('pause')">Pausa</button>
      <button onclick="api('resume')">Riprendi</button>
      <button onclick="if(confirm('Annullare la stampa?')) api('cancel')">Annulla</button>
      <button onclick="api('clear')">Azzera errore</button>
    </div>
    <button class="danger" style="width:100%" onclick="api('estop')">ARRESTO DI EMERGENZA</button>
  </div>

  <div class="card">
    <h2>Movimento</h2>
    <div class="row">
      <label>Passo <select id="step"><option>0.1</option><option selected>1</option><option>10</option><option>50</option></select> mm</label>
      <label>Velocit&agrave; <input id="feed" type="number" value="3000"> mm/min</label>
    </div>
    <div class="row">
      <div class="jog">
        <span></span><button onclick="jog('y',1)">Y+</button><span></span>
        <button onclick="jog('x',-1)">X&minus;</button><button onclick="api('home')">Home</button><button onclick="jog('x',1)">X+</button>
        <span></span><button onclick="jog('y',-1)">Y&minus;</button><span></span>
      </div>
      <div class="jog" style="grid-template-columns:56px">
        <button onclick="jog('z',1)">Z+</button><button onclick="api('home','axes=Z')">Home Z</button><button onclick="jog('z',-1)">Z&minus;</button>
      </div>
      <div class="jog" style="grid-template-columns:70px">
        <button onclick="jog('e',1)">Estrudi</button><button onclick="jog('e',-1)">Ritrai</button>
      </div>
    </div>
    <div class="row">
      <label>Ugello <input id="target" type="number" value="200"> &deg;C</label>
      <button onclick="api('temp','s='+val('target'))">Imposta</button>
      <button onclick="api('temp','s=0')">Spegni</button>
      <label>Ventola <input id="fan" type="number" value="100"> %</label>
      <button onclick="api('fan','s='+val('fan'))">Imposta</button>
    </div>
  </div>

  <div class="card">
    <h2>Stampa</h2>
    <div class="row"><input id="file" type="file" accept=".gcode,.gco,.g,.stl,.3mf,.obj"><button class="primary" onclick="uploadAndPrint()">Carica e stampa</button></div>
    <div class="row">
      <label>Strato <input id="layer" type="number" step="0.05" value="0.2"></label>
      <label>Riempimento <input id="infill" type="number" value="20"> %</label>
      <label>Ugello <input id="stemp" type="number" value="205"> &deg;C</label>
      <label><input id="supports" type="checkbox"> supporti</label>
    </div>
    <div id="report" style="color:var(--muted);font-size:14px;margin-bottom:8px">Un modello (.stl) viene prima affettato con il profilo della macchina; un G-code viene controllato e stampato.</div>
    <div class="row"><input id="path" placeholder="oppure percorso di un file sul computer" style="flex:1"><button onclick="api('print','file='+encodeURIComponent(val('path')))">Stampa</button></div>
    <div class="row"><input id="gcode" placeholder="comando G-code, es. M114" style="flex:1"><button onclick="sendGcode()">Invia</button></div>
    <div id="log"></div>
  </div>

  <div class="card" style="grid-column:1/-1">
    <h2>Storico</h2>
    <div class="wrap"><table><thead><tr><th>Inizio</th><th>Nome</th><th>Esito</th><th>Righe</th><th>Durata</th><th>Note</th></tr></thead><tbody id="history"></tbody></table></div>
  </div>
</div>
<script>
const $ = id => document.getElementById(id);
const val = id => $(id).value;
function log(text) { const el = $('log'); el.textContent = new Date().toLocaleTimeString() + '  ' + text + '\n' + el.textContent.slice(0, 4000); }
async function api(name, query, options) {
  try {
    const response = await fetch('/api/' + name + (query ? '?' + query : ''), Object.assign({method: 'POST'}, options || {}));
    const data = await response.json();
    log(name + ': ' + (data.ok ? 'ok' : 'ERRORE') + (data.message ? ' - ' + data.message : ''));
    refresh();
    return data;
  } catch (e) { log(name + ': servizio non raggiungibile'); }
}
function jog(axis, sign) {
  const amount = axis === 'e' ? 5 * sign : Number(val('step')) * sign;
  api('jog', axis + '=' + amount + '&f=' + (axis === 'z' ? 300 : axis === 'e' ? 300 : val('feed')));
}
function sendGcode() { api('gcode', 'line=' + encodeURIComponent(val('gcode'))); }
function describe(report) {
  if (!report) return '';
  const b = report.bounds;
  let text = report.moves + ' movimenti (' + report.arcs + ' archi), ' + report.layers + ' strati, filamento ' + (report.filamentMm / 1000).toFixed(2) + ' m, ingombro '
    + (b.maxX - b.minX).toFixed(0) + ' x ' + (b.maxY - b.minY).toFixed(0) + ' x ' + b.maxZ.toFixed(1) + ' mm.';
  if (report.problems.length) text += ' NON STAMPABILE: ' + report.problems.join('; ') + '.';
  if (report.warnings.length) text += ' Avvisi: ' + report.warnings.join('; ') + '.';
  return text;
}
async function uploadAndPrint() {
  const file = $('file').files[0];
  if (!file) { log('nessun file scelto'); return; }
  const isModel = /\.(stl|3mf|obj)$/i.test(file.name);
  const uploaded = await api('upload', 'name=' + encodeURIComponent(file.name), {body: await file.arrayBuffer()});
  if (!uploaded || !uploaded.ok) return;
  let path = uploaded.path, extra = '';
  if (isModel) {
    $('report').textContent = 'Slicing in corso...';
    const sliced = await api('slice', 'file=' + encodeURIComponent(path) + '&layer=' + val('layer') + '&infill=' + val('infill')
      + '&temp=' + val('stemp') + '&supports=' + ($('supports').checked ? 1 : 0));
    $('report').textContent = sliced ? (sliced.report ? describe(sliced.report) : (sliced.message || '')) : '';
    if (!sliced || !sliced.ok) return;
    extra = '&source=' + encodeURIComponent(sliced.source) + '&slicer=' + encodeURIComponent(sliced.slicer);
    path = sliced.path;
  } else {
    const checked = await api('check', 'file=' + encodeURIComponent(path));
    $('report').textContent = checked && checked.report ? describe(checked.report) : '';
    if (!checked || !checked.ok) return;
  }
  if (!confirm('Avviare la stampa di ' + file.name + '?')) return;
  api('print', 'file=' + encodeURIComponent(path) + '&name=' + encodeURIComponent(file.name) + extra);
}
const esc = t => String(t == null ? '' : t).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
function span(a, b) { if (!a) return '-'; const s = (b || Date.now() / 1000) - a; return s < 90 ? Math.round(s) + ' s' : Math.round(s / 60) + ' min'; }
async function refresh() {
  try {
    const s = await (await fetch('/api/status')).json();
    $('conn').textContent = s.connected ? 'collegata' : 'scollegata';
    $('conn').className = 'badge ' + (s.connected ? 'ok' : 'bad');
    $('state').textContent = s.state;
    $('state').className = s.state === 'Error' ? 'bad' : '';
    $('job').textContent = s.job.id ? s.job.state + '  ' + s.job.percent.toFixed(1) + '%  (' + s.job.linesDone + '/' + s.job.linesTotal + ')' : 'nessuna';
    $('progress').style.width = (s.job.id ? s.job.percent : 0) + '%';
    $('temp').textContent = s.temperature.known ? s.temperature.current.toFixed(1) + ' / ' + s.temperature.target.toFixed(0) + ' °C' : 'sensore non attivo';
    $('pos').textContent = s.position.known ? 'X' + s.position.x.toFixed(2) + ' Y' + s.position.y.toFixed(2) + ' Z' + s.position.z.toFixed(2) + (s.position.referenced ? '' : ' (non riferita)') : '-';
    $('queue').textContent = s.queue;
    $('fault').textContent = s.fault || '-';
    $('fault').className = s.fault ? 'warn' : '';
    $('last').textContent = s.lastResult || '-';
    const rows = (await (await fetch('/api/history')).json()).prints || [];
    $('history').innerHTML = rows.map(r => '<tr><td>' + new Date(r.startedAt * 1000).toLocaleString() + '</td><td>' + esc(r.name) + '</td><td>' + esc(r.state) + '</td>'<td>' + r.lines + '</td><td>' + span(r.startedAt, r.finishedAt) + '</td><td>' + esc(r.error) + '</td></tr>').join('');
  } catch (e) { $('conn').textContent = 'servizio fermo'; $('conn').className = 'badge bad'; }
}
setInterval(refresh, 1500); refresh();
</script>
</body>
</html>
)HTML";
} // namespace local
