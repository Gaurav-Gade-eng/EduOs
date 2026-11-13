'use strict';
const fs = require('fs');

/**
 * Tails an append-only log file. Only complete (newline-terminated) lines are delivered;
 * a partial trailing line is buffered until its newline arrives. If the file shrinks
 * (QEMU restarted with -serial file:) the reader resets and calls onReset().
 */
class LogReader {
  constructor(filePath, { onLine, onReset, onStatus } = {}) {
    this.path = filePath;
    this.onLine = onLine || (() => {});
    this.onReset = onReset || (() => {});
    this.onStatus = onStatus || (() => {});
    this.offset = 0;
    this.partial = '';
    this.exists = false;
    this.lastChangeAt = null;
    this.lastMtimeMs = 0;
    this.bytesRead = 0;
    this.error = null;
    this.timer = null;
    this.busy = false;
  }

  async poll() {
    if (this.busy) return;
    this.busy = true;
    try {
      let st;
      try { st = await fs.promises.stat(this.path); }
      catch (e) {
        if (e.code === 'ENOENT') { this.exists = false; this.error = null; return; }
        throw e;
      }
      this.exists = true;
      this.error = null;
      if (st.size < this.offset) {          // truncated / replaced
        this.offset = 0; this.partial = ''; this.onReset();
      }
      if (st.mtimeMs !== this.lastMtimeMs) { this.lastMtimeMs = st.mtimeMs; this.lastChangeAt = Date.now(); }
      if (st.size > this.offset) {
        const fh = await fs.promises.open(this.path, 'r');
        try {
          const len = st.size - this.offset;
          const buf = Buffer.alloc(len);
          const { bytesRead } = await fh.read(buf, 0, len, this.offset);
          this.offset += bytesRead;
          this.bytesRead += bytesRead;
          this.lastChangeAt = Date.now();
          this._feed(buf.toString('utf8', 0, bytesRead));
        } finally { await fh.close(); }
      }
    } catch (e) {
      this.error = e.message;
    } finally {
      this.busy = false;
      this.onStatus(this.status());
    }
  }

  _feed(text) {
    const data = this.partial + text;
    const lines = data.split('\n');
    this.partial = lines.pop();             // incomplete tail (maybe '')
    if (this.partial.length > 4096) this.partial = '';   // runaway garbage guard
    for (const l of lines) this.onLine(l);
  }

  status() {
    return { exists: this.exists, lastChangeAt: this.lastChangeAt, bytesRead: this.bytesRead, error: this.error };
  }

  start(intervalMs = 500) {
    this.stop();
    this.poll();
    this.timer = setInterval(() => this.poll(), intervalMs);
    this.timer.unref();
  }

  stop() { if (this.timer) clearInterval(this.timer); this.timer = null; }
}

module.exports = { LogReader };
