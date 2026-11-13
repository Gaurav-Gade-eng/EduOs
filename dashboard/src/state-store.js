'use strict';
const cfg = require('./config');

const num = (v) => (v === undefined || v === '' || Number.isNaN(Number(v)) ? null : Number(v));
const push = (arr, item, max) => { arr.push(item); if (arr.length > max) arr.splice(0, arr.length - max); };

function freshState() {
  return {
    boot: { version: null, architecture: null, boot_id: null, started: false, ready: false },
    modules: {},
    processes: {},
    scheduler: { algorithm: null, quantum: null, current_pid: null, switches: null, total_ticks: null,
                 idle_ticks: null, last_switch: null, history: [] },
    memory: { total_frames: null, used_frames: null, free_frames: null, frame_size: null,
              allocs: null, frees: null, history: [] },
    paging: { mappings: {}, recent: [], fault_count: 0 },
    disk: { available: false, device: null, sectors: null, sector_size: null, recent: [] },
    filesystem: { mounted: false, result: null, version: null, block_size: null, total_blocks: null,
                  file_count: null, recent: [] },
    syscalls: { total: 0, by_name: {}, recent: [] },
    interrupts: {},
    kernel: { panic: null, info: [] },
    telemetry: { dropped_events: null, created: null, sent: null },
  };
}

class StateStore {
  constructor() {
    this.sessions = 0;
    this.eventId = 0;
    this.events = [];
    this.counters = { parsed: 0, parse_errors: 0, ignored_lines: 0, unsupported: 0 };
    this.lastParseError = null;
    this.lastEventAt = null;
    this.state = freshState();
  }

  reset() { this.state = freshState(); this.events = []; }   // new QEMU session / truncated log

  noteIgnored() { this.counters.ignored_lines++; }
  noteParseError(reason, line) {
    this.counters.parse_errors++;
    this.lastParseError = { reason, line: String(line).slice(0, 120) };
  }

  apply(ev) {
    this.counters.parsed++;
    this.lastEventAt = Date.now();
    if (ev.category === 'BOOT' && ev.event === 'START') { this.reset(); this.sessions++; }
    const entry = { id: ++this.eventId, seq: ev.seq, category: ev.category, event: ev.event,
                    fields: ev.fields, received_at: this.lastEventAt };
    push(this.events, entry, cfg.maxEvents);
    if (ev.unsupported) { this.counters.unsupported++; return; }
    const h = HANDLERS[`${ev.category}.${ev.event}`];
    if (h) h(this.state, ev.fields, ev, entry);
  }

  // ---- views ----
  processList() {
    return Object.values(this.state.processes).sort((a, b) => a.pid - b.pid);
  }
  latestEvents(limit) { return this.events.slice(-limit).reverse(); }
}

const HANDLERS = {
  'BOOT.START'(s, p) { Object.assign(s.boot, { version: p.version, architecture: p.architecture, boot_id: p.boot_id, started: true }); },
  'KERNEL.READY'(s) { s.boot.ready = true; },
  'KERNEL.PANIC'(s, p, ev) { s.kernel.panic = { code: p.code, message: p.message, seq: ev.seq }; },
  'KERNEL.INFO'(s, p) { push(s.kernel.info, { id: p.id, detail: p.detail || null }, 20); },
  'KERNEL.STATS'(s, p) { Object.assign(s.telemetry, { created: num(p.created), sent: num(p.sent), dropped_events: num(p.dropped) }); },

  'MODULE.READY'(s, p, ev) { s.modules[p.name] = { name: p.name, status: 'READY', detail: p.detail || null, seq: ev.seq }; },
  'MODULE.ERROR'(s, p, ev) { s.modules[p.name] = { name: p.name, status: 'ERROR', detail: p.reason || null, seq: ev.seq }; },

  'PROCESS.CREATE'(s, p, ev) { upsertProc(s, p, ev, { state: p.state }); },
  'PROCESS.SNAPSHOT'(s, p, ev) { upsertProc(s, p, ev, { state: p.state, cpu_ticks: num(p.cpu_ticks) }); },
  'PROCESS.STATE'(s, p, ev) {
    const pr = s.processes[num(p.pid)];
    if (pr) { pr.prev_state = pr.state; pr.state = p.state; pr.reason = p.reason || null; pr.changed_seq = ev.seq; }
  },
  'PROCESS.EXIT'(s, p, ev) {
    const pr = s.processes[num(p.pid)];
    if (pr) { pr.prev_state = pr.state; pr.state = 'TERMINATED'; pr.exit_code = num(p.exit_code); pr.reason = p.reason || null; pr.changed_seq = ev.seq; }
  },

  'SCHEDULER.SNAPSHOT'(s, p) {
    Object.assign(s.scheduler, { algorithm: p.algorithm, quantum: num(p.quantum), current_pid: num(p.current_pid),
      switches: num(p.switches), total_ticks: num(p.total_ticks), idle_ticks: num(p.idle_ticks) });
  },
  'SCHEDULER.SWITCH'(s, p, ev) {
    const sw = { from: num(p.from), to: num(p.to), reason: p.reason, total: num(p.total), seq: ev.seq };
    s.scheduler.last_switch = sw;
    s.scheduler.current_pid = sw.to;
    s.scheduler.switches = sw.total;
    push(s.scheduler.history, sw, cfg.maxSwitchHistory);
  },

  'MEMORY.SNAPSHOT'(s, p, ev) {
    Object.assign(s.memory, { total_frames: num(p.total_frames), used_frames: num(p.used_frames),
      free_frames: num(p.free_frames), frame_size: num(p.frame_size), allocs: num(p.allocs), frees: num(p.frees) });
    push(s.memory.history, { seq: ev.seq, t: Date.now(), used: num(p.used_frames), free: num(p.free_frames) }, cfg.maxMemoryHistory);
  },

  'PAGING.MAP'(s, p, ev, entry) {
    if (p.result === 'OK') s.paging.mappings[p.virtual] = { virtual: p.virtual, physical: p.physical, flags: p.flags };
    push(s.paging.recent, { kind: 'MAP', ...p, seq: ev.seq }, cfg.maxRecent); void entry;
  },
  'PAGING.UNMAP'(s, p, ev) {
    if (p.result === 'OK') delete s.paging.mappings[p.virtual];
    push(s.paging.recent, { kind: 'UNMAP', ...p, seq: ev.seq }, cfg.maxRecent);
  },
  'PAGING.FAULT'(s, p, ev) { s.paging.fault_count++; push(s.paging.recent, { kind: 'FAULT', ...p, seq: ev.seq }, cfg.maxRecent); },

  'SYSCALL.CALL'(s, p, ev) {
    s.syscalls.total++;
    s.syscalls.by_name[p.name] = (s.syscalls.by_name[p.name] || 0) + 1;
    push(s.syscalls.recent, { name: p.name, number: num(p.number), pid: num(p.pid), result: num(p.result), seq: ev.seq }, cfg.maxRecent);
  },
  'INTERRUPT.COUNT'(s, p) { s.interrupts[`irq${p.irq}`] = num(p.count); },

  'DISK.READY'(s, p) { Object.assign(s.disk, { available: true, device: p.device, sectors: num(p.sectors), sector_size: num(p.sector_size) }); },
  'DISK.READ'(s, p, ev) { push(s.disk.recent, { op: 'READ', ...p, seq: ev.seq }, cfg.maxRecent); },
  'DISK.WRITE'(s, p, ev) { push(s.disk.recent, { op: 'WRITE', ...p, seq: ev.seq }, cfg.maxRecent); },

  'FILESYSTEM.MOUNT'(s, p) {
    const fsx = s.filesystem;
    fsx.result = p.result;
    fsx.mounted = p.result === 'OK';
    Object.assign(fsx, { version: num(p.version), block_size: num(p.block_size), total_blocks: num(p.total_blocks),
                         file_count: p.result === 'OK' ? num(p.files) : null });
  },
  'FILESYSTEM.FORMAT'(s, p, ev) { push(s.filesystem.recent, { op: 'FORMAT', ...p, seq: ev.seq }, cfg.maxRecent); },
};
for (const op of ['CREATE', 'WRITE', 'DELETE', 'READ']) {
  HANDLERS[`FILESYSTEM.${op}`] = (s, p, ev) => {
    if (p.files !== undefined) s.filesystem.file_count = num(p.files);
    push(s.filesystem.recent, { op, ...p, seq: ev.seq }, cfg.maxRecent);
  };
}

function upsertProc(s, p, ev, extra) {
  const pid = num(p.pid);
  if (pid === null) return;
  const cur = s.processes[pid] || { pid, cpu_ticks: null, prev_state: null, changed_seq: null, exit_code: null, reason: null };
  const state = extra.state || cur.state;
  if (cur.state && cur.state !== state) { cur.prev_state = cur.state; cur.changed_seq = ev.seq; }
  Object.assign(cur, { name: p.name || cur.name, state, priority: p.priority !== undefined ? num(p.priority) : cur.priority });
  if (extra.cpu_ticks !== undefined) cur.cpu_ticks = extra.cpu_ticks;
  s.processes[pid] = cur;
  const keys = Object.keys(s.processes);
  if (keys.length > cfg.maxProcesses) {           // drop oldest terminated entries first
    for (const k of keys) if (s.processes[k].state === 'TERMINATED' && Object.keys(s.processes).length > cfg.maxProcesses) delete s.processes[k];
  }
}

module.exports = { StateStore };
