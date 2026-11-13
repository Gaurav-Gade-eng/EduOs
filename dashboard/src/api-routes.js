'use strict';
const express = require('express');
const cfg = require('./config');

/** Builds the JSON API. `reader` is a LogReader, `store` a StateStore. */
function createApiRouter({ reader, store, staleAfterMs = cfg.staleAfterMs }) {
  const r = express.Router();

  function telemetryHealth() {
    const st = reader.status();
    const age = store.lastEventAt ? Date.now() - store.lastEventAt : null;
    let status = 'WAITING_FOR_DATA';
    if (!st.exists) status = 'LOG_MISSING';
    else if (st.error) status = 'LOG_ERROR';
    else if (age !== null) status = age > staleAfterMs ? 'STALE' : 'LIVE';
    return {
      status, connected: status === 'LIVE', log_exists: st.exists, log_error: st.error,
      last_event_at: store.lastEventAt ? new Date(store.lastEventAt).toISOString() : null,
      last_event_age_ms: age, bytes_read: st.bytesRead, sessions_seen: store.sessions,
      ...store.counters, last_parse_error: store.lastParseError,
      dropped_events: store.state.telemetry.dropped_events,
    };
  }

  r.get('/health', (_req, res) => res.json({ backend: 'ok', protocol_version: 1, telemetry: telemetryHealth() }));

  r.get('/status', (_req, res) => {
    const s = store.state;
    res.json({
      status: s.kernel.panic ? 'PANIC' : s.boot.ready ? 'RUNNING' : s.boot.started ? 'BOOTING' : 'NO_DATA',
      version: s.boot.version, architecture: s.boot.architecture, boot_id: s.boot.boot_id,
      process_count: store.processList().filter((p) => p.state !== 'TERMINATED').length,
      scheduler: { ...s.scheduler, history: undefined },
      memory: { ...s.memory, history: undefined },
      modules: Object.values(s.modules),
      paging: { available: !!s.modules.Paging && s.modules.Paging.status === 'READY', recent_events: s.paging.recent.slice(-10) },
      disk: { ...s.disk, recent: undefined },
      filesystem: { ...s.filesystem, recent: undefined },
      panic: s.kernel.panic,
      telemetry: telemetryHealth(),
    });
  });

  r.get('/processes', (_req, res) => {
    const list = store.processList();
    res.json({ count: list.filter((p) => p.state !== 'TERMINATED').length, processes: list });
  });
  r.get('/memory', (_req, res) => res.json(store.state.memory));
  r.get('/scheduler', (_req, res) => res.json(store.state.scheduler));
  r.get('/paging', (_req, res) => {
    const p = store.state.paging;
    res.json({ mappings: Object.values(p.mappings), fault_count: p.fault_count, recent: p.recent });
  });
  r.get('/disk', (_req, res) => res.json(store.state.disk));
  r.get('/filesystem', (_req, res) => res.json(store.state.filesystem));
  r.get('/modules', (_req, res) => res.json({ modules: Object.values(store.state.modules) }));
  r.get('/syscalls', (_req, res) => res.json(store.state.syscalls));
  r.get('/interrupts', (_req, res) => res.json(store.state.interrupts));

  r.get('/events/latest', (req, res) => {
    let limit = 50;
    if (req.query.limit !== undefined) {
      if (!/^\d+$/.test(String(req.query.limit))) return res.status(400).json({ error: 'limit must be an integer' });
      limit = Number(req.query.limit);
      if (limit < 1 || limit > cfg.maxEvents) return res.status(400).json({ error: `limit must be 1..${cfg.maxEvents}` });
    }
    res.json({ events: store.latestEvents(limit) });
  });

  r.use((_req, res) => res.status(404).json({ error: 'not found' }));
  return r;
}

module.exports = { createApiRouter };
