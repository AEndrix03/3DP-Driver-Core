#pragma once

namespace local {

/// Pagina di gestione, servita dal servizio stesso: usa solo le API /api/* (le stesse della CLI).
/// Nessuna risorsa esterna: funziona anche senza rete. Il visualizzatore e' WebGL2 scritto a mano.
    inline const char *WEB_UI = R"HTML(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>3DP</title>
<style>
:root {
  --bg:#0d0f12; --panel:#15181d; --raised:#1c2027; --line:#272c35; --ink:#e9ebee; --muted:#8a92a0;
  --accent:#ff7a1a; --on-accent:#1b0e03; --ok:#3ecf8e; --warn:#f5b83d; --danger:#ff4d4f; --cool:#58a6ff;
  --path-lo:#4f8cff; --path-mid:#c65bd8; --path-hi:#ff7a1a;
  --mono: ui-monospace, "Cascadia Mono", "SF Mono", Consolas, monospace;
  --sans: "Segoe UI Variable Text", "Segoe UI", system-ui, -apple-system, sans-serif;
}
@media (prefers-color-scheme: light) { :root {
  --bg:#eef0f3; --panel:#ffffff; --raised:#f5f6f8; --line:#dfe3e9; --ink:#141820; --muted:#5d6675;
  --accent:#e8590c; --on-accent:#ffffff; --ok:#12855a; --warn:#a86400; --danger:#d32f2f; --cool:#1f6feb;
  --path-lo:#2f6fe4; --path-mid:#a43bbd; --path-hi:#e8590c;
} }
* { box-sizing:border-box; }
[hidden] { display:none !important; }
html, body { height:100%; }
body { margin:0; background:var(--bg); color:var(--ink); font:14px/1.45 var(--sans); -webkit-font-smoothing:antialiased; }
button, input, select { font:inherit; color:inherit; }
.num { font-family:var(--mono); font-variant-numeric:tabular-nums; }

/* ---------- testata ---------- */
header { position:sticky; top:0; z-index:5; display:flex; align-items:center; gap:12px; padding:9px 18px; flex-wrap:wrap;
  background:color-mix(in srgb, var(--bg) 88%, transparent); backdrop-filter:blur(10px); border-bottom:1px solid var(--line); }
.brand { display:flex; align-items:center; gap:10px; font-weight:650; letter-spacing:.02em; font-size:16px; }
.mode { display:inline-flex; align-items:center; gap:8px; padding:5px 12px; border-radius:9px; font-weight:650; font-size:12.5px;
  letter-spacing:.07em; text-transform:uppercase; border:1px solid var(--line); background:var(--panel); }
.mode i { width:8px; height:8px; border-radius:50%; background:currentColor; }
.mode.idle { color:var(--ok); } .mode.print { color:var(--accent); } .mode.sim { color:var(--warn); } .mode.off { color:var(--danger); }
.mode small { font-weight:500; letter-spacing:0; text-transform:none; color:var(--muted); font-size:12.5px; }
.pill { display:inline-flex; align-items:center; gap:7px; padding:4px 10px; border-radius:999px; border:1px solid var(--line);
  background:var(--panel); font-size:12.5px; color:var(--muted); white-space:nowrap; }
.tabs { display:flex; gap:2px; margin-left:8px; }
.tabs button { border:0; background:transparent; padding:7px 12px; border-radius:8px; cursor:pointer; color:var(--muted); font-weight:550; }
.tabs button:hover { color:var(--ink); }
.tabs button[aria-selected=true] { background:var(--raised); color:var(--ink); box-shadow:inset 0 -2px 0 var(--accent); }
.grow { flex:1; }
.estop { border:0; border-radius:10px; padding:9px 16px; background:var(--danger); color:#fff; font-weight:700; letter-spacing:.04em;
  cursor:pointer; box-shadow:0 0 0 1px rgba(0,0,0,.2), 0 6px 18px -8px var(--danger); }
.estop:hover { filter:brightness(1.08); } .estop:active { transform:translateY(1px); }

/* ---------- impaginazione ---------- */
main { padding:14px 18px 24px; max-width:1760px; margin:0 auto; }
.split { display:grid; grid-template-columns:minmax(0, 1fr) 380px; gap:14px; }
.col { display:flex; flex-direction:column; gap:14px; min-width:0; }
.cards { display:grid; grid-template-columns:repeat(auto-fill, minmax(330px, 1fr)); gap:14px; }
.card { background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:14px; }
.card > h2 { margin:0 0 12px; font-size:11.5px; font-weight:650; color:var(--muted); text-transform:uppercase; letter-spacing:.09em;
  display:flex; align-items:center; justify-content:space-between; gap:8px; }
.card > p { margin:-4px 0 12px; color:var(--muted); font-size:13px; }
.row { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
.row + .row { margin-top:8px; }
@media (max-width: 1060px) { .split { grid-template-columns:1fr; } }

/* ---------- controlli ---------- */
.btn { border:1px solid var(--line); background:var(--raised); border-radius:9px; padding:7px 12px; cursor:pointer;
  transition:border-color .12s, background .12s; }
.btn:hover { border-color:color-mix(in srgb, var(--accent) 55%, var(--line)); }
.btn:active { transform:translateY(1px); }
.btn:disabled { opacity:.4; cursor:not-allowed; }
.btn.primary { background:var(--accent); border-color:var(--accent); color:var(--on-accent); font-weight:650; }
.btn.ghost { background:transparent; }
.btn.warn { border-color:color-mix(in srgb, var(--danger) 60%, var(--line)); color:var(--danger); }
.btn.small { padding:4px 9px; font-size:12.5px; border-radius:7px; }
.btn[aria-pressed=true] { border-color:var(--accent); color:var(--accent); }
:is(.btn, .estop, input, select, .seg button, .tabs button):focus-visible { outline:2px solid var(--cool); outline-offset:2px; }
input[type=number], input[type=text], select { background:var(--bg); border:1px solid var(--line); border-radius:9px; padding:7px 9px; min-width:0; }
input[type=number] { width:84px; font-family:var(--mono); }
input:disabled, select:disabled { opacity:.45; }
label.f { display:flex; flex-direction:column; gap:4px; font-size:12px; color:var(--muted); }
label.f > span { white-space:nowrap; }
.seg { display:inline-flex; border:1px solid var(--line); border-radius:9px; overflow:hidden; background:var(--bg); }
.seg button { border:0; background:transparent; padding:6px 11px; cursor:pointer; color:var(--muted); font-family:var(--mono); font-size:12.5px; }
.seg button[aria-pressed=true] { background:var(--raised); color:var(--ink); box-shadow:inset 0 -2px 0 var(--accent); }
.seg button:disabled { opacity:.4; cursor:not-allowed; }
input[type=range] { accent-color:var(--accent); }
.slider { display:grid; grid-template-columns:1fr auto; gap:2px 10px; align-items:center; font-size:12.5px; color:var(--muted); }
.slider input { grid-column:1 / -1; width:100%; }
.slider b { color:var(--ink); font-family:var(--mono); font-weight:600; }
.locked { margin-top:10px; font-size:12.5px; color:var(--muted); }

/* ---------- visualizzatore ---------- */
.viewer { background:var(--panel); border:1px solid var(--line); border-radius:14px; overflow:hidden; display:flex; flex-direction:column; }
.stage { position:relative; height:min(62vh, 640px); min-height:380px; background:
  radial-gradient(110% 80% at 50% 0%, color-mix(in srgb, var(--raised) 85%, transparent), transparent 75%), var(--panel); }
#view { position:absolute; inset:0; width:100%; height:100%; display:block; cursor:grab; touch-action:none; }
#view.drag { cursor:grabbing; }
.hud { position:absolute; left:14px; top:12px; right:250px; pointer-events:none; }
.hud .name { font-size:15px; font-weight:650; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.hud .facts { margin-top:6px; display:flex; gap:6px; flex-wrap:wrap; }
.chip { font-size:12px; padding:3px 8px; border-radius:7px; background:color-mix(in srgb, var(--bg) 70%, transparent);
  border:1px solid var(--line); color:var(--muted); backdrop-filter:blur(6px); }
.chip b { color:var(--ink); font-weight:600; font-family:var(--mono); }
.chip.live { color:var(--ok); border-color:color-mix(in srgb, var(--ok) 45%, var(--line)); }
.tools { position:absolute; right:12px; top:12px; display:flex; flex-direction:column; gap:6px; align-items:flex-end; }
.cut { position:absolute; right:16px; top:96px; bottom:18px; display:flex; flex-direction:column; align-items:center; gap:6px; color:var(--muted); font-size:11px; }
.cut input { writing-mode:vertical-lr; direction:rtl; flex:1; width:14px; opacity:.75; accent-color:var(--muted); }
.cut input:hover, .cut input:focus { opacity:1; }
.hint { position:absolute; left:14px; bottom:10px; font-size:11.5px; color:var(--muted); pointer-events:none; opacity:.8; }
.transport { display:grid; grid-template-columns:auto auto minmax(120px, 1fr) auto; gap:12px; align-items:center; padding:10px 14px;
  border-top:1px solid var(--line); background:var(--raised); }
.play { width:38px; height:38px; border-radius:50%; border:0; background:var(--accent); color:var(--on-accent); cursor:pointer; display:grid; place-items:center; }
.play:disabled { opacity:.4; cursor:not-allowed; }
.clock { text-align:right; line-height:1.25; }
.clock .big { font:600 14px var(--mono); }
.clock .sub { font-size:11.5px; color:var(--muted); }
#timeline { width:100%; }

/* ---------- telemetria e seriale ---------- */
.tele { display:grid; grid-template-columns:repeat(auto-fit, minmax(118px, 1fr)); border-top:1px solid var(--line); }
.tele div { padding:9px 14px; border-right:1px solid var(--line); min-width:0; }
.tele div:last-child { border-right:0; }
.tele > div:first-child { grid-column:span 2; }
.tele span { display:block; font-size:10.5px; text-transform:uppercase; letter-spacing:.08em; color:var(--muted); white-space:nowrap; }
.tele b { display:block; font:600 15px var(--mono); margin-top:2px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.tele b small { font-size:11px; color:var(--muted); font-weight:500; }
.tele .meter { height:3px; background:var(--line); border-radius:3px; margin-top:5px; overflow:hidden; padding:0; border:0; }
.tele .meter i { display:block; height:100%; width:0; background:var(--cool); transition:width .2s; }
#wire { font:12px/1.5 var(--mono); height:230px; overflow:auto; background:var(--bg); border:1px solid var(--line); border-radius:10px; padding:6px 0; }
#wire div { display:grid; grid-template-columns:62px 18px 150px minmax(0, 1fr) minmax(0, 1.25fr); gap:8px; padding:0 10px; white-space:nowrap; }
#wire div:hover { background:var(--raised); }
#wire .t { color:var(--muted); text-align:right; }
#wire .tx { color:var(--accent); } #wire .rx { color:var(--ok); }
#wire .k { font-weight:600; overflow:hidden; text-overflow:ellipsis; }
#wire .hex { color:var(--muted); overflow:hidden; text-overflow:ellipsis; }
#wire .txt { overflow:hidden; text-overflow:ellipsis; }
#wire .err .txt { color:var(--danger); }
#wire .gap { display:block; color:var(--muted); padding:0 10px; }

/* ---------- stato stampa ---------- */
.jobline { display:flex; align-items:baseline; justify-content:space-between; gap:10px; }
.jobline .pct { font:600 30px/1 var(--mono); letter-spacing:-.02em; }
.jobline .what { color:var(--muted); text-align:right; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.bar { height:6px; border-radius:6px; background:var(--line); overflow:hidden; margin:10px 0 12px; }
.bar > div { height:100%; width:0; background:linear-gradient(90deg, var(--path-lo), var(--path-mid), var(--path-hi)); transition:width .5s; }
.kv { display:grid; grid-template-columns:auto 1fr; gap:5px 14px; font-size:13px; margin:0; }
.kv dt { color:var(--muted); } .kv dd { margin:0; text-align:right; font-family:var(--mono); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.note { margin-top:10px; padding:8px 10px; border-radius:9px; font-size:13px; border:1px solid color-mix(in srgb, var(--warn) 45%, var(--line));
  color:var(--warn); background:color-mix(in srgb, var(--warn) 9%, transparent); }
.note.bad { border-color:color-mix(in srgb, var(--danger) 50%, var(--line)); color:var(--danger); background:color-mix(in srgb, var(--danger) 9%, transparent); }
.note.good { border-color:color-mix(in srgb, var(--ok) 45%, var(--line)); color:var(--ok); background:color-mix(in srgb, var(--ok) 9%, transparent); }

.temp { display:flex; align-items:flex-end; justify-content:space-between; gap:12px; }
.temp .now { font:600 34px/1 var(--mono); letter-spacing:-.03em; }
.temp .now small { font-size:15px; color:var(--muted); font-weight:500; }
#spark { width:150px; height:46px; display:block; }
.pad { display:grid; grid-template-columns:repeat(3, 46px) 14px 46px 14px 62px; grid-auto-rows:46px; gap:6px; align-items:stretch; }
.pad .btn { padding:0; display:grid; place-items:center; font-family:var(--mono); font-size:12.5px; }
.pad .home { background:color-mix(in srgb, var(--accent) 16%, var(--raised)); }
.drop { display:block; border:1.5px dashed var(--line); border-radius:12px; padding:16px 12px; text-align:center; color:var(--muted); cursor:pointer;
  transition:border-color .12s, background .12s; }
.drop:hover, .drop.over { border-color:var(--accent); background:color-mix(in srgb, var(--accent) 7%, transparent); }
.drop b { color:var(--ink); font-weight:600; }
.drop input { display:none; }
#report { font-size:13px; color:var(--muted); margin-top:10px; }
#report.bad { color:var(--danger); }
details { margin-top:10px; } summary { cursor:pointer; color:var(--muted); font-size:12.5px; }
details > div { margin-top:8px; }
#log { font:12px/1.4 var(--mono); white-space:pre-wrap; max-height:120px; overflow:auto; color:var(--muted); margin-top:8px; }
pre.out { font:12px/1.45 var(--mono); white-space:pre-wrap; background:var(--bg); border:1px solid var(--line); border-radius:9px; padding:8px 10px; margin:10px 0 0; max-height:180px; overflow:auto; }

/* ---------- configurazione ---------- */
.cfg { display:grid; grid-template-columns:minmax(0, 1fr) auto; gap:4px 14px; align-items:center; padding:9px 0; border-top:1px solid var(--line); }
.cfg:first-of-type { border-top:0; }
.cfg .lab { font-weight:550; }
.cfg .help { grid-column:1 / -1; font-size:12.5px; color:var(--muted); }
.cfg .inp { display:flex; align-items:center; gap:6px; color:var(--muted); font-size:12.5px; }
.cfg input[type=text] { width:230px; }
.cfg.changed .lab::after { content:" \25CF"; color:var(--accent); }
.savebar { position:sticky; bottom:0; display:flex; gap:12px; align-items:center; padding:12px 0; margin-top:14px;
  background:linear-gradient(transparent, var(--bg) 35%); }

/* ---------- storico ---------- */
.wrap { overflow-x:auto; }
table { width:100%; border-collapse:collapse; font-size:13px; }
th { text-align:left; font-weight:600; color:var(--muted); font-size:11.5px; text-transform:uppercase; letter-spacing:.07em; padding:6px 10px; border-bottom:1px solid var(--line); }
td { padding:8px 10px; border-bottom:1px solid var(--line); white-space:nowrap; vertical-align:top; }
td.wide { white-space:normal; color:var(--muted); }
.tag { font-size:11.5px; padding:2px 8px; border-radius:999px; border:1px solid var(--line); color:var(--muted); white-space:nowrap; }
.tag.COMPLETED { color:var(--ok); border-color:color-mix(in srgb, var(--ok) 40%, var(--line)); }
.tag.FAILED, .tag.INTERRUPTED, .tag.bad { color:var(--danger); border-color:color-mix(in srgb, var(--danger) 40%, var(--line)); }
.tag.RUNNING { color:var(--accent); border-color:color-mix(in srgb, var(--accent) 40%, var(--line)); }
.tag.warnc { color:var(--warn); border-color:color-mix(in srgb, var(--warn) 40%, var(--line)); }
.events { margin:0; padding:0; list-style:none; font-size:12.5px; white-space:normal; }
.events li { display:grid; grid-template-columns:70px 1fr; gap:10px; padding:2px 0; color:var(--muted); }
.events li.error span:last-child { color:var(--danger); } .events li.warn span:last-child { color:var(--warn); }
@media (prefers-reduced-motion: reduce) { * { transition:none !important; } }
</style>
</head>
<body>
<header>
  <div class="brand">
    <svg width="26" height="26" viewBox="0 0 26 26" aria-hidden="true"><rect x="1" y="1" width="24" height="24" rx="7" fill="none" stroke="currentColor" stroke-opacity=".35"/><path d="M7 8h12M13 8v5" stroke="currentColor" stroke-width="2" stroke-linecap="round"/><path d="M10.2 13h5.6L13 17.2z" fill="var(--accent)"/><path d="M7 20h12" stroke="var(--accent)" stroke-width="2" stroke-linecap="round"/></svg>
    3DP
  </div>
  <span id="mode" class="mode off"><i></i><span id="modeName">...</span><small id="modeHint"></small></span>
  <span id="simPill" class="pill" hidden>tempo
    <select id="simSpeed" class="num" aria-label="Velocit&agrave; della simulazione" style="border:0;background:transparent;padding:0 2px">
      <option value="1">&times;1</option><option value="4">&times;4</option><option value="12">&times;12</option><option value="30">&times;30</option>
    </select></span>
  <nav class="tabs" role="tablist">
    <button role="tab" data-tab="control" aria-selected="true">Controllo</button>
    <button role="tab" data-tab="tests" aria-selected="false">Test</button>
    <button role="tab" data-tab="config" aria-selected="false">Configurazione</button>
    <button role="tab" data-tab="history" aria-selected="false">Storico</button>
  </nav>
  <span class="grow"></span>
  <button class="estop" onclick="api('estop')" title="Ferma i motori e spegne l'ugello, subito">ARRESTO</button>
</header>

<main>
<!-- ============================================================ CONTROLLO -->
<div id="tab-control" class="split">
  <div class="col">
    <section class="viewer" aria-label="Stampante e percorso di stampa">
      <div class="stage">
        <canvas id="view"></canvas>
        <div class="hud">
          <div class="name" id="pvName">Nessun percorso caricato</div>
          <div class="facts" id="pvFacts"><span class="chip">scegli un modello o un G-code per vedere percorso e durata</span></div>
        </div>
        <div class="tools">
          <div class="seg" role="group" aria-label="Vista">
            <button onclick="setView('3d')" id="v3d" aria-pressed="true">3D</button>
            <button onclick="setView('top')" id="vtop" aria-pressed="false">Alto</button>
            <button onclick="setView('front')" id="vfront" aria-pressed="false">Fronte</button>
          </div>
          <div class="row" style="gap:6px">
            <button class="btn small ghost" id="machineBtn" aria-pressed="true" onclick="toggle('machine')">Stampante</button>
            <button class="btn small ghost" id="travelBtn" aria-pressed="false" onclick="toggle('travel')">Spostamenti</button>
            <button class="btn small ghost" id="followBtn" aria-pressed="false" onclick="toggle('follow')" title="La vista segue l'ugello">Segui</button>
          </div>
        </div>
        <div class="cut" id="cutBox" hidden><span>strato</span><input id="cut" type="range" min="0" max="0" value="0" aria-label="Mostra fino allo strato"><span class="num" id="cutLabel"></span></div>
        <div class="hint" id="hint">trascina: ruota &middot; rotella: zoom &middot; Maiusc o tasto destro: sposta &middot; doppio clic: reinquadra</div>
      </div>
      <div class="transport">
        <button class="play" id="play" onclick="togglePlay()" disabled aria-label="Avvia o ferma l'anteprima animata">
          <svg id="playIcon" width="14" height="14" viewBox="0 0 14 14"><path d="M3 1.5v11l9-5.5z" fill="currentColor"/></svg>
        </button>
        <div class="seg" id="speeds" role="group" aria-label="Velocit&agrave; dell'anteprima">
          <button data-speed="1" aria-pressed="false">&times;1</button>
          <button data-speed="20" aria-pressed="false">&times;20</button>
          <button data-speed="120" aria-pressed="true">&times;120</button>
          <button data-speed="600" aria-pressed="false">&times;600</button>
        </div>
        <input id="timeline" type="range" min="0" max="1000" value="0" disabled aria-label="Posizione nel tempo">
        <div class="clock"><div class="big" id="clock">--:--:--</div><div class="sub" id="clockSub">&nbsp;</div></div>
      </div>
      <div class="tele" aria-label="Valori letti dalla macchina">
        <div><span>Posizione reale</span><b id="tPos">-</b></div>
        <div><span>Velocit&agrave;</span><b id="tSpeed">-</b></div>
        <div title="Movimenti inviati e non ancora eseguiti: quelli in coda nella scheda piu' quelli in viaggio sulla seriale"><span>Inviati, da eseguire</span><b id="tQueue">-</b><div class="meter"><i id="tQueueBar"></i></div></div>
        <div><span>Movimenti inviati</span><b id="tRate">-</b></div>
        <div><span>Linea</span><b id="tBytes">-</b></div>
        <div><span>Ritrasmessi / errori</span><b id="tErr">-</b></div>
      </div>
    </section>

    <div class="card">
      <h2>Seriale: cosa viaggia verso la scheda e indietro
        <span class="row" style="gap:6px;text-transform:none;letter-spacing:0">
          <button class="btn small ghost" id="wMoves" aria-pressed="true" onclick="wireFilter('moves')">movimenti</button>
          <button class="btn small ghost" id="wText" aria-pressed="true" onclick="wireFilter('text')">testo</button>
          <button class="btn small ghost" id="wPause" aria-pressed="false" onclick="wireFilter('pause')">ferma</button>
        </span></h2>
      <div id="wire" aria-live="off"></div>
      <div class="locked">Per ogni scambio: il momento, il verso, che cosa fa (<span class="num">MOVE</span>, <span class="num">HOMING</span>, <span class="num">GET_TEMPERATURE</span>...), i byte e il contenuto. Ogni movimento parte come frame binario: al centro i byte, a destra la traduzione (step per asse, V velocit&agrave; in mm/s, A accelerazione, J velocit&agrave; d'ingresso). Le risposte <span class="num">ACK</span> dicono quanti movimenti restano da eseguire: &egrave; da l&igrave; che viene la posizione reale.</div>
    </div>
  </div>

  <aside class="col">
    <div class="card">
      <h2>Lavoro <span id="jobState" class="tag" hidden></span></h2>
      <div class="jobline"><span class="pct" id="pct">&ndash;</span><span class="what" id="jobName">nessun lavoro in corso</span></div>
      <div class="bar"><div id="progress"></div></div>
      <dl class="kv">
        <dt>Trascorso</dt><dd id="elapsed">-</dd>
        <dt>Restano circa</dt><dd id="remaining">-</dd>
        <dt>Righe</dt><dd id="lines">-</dd>
        <dt>Ultimo esito</dt><dd id="last" style="font-family:var(--sans)">-</dd>
      </dl>
      <div id="fault" class="note bad" hidden></div>
      <div class="row" style="margin-top:12px">
        <button class="btn" onclick="api('pause')">Pausa</button>
        <button class="btn" onclick="api('resume')">Riprendi</button>
        <button class="btn warn" id="cancelBtn" onclick="cancelJob()">Annulla</button>
        <span class="grow"></span>
        <button class="btn ghost small" onclick="api('clear')">Azzera errore</button>
      </div>
      <div style="margin-top:14px;display:grid;gap:12px">
        <label class="slider" title="Rallenta tutto il lavoro in proporzione: e' la leva giusta se la meccanica non regge."><span>Velocit&agrave; del lavoro</span><b id="spLabel">100%</b>
          <input id="sp" type="range" min="5" max="100" step="1" value="100"></label>
        <label class="slider" title="Tetto ai movimenti inviati alla scheda ogni secondo. 678 e' il massimo misurato."><span>Movimenti al secondo, al massimo</span><b id="bpsLabel">678</b>
          <input id="bps" type="range" min="10" max="678" step="1" value="678"></label>
      </div>
    </div>

    <div class="card">
      <h2>File</h2>
      <label class="drop" id="drop"><input id="file" type="file" accept=".gcode,.gco,.g,.stl,.3mf,.obj">
        <b>Scegli un file</b> o trascinalo qui<br><span style="font-size:12px">modello .stl / .3mf / .obj oppure G-code</span></label>
      <div class="row" style="margin-top:10px">
        <label class="f"><span>Strato mm</span><input id="layer" type="number" step="0.05" value="0.2"></label>
        <label class="f"><span>Riempimento %</span><input id="infill" type="number" value="20"></label>
        <label class="f"><span>Ugello &deg;C</span><input id="stemp" type="number" value="205"></label>
        <label class="f"><span>Supporti</span><select id="supports"><option value="0">no</option><option value="1">s&igrave;</option></select></label>
      </div>
      <div id="slicerNote" class="note" hidden>Slicer non trovato: i modelli non si possono affettare (i G-code s&igrave;). Installa PrusaSlicer oppure indica il suo percorso in Configurazione, Slicer.</div>
      <div id="report">I parametri valgono per i modelli: vengono affettati con il profilo della macchina. Un G-code viene solo controllato.</div>
      <div class="row" style="margin-top:10px">
        <select id="dry" aria-label="Tipo di esecuzione" title="A secco: per provare la meccanica senza hotend o senza consumare filamento">
          <option value="">Stampa vera</option>
          <option value="motion">A secco: solo assi X Y Z</option>
          <option value="heat">A secco: assi ed estrusore, senza scaldare</option>
        </select>
        <button class="btn primary" id="startBtn" style="flex:1" disabled onclick="startPrint()">Avvia</button></div>
      <details><summary>File gi&agrave; sul computer</summary><div class="row">
        <input id="path" type="text" placeholder="percorso del G-code" style="flex:1"><button class="btn" onclick="prepare(val('path'), val('path').split(/[\\/]/).pop(), '')">Apri</button></div></details>
    </div>

    <div class="card">
      <h2>Ugello</h2>
      <div class="temp">
        <div><div class="now"><span id="temp">--</span><small>&deg;C</small></div><div style="color:var(--muted);font-size:12.5px" id="tempTarget">obiettivo -</div></div>
        <canvas id="spark" width="300" height="92" aria-hidden="true"></canvas>
      </div>
      <div class="row" style="margin-top:12px">
        <input id="target" type="number" value="200" aria-label="Temperatura obiettivo">
        <button class="btn" onclick="api('temp','s='+val('target'))">Scalda</button>
        <button class="btn ghost" onclick="api('temp','s=0')">Spegni</button>
        <span class="grow"></span>
        <div class="seg" role="group" aria-label="Ventola"><button onclick="api('fan','s=100')">Ventola on</button><button onclick="api('fan','s=0')">off</button></div>
      </div>
    </div>

    <div class="card">
      <h2>Movimento manuale</h2>
      <div class="row" style="justify-content:space-between;margin-bottom:10px">
        <div class="seg" id="steps" role="group" aria-label="Passo in millimetri">
          <button data-step="0.1" aria-pressed="false">0.1</button><button data-step="1" aria-pressed="true">1</button>
          <button data-step="10" aria-pressed="false">10</button><button data-step="50" aria-pressed="false">50</button>
        </div>
        <label class="row" style="color:var(--muted);font-size:12px">mm/min <input id="feed" type="number" value="3000"></label>
      </div>
      <div class="pad">
        <span></span><button class="btn" data-idle onclick="jog('y',1)">Y+</button><span></span><span></span>
        <button class="btn" data-idle onclick="jog('z',1)">Z+</button><span></span><button class="btn" data-idle onclick="jog('e',1)">Estrudi</button>
        <button class="btn" data-idle onclick="jog('x',-1)">X&minus;</button><button class="btn home" data-idle onclick="api('home')" title="Azzera tutti gli assi">Home</button><button class="btn" data-idle onclick="jog('x',1)">X+</button><span></span>
        <button class="btn home" data-idle onclick="api('home','axes=Z')" title="Azzera solo Z">Z&#8962;</button><span></span><span></span>
        <span></span><button class="btn" data-idle onclick="jog('y',-1)">Y&minus;</button><span></span><span></span>
        <button class="btn" data-idle onclick="jog('z',-1)">Z&minus;</button><span></span><button class="btn" data-idle onclick="jog('e',-1)">Ritrai</button>
      </div>
      <div class="locked" id="jogLock" hidden>Durante un lavoro i movimenti manuali sono bloccati: restano pausa, annulla, arresto e le regolazioni di velocit&agrave;.</div>
      <details><summary>Comando G-code</summary><div>
        <div class="row"><input id="gcode" type="text" placeholder="es. M114" style="flex:1"><button class="btn" onclick="sendGcode()">Invia</button></div>
        <div id="log"></div>
      </div></details>
    </div>
  </aside>
</div>

<!-- ============================================================ TEST -->
<div id="tab-tests" hidden>
  <div id="testLock" class="note" hidden style="margin:0 0 14px">Un lavoro &egrave; in corso: i test si possono avviare solo a macchina ferma.</div>
  <div class="cards">
    <div class="card"><h2>Va e vieni</h2><p>Un asse avanti e indietro: ripetibilit&agrave;, rumori, giochi.</p>
      <div class="row"><label class="f"><span>Asse</span><select id="t1a"><option>x</option><option>y</option><option>z</option></select></label>
        <label class="f"><span>Corsa mm</span><input id="t1d" type="number" value="50"></label>
        <label class="f"><span>Velocit&agrave; mm/s</span><input id="t1f" type="number" value="50"></label>
        <label class="f"><span>Ripetizioni</span><input id="t1n" type="number" value="5"></label></div>
      <div class="row"><button class="btn primary" data-idle onclick="runTest('shuttle',{axis:val('t1a'),distance:val('t1d'),feed:val('t1f')*60,repeats:val('t1n')})">Avvia</button></div></div>

    <div class="card"><h2>Scala di velocit&agrave;</h2><p>La stessa corsa a velocit&agrave; crescenti: si trova dove la meccanica comincia a perdere passi.</p>
      <div class="row"><label class="f"><span>Asse</span><select id="t2a"><option>x</option><option>y</option><option>z</option></select></label>
        <label class="f"><span>Corsa mm</span><input id="t2d" type="number" value="60"></label>
        <label class="f"><span>Da mm/s</span><input id="t2f" type="number" value="20"></label>
        <label class="f"><span>A mm/s</span><input id="t2g" type="number" value="80"></label>
        <label class="f"><span>Gradini</span><input id="t2n" type="number" value="7"></label></div>
      <div class="row"><button class="btn primary" data-idle onclick="runTest('ladder',{axis:val('t2a'),distance:val('t2d'),feed:val('t2f')*60,feed2:val('t2g')*60,repeats:val('t2n')})">Avvia</button></div></div>

    <div class="card"><h2>Quadrato</h2><p>Angoli retti su X e Y: squadratura e comportamento negli angoli.</p>
      <div class="row"><label class="f"><span>Lato mm</span><input id="t3d" type="number" value="40"></label>
        <label class="f"><span>Velocit&agrave; mm/s</span><input id="t3f" type="number" value="50"></label>
        <label class="f"><span>Giri</span><input id="t3n" type="number" value="3"></label></div>
      <div class="row"><button class="btn primary" data-idle onclick="runTest('square',{distance:val('t3d'),feed:val('t3f')*60,repeats:val('t3n')})">Avvia</button></div></div>

    <div class="card"><h2>Cerchio</h2><p>Un arco vero (G2) spezzato dal driver in corde: fluidit&agrave; sulle curve.</p>
      <div class="row"><label class="f"><span>Diametro mm</span><input id="t4d" type="number" value="40"></label>
        <label class="f"><span>Velocit&agrave; mm/s</span><input id="t4f" type="number" value="50"></label>
        <label class="f"><span>Giri</span><input id="t4n" type="number" value="3"></label></div>
      <div class="row"><button class="btn primary" data-idle onclick="runTest('circle',{distance:val('t4d'),feed:val('t4f')*60,repeats:val('t4n')})">Avvia</button></div></div>

    <div class="card"><h2>Flusso di segmenti corti</h2><p>Migliaia di movimenti minuscoli: mette sotto sforzo collegamento e coda. Guarda movimenti al secondo, ritrasmessi ed errori.</p>
      <div class="row"><label class="f"><span>Asse</span><select id="t5a"><option>x</option><option>y</option></select></label>
        <label class="f"><span>Corsa mm</span><input id="t5d" type="number" value="50"></label>
        <label class="f"><span>Segmento mm</span><input id="t5s" type="number" step="0.01" value="0.1"></label>
        <label class="f"><span>Velocit&agrave; mm/s</span><input id="t5f" type="number" value="60"></label>
        <label class="f"><span>Giri</span><input id="t5n" type="number" value="2"></label></div>
      <div class="row"><button class="btn primary" data-idle onclick="runTest('stream',{axis:val('t5a'),distance:val('t5d'),segment:val('t5s'),feed:val('t5f')*60,repeats:val('t5n')})">Avvia</button></div></div>

    <div class="card"><h2>Estrusione</h2><p>Spinge una lunghezza nota di filamento, per tarare gli step dell'estrusore. L'ugello deve essere caldo.</p>
      <div class="row"><label class="f"><span>Lunghezza mm</span><input id="t6d" type="number" value="50"></label>
        <label class="f"><span>Velocit&agrave; mm/s</span><input id="t6f" type="number" value="3"></label></div>
      <div class="row"><button class="btn primary" data-idle onclick="runTest('extrude',{distance:val('t6d'),feed:val('t6f')*60})">Avvia</button></div></div>

    <div class="card"><h2>Finecorsa</h2><p>Stato degli interruttori letto adesso: premili a mano e rileggi.</p>
      <div class="row"><button class="btn" data-idle onclick="readOut('endstops','outEnd')">Leggi</button></div><pre class="out" id="outEnd" hidden></pre></div>

    <div class="card"><h2>Diagnostica del firmware</h2><p>RAM libera al picco, durata dell'interrupt degli step, scadenze mancate, errori di ricezione.</p>
      <div class="row"><button class="btn" data-idle onclick="readOut('diag','outDiag')">Leggi</button></div><pre class="out" id="outDiag" hidden></pre></div>
  </div>
  <div id="testMsg" class="note" hidden style="margin-top:14px"></div>
</div>

<!-- ============================================================ CONFIGURAZIONE -->
<div id="tab-config" hidden>
  <div class="cards" id="cfgGroups"></div>
  <div class="card" style="margin-top:14px"><h2>Fissati nel firmware (sola lettura)</h2>
    <p>Questi valori proteggono la macchina e stanno nella scheda: il driver li legge e non pu&ograve; superarli. Per cambiarli si ricompila il firmware.</p>
    <dl class="kv" id="cfgFirmware" style="max-width:640px"></dl></div>
  <div class="savebar"><button class="btn primary" id="cfgSave" onclick="saveConfig()" disabled>Salva</button><span id="cfgMsg" style="color:var(--muted)"></span></div>
</div>

<!-- ============================================================ STORICO -->
<div id="tab-history" hidden>
  <div class="card">
    <h2>Storico dei lavori</h2>
    <div class="wrap"><table><thead><tr><th>Inizio</th><th>Nome</th><th>Esito</th><th>Righe</th><th>Durata</th><th>Cosa &egrave; successo</th><th></th></tr></thead><tbody id="history"></tbody></table></div>
  </div>
</div>
</main>

<script>
'use strict';
const $ = id => document.getElementById(id);
const val = id => $(id).value;
const esc = t => String(t == null ? '' : t).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
const css = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
const it = n => Number(n).toLocaleString('it');
function clock(seconds) {
  if (!isFinite(seconds) || seconds < 0) return '--:--:--';
  const s = Math.round(seconds), p = n => String(n).padStart(2, '0');
  return p(Math.floor(s / 3600)) + ':' + p(Math.floor(s / 60) % 60) + ':' + p(s % 60);
}
function human(seconds) {
  if (!isFinite(seconds) || seconds <= 0) return '-';
  if (seconds < 90) return Math.round(seconds) + ' s';
  const m = Math.round(seconds / 60);
  return m < 60 ? m + ' min' : Math.floor(m / 60) + ' h ' + String(m % 60).padStart(2, '0') + ' min';
}
function log(text) { const el = $('log'); el.textContent = new Date().toLocaleTimeString() + '  ' + text + '\n' + el.textContent.slice(0, 4000); }

// ------------------------------------------------------------------ API e schede
let status = null, tab = 'control';
async function api(name, query, options) {
  try {
    const response = await fetch('/api/' + name + (query ? '?' + query : ''), Object.assign({method: 'POST'}, options || {}));
    const data = await response.json();
    log(name + ': ' + (data.ok ? 'ok' : 'ERRORE') + (data.message ? ' - ' + data.message : ''));
    refresh();
    return data;
  } catch (e) { log(name + ': servizio non raggiungibile'); }
}
document.querySelectorAll('.tabs button').forEach(b => b.addEventListener('click', () => show(b.dataset.tab)));
function show(name) {
  tab = name;
  document.querySelectorAll('.tabs button').forEach(b => b.setAttribute('aria-selected', b.dataset.tab === name));
  for (const t of ['control', 'tests', 'config', 'history']) $('tab-' + t).hidden = t !== name;
  if (name === 'config') loadConfig();
  if (name === 'history') loadHistory();
  if (name === 'control') resize();
}
let step = 1;
function jog(axis, sign) {
  const amount = axis === 'e' ? 5 * sign : step * sign;
  api('jog', axis + '=' + amount + '&f=' + (axis === 'z' || axis === 'e' ? 300 : val('feed')));
}
function sendGcode() { api('gcode', 'line=' + encodeURIComponent(val('gcode'))); }
let cancelArmed = 0;
function cancelJob() { // due tocchi invece di una finestra di conferma
  if (Date.now() < cancelArmed) { cancelArmed = 0; $('cancelBtn').textContent = 'Annulla'; api('cancel'); return; }
  cancelArmed = Date.now() + 3000;
  $('cancelBtn').textContent = 'Conferma';
  setTimeout(() => { if (Date.now() >= cancelArmed) $('cancelBtn').textContent = 'Annulla'; }, 3100);
}
function segmented(box, attribute, apply) {
  box.querySelectorAll('button[' + attribute + ']').forEach(button => button.addEventListener('click', () => {
    box.querySelectorAll('button[' + attribute + ']').forEach(b => b.setAttribute('aria-pressed', b === button));
    apply(Number(button.getAttribute(attribute)));
  }));
}
segmented($('steps'), 'data-step', v => step = v);

// ------------------------------------------------------------------ file -> controllo -> anteprima -> stampa
let ready = null; // {path, name, extra}
function describe(report) {
  if (!report) return '';
  let text = report.layers + ' strati, filamento ' + (report.filamentMm / 1000).toFixed(2) + ' m, ' + it(report.moves)
    + ' movimenti (' + it(report.arcs) + ' archi), altezza ' + report.bounds.maxZ.toFixed(1) + ' mm.';
  if (report.problems.length) text += ' Non stampabile: ' + report.problems.join('; ') + '.';
  if (report.warnings.length) text += ' Avvisi: ' + report.warnings.join('; ') + '.';
  return text;
}
function setReport(text, bad) { $('report').textContent = text; $('report').className = bad ? 'bad' : ''; }
async function prepare(path, name, extra, note) {
  if (!path) return;
  ready = null; $('startBtn').disabled = true;
  const checked = await api('check', 'file=' + encodeURIComponent(path));
  if (!checked) return;
  setReport((checked.report ? describe(checked.report) : (checked.message || '')) + (note ? ' Lo slicer avverte: ' + note : ''), !checked.ok);
  show('control');
  await loadPreview(path, name);
  if (checked.ok) { ready = {path, name, extra}; $('startBtn').disabled = busy(); }
}
async function handleFile(file) {
  if (!file) return;
  const isModel = /\.(stl|3mf|obj)$/i.test(file.name);
  setReport('Caricamento di ' + file.name + '...');
  const uploaded = await api('upload', 'name=' + encodeURIComponent(file.name), {body: await file.arrayBuffer()});
  if (!uploaded || !uploaded.ok) { setReport('Caricamento non riuscito.', true); return; }
  let path = uploaded.path, extra = '', warning = '';
  if (isModel) {
    setReport('Slicing in corso...');
    const sliced = await api('slice', 'file=' + encodeURIComponent(path) + '&layer=' + val('layer') + '&infill=' + val('infill')
      + '&temp=' + val('stemp') + '&supports=' + val('supports'));
    if (!sliced || !sliced.path) { setReport(sliced ? 'Slicing non riuscito: ' + sliced.message : 'Slicing non riuscito.', true); return; }
    extra = '&source=' + encodeURIComponent(sliced.source) + '&slicer=' + encodeURIComponent(sliced.slicer);
    path = sliced.path; warning = sliced.warning || '';
  }
  prepare(path, file.name, extra, warning);
}
$('file').addEventListener('change', e => handleFile(e.target.files[0]));
const drop = $('drop');
['dragenter', 'dragover'].forEach(n => drop.addEventListener(n, e => { e.preventDefault(); drop.classList.add('over'); }));
['dragleave', 'drop'].forEach(n => drop.addEventListener(n, e => { e.preventDefault(); drop.classList.remove('over'); }));
drop.addEventListener('drop', e => handleFile(e.dataTransfer.files[0]));
async function startPrint() {
  if (!ready) return;
  const started = await api('print', 'file=' + encodeURIComponent(ready.path) + '&name=' + encodeURIComponent(ready.name) + ready.extra
    + (val('dry') ? '&dry=' + val('dry') : ''));
  if (started && started.ok) { $('startBtn').disabled = true; playing = false; }
  else if (started) setReport(started.message, true);
}

// ------------------------------------------------------------------ test
async function runTest(kind, values) {
  const query = 'kind=' + kind + Object.entries(values).map(([k, v]) => '&' + k + '=' + encodeURIComponent(v)).join('');
  const result = await api('test', query);
  const box = $('testMsg');
  box.hidden = false;
  if (result && result.ok) { box.className = 'note good'; box.textContent = 'Test avviato: lo vedi nella scheda Controllo.'; show('control'); }
  else if (result && result.needsForce) {
    // senza homing i limiti di corsa non valgono: la scelta resta all'operatore, in modo esplicito
    box.className = 'note';
    box.innerHTML = 'La posizione non è riferita (manca l’homing): il driver non può controllare che ci sia corsa sufficiente. '
      + '<button class="btn small warn" id="forceTest" style="margin-left:8px">Avvia comunque</button>';
    $('forceTest').onclick = () => runTest(kind, Object.assign({}, values, {force: 1}));
  }
  else { box.className = 'note bad'; box.textContent = result ? result.message : 'Servizio non raggiungibile.'; }
}
async function readOut(name, target) {
  const result = await api(name);
  const box = $(target);
  box.hidden = false;
  box.textContent = result ? ((result.body || []).join('\n') || result.message) : 'Servizio non raggiungibile.';
}

// ------------------------------------------------------------------ configurazione
let cfgChanged = {};
function renderConfig(data) {
  if (!data || !data.groups) return;
  const locked = busy();
  $('cfgGroups').innerHTML = data.groups.map(g => '<div class="card"><h2>' + esc(g.name) + '</h2>' + g.items.map(i => {
    const off = locked && !i.whilePrinting ? ' disabled title="Si cambia solo a macchina ferma"' : '';
    const input = i.text
      ? '<input type="text" data-key="' + i.key + '" value="' + esc(i.value) + '"' + off + '>'
      : '<input type="number" data-key="' + i.key + '" min="' + i.min + '" max="' + i.max + '" step="' + i.step + '" value="' + i.value + '"' + off + '>';
    return '<div class="cfg" id="cfg-' + i.key + '"><span class="lab">' + esc(i.label) + '</span><span class="inp">' + input + esc(i.unit === 'C' ? '°C' : i.unit)
      + '</span>' + (i.help ? '<span class="help">' + esc(i.help) + '</span>' : '') + '</div>';
  }).join('') + '</div>').join('');
  const f = data.firmware;
  const row = (k, v) => '<dt>' + k + '</dt><dd>' + v + '</dd>';
  $('cfgFirmware').innerHTML = !f || !f.known ? row('Stato', 'non ancora letti: arrivano al primo movimento') :
    row('Step per mm (X Y Z E)', f.stepsPerMm.join(' · ')) + row('Velocità massima mm/s (X Y Z E)', f.maxFeed.map(v => +v.toFixed(1)).join(' · '))
    + row('Accelerazione massima mm/s² (X Y Z E)', f.maxAcceleration.join(' · ')) + row('Deviazione di giunzione', f.junctionDeviation + ' mm')
    + row('Step al secondo, massimo', it(f.maxStepRate)) + row('Movimenti in coda nella scheda', f.queueBlocks)
    + row('Buffer di ricezione', f.receiveBuffer + ' byte') + row('Corsa (X Y Z)', f.travel.map(v => +v.toFixed(1)).join(' · ') + ' mm');
  cfgChanged = {}; $('cfgSave').disabled = true;
}
async function loadConfig() {
  try { renderConfig(await (await fetch('/api/config')).json()); } catch (e) {}
}
$('cfgGroups').addEventListener('input', e => {
  const key = e.target.dataset.key; if (!key) return;
  cfgChanged[key] = e.target.value; $('cfg-' + key).classList.add('changed'); $('cfgSave').disabled = false; $('cfgMsg').textContent = '';
});
async function saveConfig() {
  const query = Object.entries(cfgChanged).map(([k, v]) => k + '=' + encodeURIComponent(v)).join('&');
  const data = await api('config', query);
  if (!data) return;
  $('cfgMsg').textContent = data.ok ? 'Salvato.' : data.message;
  $('cfgMsg').style.color = data.ok ? 'var(--ok)' : 'var(--danger)';
  if (data.ok) { renderConfig(data); reloadPreview(); }
}
// le due regolazioni che servono a portata di mano
function quick(id, label, key, suffix) {
  $(id).addEventListener('input', e => $(label).textContent = e.target.value + suffix);
  $(id).addEventListener('change', async e => { await api('config', key + '=' + e.target.value); reloadPreview(); });
}
quick('sp', 'spLabel', 'speed_percent', '%'); quick('bps', 'bpsLabel', 'blocks_per_second', '');

// ------------------------------------------------------------------ percorso: dati
let pv = null;          // percorso caricato
let cursor = 0;         // indice (anche frazionario) del punto raggiunto
let live = false;       // il cursore segue la macchina
let playing = false, speed = 120, lastFrame = 0;
const opt = {machine: true, travel: false, follow: false};
let cutLayer = 0;
async function loadPreview(path, name, keep) {
  let data;
  try { data = await (await fetch('/api/preview?file=' + encodeURIComponent(path))).json(); } catch (e) { return; }
  if (!data || !data.ok || !data.pts) return;
  const n = data.count, pts = data.pts;
  const p = {file: path, name: name || path.split(/[\\/]/).pop(), n, total: data.seconds, heatWaits: data.heatWaits,
    x: new Float32Array(n), y: new Float32Array(n), z: new Float32Array(n), t: new Float32Array(n), c: new Uint32Array(n), e: new Uint8Array(n)};
  let maxZ = 0, topZ = 0, tracks = 0;
  const layerOf = new Map(), ex = [], ey = [], ax = [], ay = [], every = Math.max(1, Math.floor(n / 4000));
  for (let i = 0, k = 0; i < n; ++i, k += 6) {
    p.x[i] = pts[k] / 100; p.y[i] = pts[k + 1] / 100; p.z[i] = pts[k + 2] / 100; p.t[i] = pts[k + 3] / 100; p.c[i] = pts[k + 4]; p.e[i] = pts[k + 5];
    topZ = Math.max(topZ, p.z[i]);
    if (p.e[i]) { ++tracks; layerOf.set(pts[k + 2], 0); maxZ = Math.max(maxZ, p.z[i]); }
    if (i % every === 0) { ax.push(p.x[i]); ay.push(p.y[i]); if (p.e[i]) { ex.push(p.x[i]); ey.push(p.y[i]); } }
  }
  // dal 2° al 98° percentile dei tratti stampati: pochi tratti lontani (spurgo) non rimpiccioliscono il pezzo
  const xs = ex.length > 8 ? ex : ax, ys = ex.length > 8 ? ey : ay;
  xs.sort((a, b) => a - b); ys.sort((a, b) => a - b);
  const lo = ex.length > 8 ? Math.floor(xs.length * 0.02) : 0, hi = ex.length > 8 ? Math.ceil(xs.length * 0.98) - 1 : xs.length - 1;
  const minX = xs[lo], maxX = xs[hi], minY = ys[lo], maxY = ys[hi];
  if (!tracks) maxZ = topZ;
  p.layers = [...layerOf.keys()].sort((a, b) => a - b).map(v => v / 100);
  p.tracks = tracks; p.maxZ = maxZ || 1;
  p.center = [(minX + maxX) / 2, (minY + maxY) / 2, maxZ / 2];
  p.radius = Math.max(18, Math.hypot(maxX - minX, maxY - minY, maxZ) / 2);
  const before = keep && pv ? timeAt(cursor) / pv.total : 0;
  pv = p; playing = false; playIcon(); cutLayer = p.layers.length;
  if (keep) seek(before * p.total); else { cursor = 0; live = false; fit(); }
  if (!tracks && !opt.travel) toggle('travel'); // un test non estrude: il suo percorso sono gli spostamenti
  $('cut').max = p.layers.length; $('cut').value = p.layers.length; $('cutBox').hidden = p.layers.length < 2; $('cutLabel').textContent = '';
  $('play').disabled = false; $('timeline').disabled = false;
  $('pvName').textContent = p.name;
  uploadPath(); hud();
}
function reloadPreview() { if (pv) loadPreview(pv.file, pv.name, true); }
function hud() {
  if (!pv) return;
  $('pvFacts').innerHTML = '<span class="chip">durata prevista <b>' + human(pv.total) + '</b>' + (pv.heatWaits ? ' + riscaldamento' : '') + '</span>'
    + (pv.layers.length ? '<span class="chip"><b>' + pv.layers.length + '</b> strati</span>' : '')
    + '<span class="chip"><b>' + it(pv.n - 1) + '</b> movimenti</span>'
    + (live ? '<span class="chip live">dal vivo: posizione eseguita dalla macchina</span>' : '');
}
function indexAtTime(t) { // primo punto con tempo >= t
  let lo = 0, hi = pv.n - 1;
  while (lo < hi) { const mid = (lo + hi) >> 1; if (pv.t[mid] < t) lo = mid + 1; else hi = mid; }
  return lo;
}
function indexAtCommand(count) { // ultimo punto prodotto dai primi `count` comandi
  let lo = 0, hi = pv.n - 1;
  while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (pv.c[mid] <= count) lo = mid; else hi = mid - 1; }
  return lo;
}
function timeAt(position) {
  const i = Math.min(pv.n - 1, Math.max(0, Math.floor(position))), f = position - i;
  return i + 1 < pv.n ? pv.t[i] + (pv.t[i + 1] - pv.t[i]) * f : pv.t[i];
}
function seek(t) {
  const i = Math.max(1, indexAtTime(t)), span = pv.t[i] - pv.t[i - 1];
  cursor = Math.min(pv.n - 1, i - 1 + (span > 0 ? Math.min(1, Math.max(0, (t - pv.t[i - 1]) / span)) : 1));
}
/// Il punto del percorso piu' vicino alla posizione eseguita, cercato poco prima di cio' che e' gia' stato inviato
function locate(x, y, z, upTo) {
  let best = upTo, bestD = 1e18;
  for (let i = Math.max(1, upTo - 900); i <= upTo; ++i) {
    const ax = pv.x[i - 1], ay = pv.y[i - 1], az = pv.z[i - 1], dx = pv.x[i] - ax, dy = pv.y[i] - ay, dz = pv.z[i] - az;
    const len = dx * dx + dy * dy + dz * dz;
    const f = len > 0 ? Math.min(1, Math.max(0, ((x - ax) * dx + (y - ay) * dy + (z - az) * dz) / len)) : 1;
    const d = (ax + dx * f - x) ** 2 + (ay + dy * f - y) ** 2 + (az + dz * f - z) ** 2;
    if (d <= bestD) { bestD = d; best = i - 1 + f; }
  }
  return best;
}

// ------------------------------------------------------------------ visualizzatore WebGL
const canvas = $('view');
const gl = canvas.getContext('webgl2', {antialias: true, alpha: true, premultipliedAlpha: false});
const volume = {x: 220, y: 220, z: 200};
const cam = {yaw: -2.05, el: 0.5, dist: 620, tx: 110, ty: 110, tz: 45};
const head = {x: 0, y: 0, z: 0, tx: 0, ty: 0, tz: 0, known: false};
let hotness = 0, W = 0, H = 0, dpr = 1;

const M = { // matrici 4x4 per colonne
  mul(a, b) { const o = new Float32Array(16);
    for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    return o; },
  perspective(fovy, aspect, near, far) { const f = 1 / Math.tan(fovy / 2), o = new Float32Array(16);
    o[0] = f / aspect; o[5] = f; o[10] = (far + near) / (near - far); o[11] = -1; o[14] = 2 * far * near / (near - far); return o; },
  lookAt(e, t) { // Z in alto
    let fx = t[0] - e[0], fy = t[1] - e[1], fz = t[2] - e[2]; const fl = Math.hypot(fx, fy, fz); fx /= fl; fy /= fl; fz /= fl;
    let sx = fy, sy = -fx, sl = Math.hypot(sx, sy); if (sl < 1e-5) { sx = 1; sy = 0; sl = 1; } sx /= sl; sy /= sl;
    const ux = sy * fz, uy = -sx * fz, uz = sx * fy - sy * fx;
    return {m: new Float32Array([sx, ux, -fx, 0, sy, uy, -fy, 0, 0, uz, -fz, 0,
      -(sx * e[0] + sy * e[1]), -(ux * e[0] + uy * e[1] + uz * e[2]), fx * e[0] + fy * e[1] + fz * e[2], 1]), right: [sx, sy, 0], up: [ux, uy, uz]}; }
};
function program(vs, fs) {
  const make = (type, source) => { const s = gl.createShader(type); gl.shaderSource(s, source); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) console.error(gl.getShaderInfoLog(s)); return s; };
  const p = gl.createProgram(); gl.attachShader(p, make(gl.VERTEX_SHADER, vs)); gl.attachShader(p, make(gl.FRAGMENT_SHADER, fs)); gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) console.error(gl.getProgramInfoLog(p));
  const u = {}; for (let i = 0, n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS); i < n; ++i) { const name = gl.getActiveUniform(p, i).name; u[name] = gl.getUniformLocation(p, name); }
  return {p, u};
}
let meshProg, pathProg, groups = {}, pathVao = null, pathCount = 0, pathBuffer = null;
function initGl() {
  meshProg = program(`#version 300 es
    layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNor; layout(location=2) in vec4 aCol;
    uniform mat4 uVP; uniform vec3 uOff; out vec3 vNor; out vec4 vCol;
    void main() { vNor = aNor; vCol = aCol; gl_Position = uVP * vec4(aPos + uOff, 1.0); }`,
    `#version 300 es
    precision mediump float; in vec3 vNor; in vec4 vCol; uniform float uGlow, uUnlit; uniform vec3 uGlowColor; out vec4 o;
    void main() { vec3 n = normalize(vNor); float d = max(dot(n, normalize(vec3(-0.35, -0.55, 0.76))), 0.0);
      float back = max(dot(n, normalize(vec3(0.5, 0.6, 0.2))), 0.0);
      vec3 c = mix(vCol.rgb * (0.34 + 0.62 * d + 0.14 * back), vCol.rgb, uUnlit);
      float heat = clamp(vCol.a * uGlow, 0.0, 1.0);
      c = mix(c, uGlowColor * (0.55 + 0.6 * d + 0.2 * back), heat * 0.85) + uGlowColor * heat * 0.12; o = vec4(c, 1.0); }`);
  // Un nastro largo quanto la traccia per ogni tratto: quattro vertici, disegnati in istanze
  pathProg = program(`#version 300 es
    layout(location=0) in vec2 aCorner; layout(location=1) in vec3 aA; layout(location=2) in vec3 aB; layout(location=3) in vec2 aInfo;
    uniform mat4 uVP; uniform vec2 uView; uniform float uWidth, uMinPx;
    out float vProg; out float vSide; out float vZ; out float vKind;
    void main() { vec4 a = uVP * vec4(aA, 1.0), b = uVP * vec4(aB, 1.0);
      vec2 sa = a.xy / max(a.w, 1e-4) * uView, sb = b.xy / max(b.w, 1e-4) * uView, d = sb - sa;
      float len = length(d); vec2 along = len > 1e-4 ? d / len : vec2(1.0, 0.0); vec2 n = vec2(-along.y, along.x);
      vec4 c = mix(a, b, aCorner.x);
      float px = max(uMinPx, uWidth / max(c.w, 1e-4));
      // un po' piu' lungo del tratto: i tratti si saldano negli angoli
      vec2 off = (n * aCorner.y + along * (aCorner.x * 2.0 - 1.0) * 0.5) * px * 0.5;
      c.xy += off / uView * c.w;
      gl_Position = c; vProg = aInfo.x - 1.0 + aCorner.x; vSide = aCorner.y; vZ = mix(aA.z, aB.z, aCorner.x); vKind = aInfo.y; }`,
    `#version 300 es
    precision mediump float; in float vProg; in float vSide; in float vZ; in float vKind;
    uniform float uCursor, uCutZ, uMaxZ, uTravel; uniform int uPass; uniform vec3 uLo, uMid, uHi, uCool; uniform vec4 uGhost; out vec4 o;
    void main() { if (vZ > uCutZ) discard;
      bool done = vProg <= uCursor;
      if (vKind < 0.5) { if (uPass != 1 || uTravel < 0.5) discard; o = vec4(uCool, done ? 0.6 : 0.2); return; }
      if (uPass == 0) { if (!done) discard; float h = clamp(vZ / uMaxZ, 0.0, 1.0);
        vec3 c = h < 0.5 ? mix(uLo, uMid, h * 2.0) : mix(uMid, uHi, h * 2.0 - 1.0);
        o = vec4(c * (1.0 - 0.5 * vSide * vSide), 1.0); }   // piu' scura ai bordi: sembra un cordone tondo
      else { if (done) discard; o = uGhost; } }`);
  buildMachine();
  // quadrato di base dei nastri
  pathVao = gl.createVertexArray(); gl.bindVertexArray(pathVao);
  const quad = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, quad);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, -1, 0, 1, 1, -1, 1, 1]), gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
  pathBuffer = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, pathBuffer);
  for (const [loc, size, offset] of [[1, 3, 0], [2, 3, 12], [3, 2, 24]]) {
    gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 32, offset); gl.vertexAttribDivisor(loc, 1);
  }
  gl.bindVertexArray(null);
}
function uploadPath() {
  if (!gl || !pv) return;
  const n = pv.n - 1, data = new Float32Array(n * 8);
  for (let i = 1, k = 0; i < pv.n; ++i, k += 8) {
    data[k] = pv.x[i - 1]; data[k + 1] = pv.y[i - 1]; data[k + 2] = pv.z[i - 1];
    data[k + 3] = pv.x[i]; data[k + 4] = pv.y[i]; data[k + 5] = pv.z[i]; data[k + 6] = i; data[k + 7] = pv.e[i];
  }
  gl.bindBuffer(gl.ARRAY_BUFFER, pathBuffer); gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  pathCount = n;
}
/// Costruttore di solidi: scatole e cilindri con normali, colore e "quanto si arroventa" (quarto valore del colore)
function Mesh() {
  const v = [];
  const put = (p, n, col) => v.push(p[0], p[1], p[2], n[0], n[1], n[2], col[0], col[1], col[2], col[3] || 0);
  const tri = (a, b, c, n, col) => { put(a, n, col); put(b, n, col); put(c, n, col); };
  const quad = (a, b, c, d, n, col) => { tri(a, b, c, n, col); tri(a, c, d, n, col); };
  return {v,
    box(x0, y0, z0, x1, y1, z1, col) {
      quad([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1], col); quad([x0, y1, z0], [x1, y1, z0], [x1, y0, z0], [x0, y0, z0], [0, 0, -1], col);
      quad([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [0, -1, 0], col); quad([x1, y1, z0], [x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [0, 1, 0], col);
      quad([x0, y1, z0], [x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [-1, 0, 0], col); quad([x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1], [1, 0, 0], col); },
    cyl(cx, cy, z0, z1, r0, r1, col, sides) {
      const s = sides || 28, slope = (r0 - r1) / (z1 - z0);
      for (let i = 0; i < s; ++i) {
        const a0 = i / s * 6.2831853, a1 = (i + 1) / s * 6.2831853, c0 = Math.cos(a0), s0 = Math.sin(a0), c1 = Math.cos(a1), s1 = Math.sin(a1);
        const p0 = [cx + r0 * c0, cy + r0 * s0, z0], p1 = [cx + r0 * c1, cy + r0 * s1, z0], p2 = [cx + r1 * c1, cy + r1 * s1, z1], p3 = [cx + r1 * c0, cy + r1 * s0, z1];
        const n0 = [c0, s0, slope], n1 = [c1, s1, slope];
        put(p0, n0, col); put(p1, n1, col); put(p2, n1, col); put(p0, n0, col); put(p2, n1, col); put(p3, n0, col);
        tri([cx, cy, z1], p3, p2, [0, 0, 1], col); tri([cx, cy, z0], p1, p0, [0, 0, -1], col);
      } },
    lines(list, col) { for (const p of list) put(p, [0, 0, 1], col); }
  };
}
function upload(mesh, mode) {
  const vao = gl.createVertexArray(); gl.bindVertexArray(vao);
  const buffer = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(mesh.v), gl.STATIC_DRAW);
  for (const [loc, size, offset] of [[0, 3, 0], [1, 3, 12], [2, 4, 24]]) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 40, offset); }
  gl.bindVertexArray(null);
  return {vao, count: mesh.v.length / 10, mode: mode || gl.TRIANGLES};
}
/// Una macchina a ponte mobile: piano fermo, il ponte porta la testina in X e Y e sale in Z.
/// E' una rappresentazione: le quote dell'hotend sono verosimili, non rilevate dalla macchina.
function buildMachine() {
  const X = volume.x, Y = volume.y, Zt = volume.z + 95, steel = [0.56, 0.59, 0.64], dark = [0.17, 0.185, 0.215], alu = [0.74, 0.76, 0.8];
  const light = matchMedia('(prefers-color-scheme: light)').matches;
  const bedTop = light ? [0.8, 0.82, 0.85] : [0.115, 0.125, 0.15], rail = light ? [0.5, 0.53, 0.58] : [0.3, 0.325, 0.37];
  let m = Mesh();
  m.box(-6, -6, -5, X + 6, Y + 6, -0.05, bedTop);                // piano
  m.box(-34, -34, -22, X + 34, Y + 34, -8, dark);                // basamento
  for (const x of [-34, X + 22]) for (const y of [-34, Y + 22]) m.box(x, y, -22, x + 12, y + 12, Zt, rail); // montanti
  m.box(-34, Y + 22, Zt - 12, X + 34, Y + 34, Zt, rail); m.box(-34, -34, Zt - 12, -22, Y + 34, Zt, rail); m.box(X + 22, -34, Zt - 12, X + 34, Y + 34, Zt, rail);
  groups.frame = upload(m);
  m = Mesh(); const grid = [], g = light ? [0.62, 0.65, 0.7] : [0.23, 0.25, 0.29];
  for (let p = 0; p <= X + 0.01; p += 20) grid.push([p, 0, 0], [p, Y, 0]);
  for (let p = 0; p <= Y + 0.01; p += 20) grid.push([0, p, 0], [X, p, 0]);
  m.lines(grid, g);
  m.lines([[0, 0, 0.05], [24, 0, 0.05]], [1, 0.48, 0.1]); m.lines([[0, 0, 0.05], [0, 24, 0.05]], [0.35, 0.65, 1]);
  groups.grid = upload(m, gl.LINES);
  m = Mesh(); m.box(-34, -34, 66, -22, Y + 34, 78, steel); m.box(X + 22, -34, 66, X + 34, Y + 34, 78, steel); // guide Y: salgono con Z
  groups.rails = upload(m);
  m = Mesh(); m.box(-30, 20, 62, X + 30, 32, 80, alu); m.box(-30, 20, 80, X + 30, 23, 84, steel);              // trave X: segue Y e Z
  groups.beam = upload(m);
  m = Mesh(); const brass = [0.84, 0.63, 0.24, 0.45], block = [0.7, 0.72, 0.76, 1], fin = [0.8, 0.82, 0.86];
  m.cyl(0, 0, 0, 2.2, 0.45, 2.4, brass, 20);                      // punta dell'ugello
  m.cyl(0, 0, 2.2, 5.2, 3.6, 3.6, brass, 6);                      // esagono dell'ugello
  m.box(-10, -6.5, 5.2, 8, 6.5, 16.5, block);                     // blocco riscaldante
  m.cyl(0, 0, 16.5, 21, 1.6, 1.6, steel, 14);                     // gola
  m.cyl(0, 0, 21, 47, 4.4, 4.4, fin, 24);                         // dissipatore: anima e alette
  for (let z = 21; z < 46; z += 2.9) m.cyl(0, 0, z, z + 1.1, 11, 11, fin, 32);
  m.cyl(0, 0, 47, 53, 8, 8, fin, 24);
  m.box(-15, 12, 24, 15, 20, 80, dark);                           // carrello sulla trave
  m.box(-15, 11.4, 62, 15, 12, 65, [1, 0.48, 0.1]);
  m.box(-11, -9, 53, 11, 12, 72, dark);                           // estrusore
  m.box(11.5, -11, 23, 16, 11, 45, [0.1, 0.11, 0.13]);            // ventola del dissipatore, di lato
  m.cyl(0, 0, 74, 150, 0.9, 0.9, [1, 0.48, 0.1], 10);             // filamento
  groups.head = upload(m);
}
function resize() {
  const box = canvas.getBoundingClientRect();
  dpr = Math.min(2, window.devicePixelRatio || 1);
  W = Math.max(50, Math.round(box.width * dpr)); H = Math.max(50, Math.round(box.height * dpr));
  if (canvas.width !== W || canvas.height !== H) { canvas.width = W; canvas.height = H; }
}
function rgb(name) { const v = parseInt(css(name).slice(1), 16); return [(v >> 16) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255]; }
function fit() {
  if (pv) { cam.tx = pv.center[0]; cam.ty = pv.center[1]; cam.tz = pv.center[2] + 14; cam.dist = Math.max(200, pv.radius * 6.4); }
  else { cam.tx = volume.x / 2; cam.ty = volume.y / 2; cam.tz = 95; cam.dist = Math.max(volume.x, volume.y) * 3.7; }
}
function cutZ() { return pv && cutLayer < pv.layers.length ? (cutLayer ? pv.layers[cutLayer - 1] : -1) + 0.001 : 1e9; }
function draw(now) {
  requestAnimationFrame(draw);
  const dt = Math.min(0.25, (now - lastFrame) / 1000 || 0); lastFrame = now;
  if (tab !== 'control') return;
  // --- dove sta la testina ---
  if (pv && playing && !live) {
    const t = timeAt(cursor) + dt * speed;
    if (t >= pv.total) { cursor = pv.n - 1; playing = false; playIcon(); } else seek(t);
  }
  if (pv && !live && (playing || cursor > 0)) {
    const i = Math.min(pv.n - 1, Math.floor(cursor)), j = Math.min(pv.n - 1, i + 1), f = cursor - i;
    head.x = pv.x[i] + (pv.x[j] - pv.x[i]) * f; head.y = pv.y[i] + (pv.y[j] - pv.y[i]) * f; head.z = pv.z[i] + (pv.z[j] - pv.z[i]) * f;
  } else {
    // dal vivo la posizione arriva qualche volta al secondo: la si insegue per avere un moto continuo
    const k = 1 - Math.exp(-dt / 0.14);
    head.x += (head.tx - head.x) * k; head.y += (head.ty - head.y) * k; head.z += (head.tz - head.z) * k;
    if (live && pv) cursor += (cursorTarget - cursor) * (1 - Math.exp(-dt / 0.2));
  }
  if (opt.follow) { const k = 1 - Math.exp(-dt / 0.5); cam.tx += (head.x - cam.tx) * k; cam.ty += (head.y - cam.ty) * k; cam.tz += (head.z + 10 - cam.tz) * k; }

  // --- camera ---
  const ce = Math.cos(cam.el), eye = [cam.tx + cam.dist * ce * Math.cos(cam.yaw), cam.ty + cam.dist * ce * Math.sin(cam.yaw), cam.tz + cam.dist * Math.sin(cam.el)];
  const view = M.lookAt(eye, [cam.tx, cam.ty, cam.tz]); cam.right = view.right; cam.up = view.up;
  const proj = M.perspective(0.5, W / H, Math.max(2, cam.dist * 0.02), cam.dist * 6 + 2000), vp = M.mul(proj, view.m);
  gl.viewport(0, 0, W, H); gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.disable(gl.BLEND); gl.depthMask(true);

  // --- solidi ---
  gl.useProgram(meshProg.p); gl.uniformMatrix4fv(meshProg.u.uVP, false, vp); gl.uniform3fv(meshProg.u.uGlowColor, [1.0, 0.33, 0.08]);
  const solid = (group, x, y, z, glow, unlit) => { gl.uniform3f(meshProg.u.uOff, x, y, z); gl.uniform1f(meshProg.u.uGlow, glow || 0); gl.uniform1f(meshProg.u.uUnlit, unlit || 0);
    gl.bindVertexArray(group.vao); gl.drawArrays(group.mode, 0, group.count); };
  if (opt.machine) { solid(groups.frame, 0, 0, 0); solid(groups.rails, 0, 0, head.z); solid(groups.beam, 0, head.y, head.z); }
  solid(groups.grid, 0, 0, 0, 0, 1);
  solid(groups.head, head.x, head.y, head.z, hotness);

  // --- percorso ---
  if (pv && pathCount) {
    gl.useProgram(pathProg.p); gl.uniformMatrix4fv(pathProg.u.uVP, false, vp);
    gl.uniform2f(pathProg.u.uView, W / 2, H / 2); gl.uniform1f(pathProg.u.uWidth, 0.46 * proj[5] * H / 2);
    gl.uniform1f(pathProg.u.uCursor, cursor); gl.uniform1f(pathProg.u.uCutZ, cutZ()); gl.uniform1f(pathProg.u.uMaxZ, pv.maxZ);
    gl.uniform1f(pathProg.u.uTravel, opt.travel ? 1 : 0);
    gl.uniform3fv(pathProg.u.uLo, rgb('--path-lo')); gl.uniform3fv(pathProg.u.uMid, rgb('--path-mid')); gl.uniform3fv(pathProg.u.uHi, rgb('--path-hi')); gl.uniform3fv(pathProg.u.uCool, rgb('--cool'));
    const ink = rgb('--ink'); gl.uniform4f(pathProg.u.uGhost, ink[0], ink[1], ink[2], 0.045);
    gl.bindVertexArray(pathVao);
    gl.uniform1i(pathProg.u.uPass, 0); gl.uniform1f(pathProg.u.uMinPx, 1.3 * dpr); gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, pathCount);
    gl.enable(gl.BLEND); gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false);
    gl.uniform1i(pathProg.u.uPass, 1); gl.uniform1f(pathProg.u.uMinPx, 1.0 * dpr); gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, pathCount);
    gl.depthMask(true); gl.disable(gl.BLEND);
  }
  gl.bindVertexArray(null);
  transport();
}
function transport() {
  if (!pv) return;
  const t = timeAt(Math.max(0, cursor)), i = Math.min(pv.n - 1, Math.max(0, Math.floor(cursor)));
  if (document.activeElement !== $('timeline') || live) $('timeline').value = pv.total > 0 ? Math.round(t / pv.total * 1000) : 0;
  $('clock').textContent = clock(t) + ' / ' + clock(pv.total);
  let layer = 0; while (layer < pv.layers.length && pv.layers[layer] <= pv.z[i] + 0.001) ++layer;
  $('clockSub').textContent = (pv.layers.length ? 'strato ' + layer + ' di ' + pv.layers.length + ' · ' : '') + 'Z ' + head.z.toFixed(2);
}
function playIcon() {
  $('playIcon').innerHTML = playing ? '<path d="M3 1.5h3v11H3zM8 1.5h3v11H8z" fill="currentColor"/>' : '<path d="M3 1.5v11l9-5.5z" fill="currentColor"/>';
}
function togglePlay() {
  if (!pv || live) return;
  if (!playing && cursor >= pv.n - 1) cursor = 0;
  playing = !playing; playIcon();
}
segmented($('speeds'), 'data-speed', v => speed = v);
$('timeline').addEventListener('input', e => { if (!pv || live) return; seek(e.target.value / 1000 * pv.total); });
$('cut').addEventListener('input', e => { cutLayer = Number(e.target.value); $('cutLabel').textContent = cutLayer < pv.layers.length ? cutLayer : ''; });
function toggle(name) { opt[name] = !opt[name]; $(name + 'Btn').setAttribute('aria-pressed', opt[name]); }
function setView(name) {
  if (name === 'top') { cam.yaw = -Math.PI / 2; cam.el = 1.555; } else if (name === 'front') { cam.yaw = -Math.PI / 2; cam.el = 0.06; } else { cam.yaw = -2.05; cam.el = 0.5; }
  for (const v of ['3d', 'top', 'front']) $('v' + v).setAttribute('aria-pressed', v === name);
  fit();
}
let pointer = null;
canvas.addEventListener('pointerdown', e => { pointer = {x: e.clientX, y: e.clientY, pan: e.button !== 0 || e.shiftKey}; canvas.setPointerCapture(e.pointerId); canvas.classList.add('drag'); $('hint').hidden = true; });
canvas.addEventListener('pointermove', e => {
  if (!pointer) return;
  const dx = e.clientX - pointer.x, dy = e.clientY - pointer.y; pointer.x = e.clientX; pointer.y = e.clientY;
  if (pointer.pan && cam.right) { const k = cam.dist * 0.0011;
    cam.tx += (-cam.right[0] * dx + cam.up[0] * dy) * k; cam.ty += (-cam.right[1] * dx + cam.up[1] * dy) * k; cam.tz += (-cam.right[2] * dx + cam.up[2] * dy) * k; }
  else { cam.yaw -= dx * 0.007; cam.el = Math.min(1.555, Math.max(0.03, cam.el + dy * 0.006)); }
});
const release = () => { pointer = null; canvas.classList.remove('drag'); };
canvas.addEventListener('pointerup', release); canvas.addEventListener('pointercancel', release);
canvas.addEventListener('contextmenu', e => e.preventDefault());
canvas.addEventListener('wheel', e => { e.preventDefault(); cam.dist = Math.min(4000, Math.max(25, cam.dist * Math.exp(e.deltaY * 0.0012))); }, {passive: false});
canvas.addEventListener('dblclick', fit);
new ResizeObserver(resize).observe(canvas);
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { if (gl) buildMachine(); });

// ------------------------------------------------------------------ temperatura: ultimi minuti
const samples = [];
function spark(target) {
  const c = $('spark'), g = c.getContext('2d'), w = c.width, h = c.height;
  g.clearRect(0, 0, w, h);
  if (samples.length < 2) return;
  const top = Math.max(60, target + 15, ...samples), y = v => h - 4 - (v / top) * (h - 8), x = i => i / (samples.length - 1) * w;
  if (target > 0) { g.strokeStyle = css('--muted'); g.setLineDash([4, 5]); g.lineWidth = 1.5; g.beginPath(); g.moveTo(0, y(target)); g.lineTo(w, y(target)); g.stroke(); g.setLineDash([]); }
  g.beginPath(); samples.forEach((v, i) => i ? g.lineTo(x(i), y(v)) : g.moveTo(x(i), y(v)));
  g.strokeStyle = css('--accent'); g.lineWidth = 2.5; g.lineJoin = 'round'; g.stroke();
  g.lineTo(w, h); g.lineTo(0, h); g.closePath(); g.globalAlpha = 0.12; g.fillStyle = css('--accent'); g.fill(); g.globalAlpha = 1;
}

// ------------------------------------------------------------------ dal vivo: posizione eseguita, collegamento, seriale
let wireLast = 0, wireRows = [], wirePaused = false, wireMoves = true, wireText = true, cursorTarget = 0, lastLink = null, lastLinkAt = 0, rate = 0, byteRate = 0;
function wireFilter(which) {
  if (which === 'moves') wireMoves = !wireMoves; else if (which === 'text') wireText = !wireText; else wirePaused = !wirePaused;
  $('wMoves').setAttribute('aria-pressed', wireMoves); $('wText').setAttribute('aria-pressed', wireText); $('wPause').setAttribute('aria-pressed', wirePaused);
  renderWire();
}
const KINDS = {move: 'mov', ack: 'ack', text: 'testo', resend: 'RITR', raw: 'dati'};
function renderWire() {
  const box = $('wire'), stick = box.scrollTop + box.clientHeight >= box.scrollHeight - 30;
  box.innerHTML = wireRows.filter(r => r.gap || ((r.k === 'move' || r.k === 'ack' || r.k === 'resend') ? wireMoves : wireText)).map(r => r.gap
    ? '<span class="gap">… ' + it(r.gap) + ' scambi non mostrati (troppi per leggerli)</span>'
    : '<div class="' + (r.k === 'resend' || /ERR/.test(r.text) ? 'err' : '') + '"><span class="t">' + r.t.toFixed(3) + '</span><span class="' + (r.d === 'T' ? 'tx">→' : 'rx">←') + '</span><span class="k">'
      + esc(r.n || KINDS[r.k] || r.k) + '</span><span class="hex">' + r.hex + '</span><span class="txt">' + esc(r.text) + '</span></div>').join('');
  if (stick) box.scrollTop = box.scrollHeight;
}
async function pollLive() {
  let d;
  try { d = await (await fetch('/api/live?after=' + wireLast)).json(); } catch (e) { return; }
  if (!d || !d.ok) return;
  // --- posizione eseguita ---
  const active = status && status.job.id;
  if (d.exec.known && (d.moving || active)) { head.tx = d.exec.x; head.ty = d.exec.y; head.tz = d.exec.z; head.known = true; }
  else if (status && status.position.known) { head.tx = status.position.x; head.ty = status.position.y; head.tz = status.position.z; head.known = true; }
  if (live && pv && status && head.known) cursorTarget = locate(head.tx, head.ty, head.tz, indexAtCommand(status.job.linesDone));
  $('tPos').textContent = head.known ? 'X' + head.tx.toFixed(1) + ' Y' + head.ty.toFixed(1) + ' Z' + head.tz.toFixed(2) : '-';
  $('tSpeed').innerHTML = d.moving ? d.speed.toFixed(1) + ' <small>mm/s</small>' : 'ferma';
  $('tQueue').innerHTML = d.queued + ' <small>/ ' + d.queueSize + '</small>';
  $('tQueueBar').style.width = Math.min(100, d.queued / d.queueSize * 100) + '%';
  // --- collegamento: quanto passa in un secondo ---
  const now = performance.now();
  if (!lastLink) { lastLink = d.link; lastLinkAt = now; }
  else if (now - lastLinkAt > 900) {
    const s = (now - lastLinkAt) / 1000;
    rate = (d.link.framesTx - lastLink.framesTx) / s; byteRate = (d.link.bytesTx + d.link.bytesRx - lastLink.bytesTx - lastLink.bytesRx) / s;
    lastLink = d.link; lastLinkAt = now;
  }
  $('tRate').innerHTML = Math.round(rate) + ' <small>/s · ' + it(d.link.framesTx) + ' in tutto</small>';
  $('tBytes').innerHTML = it(Math.round(byteRate)) + ' <small>byte/s</small>';
  $('tErr').innerHTML = d.link.resent + ' <small>/</small> ' + d.link.errors;
  $('tErr').style.color = d.link.resent + d.link.errors > 0 ? 'var(--warn)' : '';
  // --- seriale ---
  wireLast = d.last;
  if (!wirePaused && (d.wire.length || d.skipped)) {
    if (d.skipped) wireRows.push({gap: d.skipped});
    wireRows.push(...d.wire);
    if (wireRows.length > 260) wireRows = wireRows.slice(-260);
    if (tab === 'control') renderWire();
  }
}

// ------------------------------------------------------------------ stato
const MODES = {sim: ['Simulazione', 'macchina simulata: prova quello che vuoi'], idle: ['Pronta', 'macchina vera, ferma: comandi liberi e test'],
  print: ['In stampa', 'macchina vera al lavoro: si osserva; restano pausa, annulla e velocità']};
const JOBS = {RUNNING: 'in corso', PAUSED: 'in pausa', HEATING: 'riscaldamento', HOMING: 'homing', LOADING: 'caricamento', PRECHECK: 'controlli', QUEUED: 'in coda'};
function busy() { return !!(status && status.job.id); }
function span(a, b) { return a ? human((b || Date.now() / 1000) - a) : '-'; }
let firstStatus = true;
async function refresh() {
  let s;
  try { s = await (await fetch('/api/status')).json(); }
  catch (e) { $('mode').className = 'mode off'; $('modeName').textContent = 'Servizio fermo'; $('modeHint').textContent = ''; return; }
  status = s;
  if (s.volume && (s.volume.x !== volume.x || s.volume.y !== volume.y || s.volume.z !== volume.z)) { Object.assign(volume, s.volume); if (gl) buildMachine(); if (!pv) fit(); }
  const job = s.job, active = !!job.id;
  const mode = !s.connected ? 'off' : s.mode;
  $('mode').className = 'mode ' + mode;
  $('modeName').textContent = mode === 'off' ? 'Scollegata' : MODES[mode][0] + (s.mode === 'sim' && active ? ' · lavoro in corso' : '');
  $('modeHint').textContent = mode === 'off' ? 'la scheda non risponde' : (s.state === 'Error' ? 'macchina in errore: premi Azzera errore' : MODES[mode][1]);
  $('simPill').hidden = !s.sim;
  $('slicerNote').hidden = !s.slicer || s.slicer.found;
  if (s.sim && document.activeElement !== $('simSpeed')) $('simSpeed').value = s.sim.speed;
  if (firstStatus) { firstStatus = false; $('target').value = s.preheat; }
  for (const [id, label, value, suffix] of [['sp', 'spLabel', s.speedPercent, '%'], ['bps', 'bpsLabel', s.blocksPerSecond, '']]) {
    if (document.activeElement !== $(id)) { $(id).value = value; $(label).textContent = value + suffix; }
  }
  // a lavoro in corso i comandi che muoverebbero la macchina sono spenti
  document.querySelectorAll('[data-idle]').forEach(el => el.disabled = active);
  $('jogLock').hidden = !active; $('testLock').hidden = !active;
  $('startBtn').disabled = active || !ready;

  $('pct').textContent = active ? job.percent.toFixed(1) + '%' : '–';
  $('jobName').textContent = active ? job.name : 'nessun lavoro in corso';
  $('jobState').hidden = !active; $('jobState').textContent = JOBS[job.state] || job.state; $('jobState').className = 'tag ' + job.state;
  $('progress').style.width = (active ? job.percent : 0) + '%';
  $('elapsed').textContent = active ? clock(job.elapsed) : '-';
  $('lines').textContent = active ? it(job.linesDone) + ' / ' + it(job.linesTotal) : '-';
  $('last').textContent = s.lastResult || '-'; $('last').title = s.lastResult || '';
  $('fault').hidden = !s.fault; $('fault').textContent = s.fault;

  const known = s.temperature.known;
  hotness = known ? Math.min(1, Math.max(0, (s.temperature.current - 45) / 190)) : 0;
  $('temp').textContent = known ? s.temperature.current.toFixed(1) : '--';
  $('tempTarget').textContent = known ? (s.temperature.target > 0 ? 'obiettivo ' + s.temperature.target.toFixed(0) + ' °C' : 'riscaldamento spento') : 'sensore non attivo';
  if (known) { samples.push(s.temperature.current); if (samples.length > 160) samples.shift(); spark(s.temperature.target); }

  // il lavoro in corso si segue sul suo percorso: lo si carica se a schermo ce n'e' un altro
  let remaining = '-';
  if (active && job.file) {
    if (!pv || pv.file !== job.file) { if (refresh.loading !== job.file) { refresh.loading = job.file; loadPreview(job.file, job.name); } }
    else {
      if (!live) { live = true; playing = false; playIcon(); cursorTarget = cursor = indexAtCommand(job.linesDone); hud(); }
      remaining = human(pv.total - pv.t[Math.min(pv.n - 1, Math.max(0, Math.floor(cursor)))]);
    }
    $('play').disabled = true; $('timeline').disabled = true;
  } else {
    if (live) { live = false; hud(); }
    refresh.loading = null; $('play').disabled = !pv; $('timeline').disabled = !pv;
  }
  $('remaining').textContent = remaining;
  if (tab === 'history') loadHistory();
}
async function loadHistory() {
  let rows;
  try { rows = (await (await fetch('/api/history')).json()).prints || []; } catch (e) { return; }
  const open = new Set([...document.querySelectorAll('#history details[open]')].map(d => d.dataset.job));
  $('history').innerHTML = rows.map(r => {
    const errors = r.events.filter(e => e.level === 'error').length, warns = r.events.filter(e => e.level === 'warn').length;
    const badges = (errors ? '<span class="tag bad">' + errors + ' errori</span> ' : '') + (warns ? '<span class="tag warnc">' + warns + ' avvisi</span> ' : '');
    const events = r.events.length ? '<details data-job="' + esc(r.jobId) + '"' + (open.has(r.jobId) ? ' open' : '') + ' style="margin:6px 0 0"><summary>' + r.events.length + ' eventi</summary><ul class="events">'
      + r.events.map(e => '<li class="' + esc(e.level) + '"><span class="num">' + new Date(e.at * 1000).toLocaleTimeString('it') + '</span><span>' + esc(e.text) + '</span></li>').join('') + '</ul></details>' : '';
    return '<tr><td class="num">' + new Date(r.startedAt * 1000).toLocaleString('it') + '</td><td>' + esc(r.name) + '</td><td><span class="tag ' + esc(r.state) + '">' + esc(r.state)
      + '</span></td><td class="num">' + it(r.lines) + '</td><td class="num">' + span(r.startedAt, r.finishedAt) + '</td><td class="wide">' + badges + esc(r.error) + events
      + '</td><td><button class="btn small ghost" data-path="' + esc(r.gcodePath) + '" data-name="' + esc(r.name) + '">Apri</button></td></tr>';
  }).join('') || '<tr><td colspan="7" style="color:var(--muted)">Ancora nessun lavoro.</td></tr>';
}
$('history').addEventListener('click', e => { const b = e.target.closest('button[data-path]'); if (b) prepare(b.dataset.path, b.dataset.name, ''); });
$('simSpeed').addEventListener('change', e => api('simspeed', 's=' + e.target.value));

if (gl) { initGl(); fit(); resize(); requestAnimationFrame(draw); }
else { $('pvName').textContent = 'Il browser non supporta WebGL2: il visualizzatore non è disponibile'; }
setInterval(refresh, 1500); refresh();
setInterval(pollLive, 250);
</script>
</body>
</html>
)HTML";
} // namespace local
