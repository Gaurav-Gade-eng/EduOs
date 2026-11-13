'use strict';
// All dynamic text is inserted with textContent (never innerHTML) so kernel text cannot inject HTML.
// Missing telemetry is shown as "—" / "Not reported"; nothing is invented.

const NR = '—';
const EXPECTED_MODULES = ['Kernel_Core', 'Serial_Telemetry', 'GDT', 'IDT', 'Interrupt_Controller', 'Timer', 'Keyboard',
  'Process_Manager', 'Scheduler', 'Physical_Memory_Manager', 'Paging', 'System_Call_Interface', 'ATA_Driver', 'EduFS', 'Shell'];

const $ = (id) => document.getElementById(id);
const state = { page: 'processes', sub: 'memory', selectedPid: null, prevProcStates: {}, timer: null, rateSamples: [], failures: 0 };

function el(tag, props = {}, ...kids) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) { if (k === 'class') n.className = v; else n.setAttribute(k, v); }
  for (const kid of kids) n.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  return n;
}
const show = (v) => (v === null || v === undefined || v === '' ? NR : v);
const card = (k, v, s) => el('div', { class: 'card' }, el('div', { class: 'k' }, k), el('div', { class: 'v' }, show(v)), s ? el('div', { class: 's' }, s) : '');

async function api(path) {
  const r = await fetch('/api/' + path, { cache: 'no-store' });
  if (!r.ok) throw new Error(path + ' -> HTTP ' + r.status);
  return r.json();
}

// ---------------------------------------------------------------- header / health
function renderHealth(status) {
  const t = status.telemetry;
  const badge = $('health'), banner = $('banner');
  const map = { LIVE: ['live', 'Telemetry live'], STALE: ['stale', 'Telemetry stale'], LOG_MISSING: ['bad', 'Log missing'],
    LOG_ERROR: ['bad', 'Log error'], WAITING_FOR_DATA: ['stale', 'Waiting for data'] };
  const [cls, label] = map[t.status] || ['', t.status];
  badge.className = 'badge ' + cls; badge.textContent = label;
  let msg = null, bad = false;
  if (status.status === 'PANIC') { msg = 'KERNEL PANIC: ' + status.panic.code + ' — ' + status.panic.message; bad = true; }
  else if (t.status === 'LOG_MISSING') { msg = 'Serial log not found. Start EduOS in QEMU (make run) or run "npm run demo".'; bad = true; }
  else if (t.status === 'STALE') msg = 'No new kernel events for ' + Math.round(t.last_event_age_ms / 1000) + ' s — QEMU may have stopped. Showing last known data.';
  else if (t.status === 'WAITING_FOR_DATA') msg = 'Log exists but no valid events yet.';
  else if (t.dropped_events > 0) msg = 'Kernel dropped ' + t.dropped_events + ' telemetry events (queue overflow).';
  banner.hidden = !msg;
  if (msg) { banner.textContent = msg; banner.className = 'banner' + (bad ? ' bad' : ''); }
}

// ---------------------------------------------------------------- processes
async function renderProcesses() {
  const data = await api('processes');
  const q = $('proc-search').value.trim().toLowerCase();
  const rows = data.processes.filter((p) => !q || [p.pid, p.name, p.state].join(' ').toLowerCase().includes(q));
  $('proc-count').textContent = 'Kernel reports ' + data.count + ' live process(es)';
  const body = $('proc-body');
  body.replaceChildren();
  if (!rows.length) body.append(el('tr', {}, el('td', { colspan: 6, class: 'muted' }, data.processes.length ? 'No matches' : 'Waiting for data')));
  for (const p of rows) {
    const changed = state.prevProcStates[p.pid] !== undefined && state.prevProcStates[p.pid] !== p.state;
    const tr = el('tr', { class: (changed ? 'changed ' : '') + (state.selectedPid === p.pid ? 'sel' : '') },
      el('td', {}, p.pid), el('td', {}, show(p.name)), el('td', { class: 's-' + p.state }, show(p.state)),
      el('td', {}, show(p.priority)), el('td', {}, show(p.cpu_ticks)), el('td', {}, show(p.reason)));
    tr.addEventListener('click', () => { state.selectedPid = p.pid; renderProcesses(); });
    body.append(tr);
  }
  state.prevProcStates = Object.fromEntries(data.processes.map((p) => [p.pid, p.state]));
  const sel = data.processes.find((p) => p.pid === state.selectedPid);
  const box = $('proc-detail');
  box.hidden = !sel;
  if (sel) {
    box.replaceChildren(el('b', {}, 'PID ' + sel.pid + ' — ' + show(sel.name)),
      el('div', { class: 'muted' }, 'state ' + show(sel.state) + ' (previous ' + show(sel.prev_state) + '), priority ' + show(sel.priority) +
        ', cpu ticks ' + show(sel.cpu_ticks) + ', exit code ' + show(sel.exit_code)));
  }
}

// ---------------------------------------------------------------- performance
function drawLine(canvas, series, colour, maxHint) {
  const g = canvas.getContext('2d'), w = canvas.width, h = canvas.height;
  g.clearRect(0, 0, w, h);
  g.strokeStyle = '#26323f'; g.lineWidth = 1;
  for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, (h * i) / 4); g.lineTo(w, (h * i) / 4); g.stroke(); }
  if (series.length < 2) { g.fillStyle = '#8b99a8'; g.font = '14px sans-serif'; g.fillText('Waiting for samples…', 16, 24); return; }
  const max = Math.max(maxHint || 0, ...series, 1);
  g.strokeStyle = colour; g.lineWidth = 2; g.beginPath();
  series.forEach((v, i) => { const x = (i / (series.length - 1)) * (w - 8) + 4, y = h - 6 - (v / max) * (h - 12); i ? g.lineTo(x, y) : g.moveTo(x, y); });
  g.stroke();
}

async function renderPerformance() {
  if (state.sub === 'memory') {
    const m = await api('memory');
    const mb = (f) => (f === null || m.frame_size === null ? null : (f * m.frame_size / 1048576).toFixed(2) + ' MiB');
    const c = $('mem-cards'); c.replaceChildren(
      card('Total frames', m.total_frames, mb(m.total_frames)), card('Used frames', m.used_frames, mb(m.used_frames)),
      card('Free frames', m.free_frames, mb(m.free_frames)), card('Frame size', m.frame_size === null ? null : m.frame_size + ' B'),
      card('Allocations', m.allocs), card('Frees', m.frees));
    drawLine($('mem-chart'), m.history.map((s) => s.used), '#4aa3ff', m.total_frames || 0);
  } else {
    const s = await api('scheduler');
    const now = Date.now();
    if (s.switches !== null) { state.rateSamples.push({ t: now, n: s.switches }); state.rateSamples = state.rateSamples.slice(-60); }
    const rs = state.rateSamples; let rate = null;
    if (rs.length >= 2) { const a = rs[0], b = rs[rs.length - 1]; if (b.t > a.t && b.n >= a.n) rate = ((b.n - a.n) / ((b.t - a.t) / 1000)).toFixed(2) + ' / s'; }
    const util = s.total_ticks > 0 && s.idle_ticks !== null ? (100 - (100 * s.idle_ticks) / s.total_ticks).toFixed(1) + ' %' : null;
    const l = s.last_switch;
    $('sched-cards').replaceChildren(card('Algorithm', s.algorithm), card('Quantum', s.quantum === null ? null : s.quantum + ' ticks'),
      card('Current PID', s.current_pid), card('Context switches', s.switches), card('Switch rate', rate, 'from successive samples'),
      card('Last switch', l ? l.from + ' → ' + l.to : null, l ? 'reason ' + l.reason : ''), card('Busy (kernel ticks)', util, 'total − idle ticks'));
    const series = []; const rsArr = rs.map((x) => x.n);
    for (let i = 1; i < rsArr.length; i++) series.push(rsArr[i] - rsArr[i - 1]);
    drawLine($('sw-chart'), series, '#3ecf8e', 1);
  }
}

// ---------------------------------------------------------------- details
async function renderDetails() {
  const [status, ev, sys, irq, paging, disk, fsx] = await Promise.all([api('status'), api('events/latest?limit=200'),
    api('syscalls'), api('interrupts'), api('paging'), api('disk'), api('filesystem')]);
  const t = status.telemetry;
  $('detail-cards').replaceChildren(
    card('Telemetry', t.status, t.last_event_age_ms === null ? '' : 'last event ' + Math.round(t.last_event_age_ms / 1000) + ' s ago'),
    card('Events parsed', t.parsed, t.parse_errors + ' malformed, ' + t.ignored_lines + ' ignored'),
    card('Kernel dropped', t.dropped_events), card('Syscalls seen', sys.total),
    card('Timer IRQs', irq.irq0), card('Keyboard IRQs', irq.irq1), card('Page faults', paging.fault_count),
    card('Sessions seen', t.sessions_seen));

  const cats = [...new Set(ev.events.map((e) => e.category))].sort();
  const sel = $('ev-cat'), cur = sel.value;
  sel.replaceChildren(el('option', { value: '' }, 'All categories'), ...cats.map((c) => el('option', { value: c }, c)));
  sel.value = cats.includes(cur) ? cur : '';
  const q = $('ev-search').value.trim().toLowerCase();
  const body = $('ev-body'); body.replaceChildren();
  for (const e of ev.events) {
    const f = Object.entries(e.fields).map(([k, v]) => k + '=' + v).join('  ');
    if (sel.value && e.category !== sel.value) continue;
    if (q && !(e.category + ' ' + e.event + ' ' + f).toLowerCase().includes(q)) continue;
    body.append(el('tr', {}, el('td', {}, e.seq), el('td', {}, e.category), el('td', {}, e.event), el('td', { class: 'fields' }, f)));
  }
  if (!body.children.length) body.append(el('tr', {}, el('td', { colspan: 4, class: 'muted' }, 'No events')));

  const list = (items, fmt) => items.length ? el('div', {}, ...items.slice(-8).reverse().map((i) => el('div', { class: 'fields' }, fmt(i)))) : el('div', { class: 'muted' }, 'Not reported');
  $('paging-box').replaceChildren(
    el('div', {}, 'Active dynamic mappings: ' + paging.mappings.length),
    list(paging.mappings, (m) => m.virtual + ' → ' + m.physical + ' ' + m.flags),
    el('div', { class: 'muted' }, 'Recent paging events'),
    list(paging.recent, (r) => r.kind + ' ' + Object.entries(r).filter(([k]) => k !== 'kind' && k !== 'seq').map(([k, v]) => k + '=' + v).join(' ')));
  $('fs-box').replaceChildren(
    el('div', {}, 'Disk: ' + (disk.available ? disk.device + ', ' + disk.sectors + ' sectors × ' + disk.sector_size + ' B' : 'Not reported')),
    el('div', {}, 'EduFS: ' + (fsx.result === null ? 'Not reported' : fsx.mounted ? 'mounted v' + fsx.version + ', files ' + show(fsx.file_count) + ', blocks ' + show(fsx.total_blocks) : 'not mounted (' + fsx.result + ')')),
    el('div', { class: 'muted' }, 'Recent file-system operations'),
    list(fsx.recent, (r) => r.op + ' ' + show(r.name) + ' ' + show(r.result)),
    el('div', { class: 'muted' }, 'Recent disk operations'),
    list(disk.recent, (r) => r.op + ' lba=' + r.lba + ' count=' + r.count + ' ' + r.result));
}

// ---------------------------------------------------------------- services
async function renderServices() {
  const { modules } = await api('modules');
  const by = Object.fromEntries(modules.map((m) => [m.name, m]));
  const names = [...EXPECTED_MODULES, ...modules.map((m) => m.name).filter((n) => !EXPECTED_MODULES.includes(n))];
  $('svc-cards').replaceChildren(...names.map((n) => {
    const m = by[n];
    const label = !m ? 'Not reported' : m.status === 'READY' ? 'Ready' : 'Error';
    const c = el('div', { class: 'card' }, el('div', { class: 'k' }, n.replace(/_/g, ' ')),
      el('div', { class: 'v ' + (!m ? 'st-unknown' : m.status === 'READY' ? 'st-ready' : 'st-error') }, label));
    if (m && m.detail) c.append(el('div', { class: 's' }, m.detail));
    return c;
  }));
}

// ---------------------------------------------------------------- loop
async function refresh() {
  try {
    const status = await api('status');
    renderHealth(status);
    state.failures = 0;
    if (state.page === 'processes') await renderProcesses();
    else if (state.page === 'performance') await renderPerformance();
    else if (state.page === 'details') await renderDetails();
    else await renderServices();
  } catch (e) {
    state.failures++;
    $('health').className = 'badge bad'; $('health').textContent = 'Backend unreachable';
    $('banner').hidden = false; $('banner').className = 'banner bad'; $('banner').textContent = 'Cannot reach the dashboard backend (' + e.message + '). Retrying…';
  }
}
function schedule() { clearInterval(state.timer); state.timer = setInterval(refresh, Number($('interval').value)); }

document.querySelectorAll('#nav button').forEach((b) => b.addEventListener('click', () => {
  state.page = b.dataset.page;
  document.querySelectorAll('#nav button').forEach((x) => x.classList.toggle('active', x === b));
  document.querySelectorAll('.page').forEach((p) => { p.hidden = p.id !== 'page-' + state.page; });
  refresh();
}));
document.querySelectorAll('.tabs button').forEach((b) => b.addEventListener('click', () => {
  state.sub = b.dataset.sub;
  document.querySelectorAll('.tabs button').forEach((x) => x.classList.toggle('active', x === b));
  $('sub-memory').hidden = state.sub !== 'memory'; $('sub-scheduler').hidden = state.sub !== 'scheduler';
  refresh();
}));
$('refresh').addEventListener('click', refresh);
$('interval').addEventListener('change', schedule);
['proc-search', 'ev-search'].forEach((id) => $(id).addEventListener('input', refresh));
$('ev-cat').addEventListener('change', refresh);
refresh(); schedule();
