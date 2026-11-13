'use strict';
const path = require('path');
const express = require('express');
const cfg = require('./src/config');
const { parseLine } = require('./src/event-parser');
const { LogReader } = require('./src/log-reader');
const { StateStore } = require('./src/state-store');
const { createApiRouter } = require('./src/api-routes');

function createApp(options = {}) {
  const logPath = options.logPath || cfg.logPath;
  const store = new StateStore();
  const reader = new LogReader(logPath, {
    onLine: (line) => {
      const r = parseLine(line, cfg.maxLineLength);
      if (r.ok) store.apply(r.event);
      else if (r.ignored) { if (line.trim()) store.noteIgnored(); }
      else store.noteParseError(r.reason, line);
    },
    onReset: () => store.reset(),
  });
  const app = express();
  app.disable('x-powered-by');
  app.use('/api', createApiRouter({ reader, store, staleAfterMs: options.staleAfterMs }));
  app.use(express.static(path.join(__dirname, 'public')));
  return { app, reader, store };
}

if (require.main === module) {
  const { app, reader } = createApp();
  reader.start(cfg.pollMs);
  const server = app.listen(cfg.port, cfg.host, () => {
    console.log(`EduOS dashboard: http://${cfg.host}:${cfg.port}  (log: ${cfg.logPath})`);
  });
  const stop = () => { reader.stop(); server.close(() => process.exit(0)); };
  process.on('SIGINT', stop);
  process.on('SIGTERM', stop);
}

module.exports = { createApp };
