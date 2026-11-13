'use strict';
// Demo helper: appends a fixture's lines to a log file one by one, imitating QEMU's serial output.
// usage: node tools/replay.js <fixture> <logfile> [delayMs=300]
const fs = require('fs');
const path = require('path');
const [src, dst, delay = '300'] = process.argv.slice(2);
if (!src || !dst) { console.error('usage: node tools/replay.js <fixture> <logfile> [delayMs]'); process.exit(1); }
const lines = fs.readFileSync(src, 'utf8').split('\n').filter((l) => l.length);
fs.mkdirSync(path.dirname(dst), { recursive: true });
fs.writeFileSync(dst, '');           // like QEMU: fresh file per run
let i = 0;
const t = setInterval(() => {
  if (i >= lines.length) { clearInterval(t); console.log('replay done'); return; }
  fs.appendFileSync(dst, lines[i++] + '\n');
}, Number(delay));
