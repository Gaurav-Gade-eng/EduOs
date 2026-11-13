'use strict';
const path = require('path');

// The log path comes only from configuration (env), never from HTTP requests.
module.exports = {
  logPath: process.env.EDUOS_LOG || path.resolve(__dirname, '..', '..', 'build', 'eduos_events.log'),
  port: Number(process.env.PORT) || 3000,
  host: process.env.HOST || '127.0.0.1',
  pollMs: 500,
  staleAfterMs: 15000,       // kernel emits periodic snapshots every ~5 s
  maxLineLength: 512,
  maxEvents: 500,
  maxMemoryHistory: 120,
  maxSwitchHistory: 200,
  maxProcesses: 64,
  maxRecent: 50,
};
