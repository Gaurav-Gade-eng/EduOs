'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseLine } = require('../src/event-parser');
const { StateStore } = require('../src/state-store');
const { LogReader } = require('../src/log-reader');
const { createApp } = require('../server');

const FIXTURE = path.resolve(__dirname, '..', '..', 'tests', 'sample-events', 'boot-session.log');

function loadFixture(store) {
  for (const line of fs.readFileSync(FIXTURE, 'utf8').split('\n')) {
    if (!line) continue;
    const r = parseLine(line);
    if (r.ok) store.apply(r.event); else if (r.ignored) store.noteIgnored(); else store.noteParseError(r.reason, line);
  }
}

test('parser: valid line, decoding, unknown version', () => {
  const r = parseLine('EDUOS|1|00000007|PROCESS|CREATE|pid=3|name=my%20task|state=READY');
  assert.ok(r.ok);
  assert.strictEqual(r.event.fields.name, 'my task');
  assert.strictEqual(r.event.seq, 7);
  assert.ok(parseLine('EDUOS|2|00000001|X|Y').event.unsupported);
});

test('parser: rejects malformed, ignores noise, never throws', () => {
  assert.strictEqual(parseLine('hello').ignored, true);
  for (const bad of ['EDUOS|1|x|A|B', 'EDUOS|1|1|a|B', 'EDUOS|1|1|A|B|noequals', 'EDUOS|1|1|A|B|b!d=1', 'EDUOS|1', 'EDUOS|1|1|A|B|' + 'x'.repeat(600)]) {
    assert.strictEqual(parseLine(bad).ok, false, bad);
  }
  assert.ok(parseLine('EDUOS|1|1|A|B|k=%E0%A4%A').ok);     // bad percent-escape is kept raw, no crash
});

test('state store reconstructs fixture session', () => {
  const store = new StateStore();
  loadFixture(store);
  const s = store.state;
  assert.strictEqual(s.boot.ready, true);
  assert.strictEqual(s.modules.EduFS.status, 'READY');
  assert.strictEqual(s.memory.total_frames, 3840);
  assert.strictEqual(s.memory.history.length, 1);
  assert.strictEqual(s.processes[3].state, 'TERMINATED');
  assert.strictEqual(s.processes[2].state, 'READY');
  assert.strictEqual(s.scheduler.switches, 3);
  assert.strictEqual(s.paging.fault_count, 1);
  assert.deepStrictEqual(Object.keys(s.paging.mappings), []);
  assert.strictEqual(s.filesystem.file_count, 1);
  assert.strictEqual(s.disk.sectors, 65536);
  assert.strictEqual(s.syscalls.by_name.yield, 1);
  assert.strictEqual(s.telemetry.dropped_events, 0);
  assert.ok(store.counters.parse_errors >= 3);
  assert.strictEqual(store.counters.ignored_lines, 1);
  assert.strictEqual(store.counters.unsupported, 1);
  // delimiter-forging filename was percent-decoded into a single field, not a second record
  const forged = store.events.find((e) => e.event === 'CREATE' && e.fields.result === 'ERR_INVAL');
  assert.strictEqual(forged.fields.name, 'bad|name\nforged');
});

test('BOOT START resets state (new QEMU session)', () => {
  const store = new StateStore();
  loadFixture(store); loadFixture(store);
  assert.strictEqual(store.sessions, 2);
  assert.strictEqual(Object.keys(store.state.processes).length, 4);
});

test('log reader: partial lines, append, truncation', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'eduos-'));
  const file = path.join(dir, 'e.log');
  const lines = []; let resets = 0;
  const rd = new LogReader(file, { onLine: (l) => lines.push(l), onReset: () => resets++ });
  await rd.poll();
  assert.strictEqual(rd.status().exists, false);
  fs.writeFileSync(file, 'EDUOS|1|1|A|B\nEDUOS|1|2|A|');
  await rd.poll();
  assert.deepStrictEqual(lines, ['EDUOS|1|1|A|B']);          // partial tail withheld
  fs.appendFileSync(file, 'B\n');
  await rd.poll();
  assert.deepStrictEqual(lines, ['EDUOS|1|1|A|B', 'EDUOS|1|2|A|B']);
  fs.writeFileSync(file, 'x\n');                              // shorter file => truncation
  await rd.poll();
  assert.strictEqual(resets, 1);
  assert.strictEqual(lines.at(-1), 'x');
  fs.rmSync(dir, { recursive: true });
});

test('HTTP API end to end', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'eduos-'));
  const file = path.join(dir, 'e.log');
  fs.copyFileSync(FIXTURE, file);
  const { app, reader } = createApp({ logPath: file });
  await reader.poll();
  const server = app.listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  const base = 'http://127.0.0.1:' + server.address().port;
  try {
    const status = await (await fetch(base + '/api/status')).json();
    assert.strictEqual(status.status, 'RUNNING');
    assert.strictEqual(status.telemetry.status, 'LIVE');
    assert.strictEqual(status.process_count, 3);
    const procs = await (await fetch(base + '/api/processes')).json();
    assert.strictEqual(procs.processes.length, 4);
    assert.strictEqual((await fetch(base + '/api/events/latest?limit=0')).status, 400);
    assert.strictEqual((await fetch(base + '/api/events/latest?limit=abc')).status, 400);
    assert.strictEqual((await fetch(base + '/api/events/latest?limit=5')).status, 200);
    assert.strictEqual((await fetch(base + '/api/nope')).status, 404);
    const html = await (await fetch(base + '/')).text();
    assert.ok(html.includes('EduOS'));
  } finally { server.close(); fs.rmSync(dir, { recursive: true }); }
});

test('API reports missing log', async () => {
  const { app, reader } = createApp({ logPath: path.join(os.tmpdir(), 'definitely-missing-eduos.log') });
  await reader.poll();
  const server = app.listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  try {
    const h = await (await fetch('http://127.0.0.1:' + server.address().port + '/api/health')).json();
    assert.strictEqual(h.telemetry.status, 'LOG_MISSING');
  } finally { server.close(); }
});
