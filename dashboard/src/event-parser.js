'use strict';
/**
 * Parses one telemetry line:  EDUOS|1|00001234|CATEGORY|EVENT|key=value|key=value
 * Returns { ok:true, event } or { ok:false, reason, ignored? }.
 *  - lines without the EDUOS prefix are "ignored" (BIOS/console noise), not errors
 *  - unknown protocol versions are returned ok with event.unsupported = true
 *  - unknown categories/events are preserved (generic event)
 */
const MAX_LEN = 512;
const KEY_RE = /^[A-Za-z0-9_]+$/;
const NAME_RE = /^[A-Z][A-Z0-9_]*$/;

function decodeValue(v) {
  try { return decodeURIComponent(v); } catch (_) { return v; }
}

function parseLine(line, maxLen = MAX_LEN) {
  if (typeof line !== 'string') return { ok: false, reason: 'not_string' };
  line = line.replace(/\r$/, '');
  if (!line.startsWith('EDUOS|')) return { ok: false, reason: 'no_prefix', ignored: true };
  if (line.length > maxLen) return { ok: false, reason: 'too_long' };
  const parts = line.split('|');
  if (parts.length < 5) return { ok: false, reason: 'too_few_fields' };
  const [, ver, seq, category, name, ...attrs] = parts;
  if (!/^\d+$/.test(ver)) return { ok: false, reason: 'bad_version' };
  if (!/^\d+$/.test(seq)) return { ok: false, reason: 'bad_sequence' };
  if (!NAME_RE.test(category) || !NAME_RE.test(name)) return { ok: false, reason: 'bad_name' };
  const fields = {};
  for (const a of attrs) {
    const i = a.indexOf('=');
    if (i <= 0) return { ok: false, reason: 'bad_attribute' };
    const k = a.slice(0, i);
    if (!KEY_RE.test(k)) return { ok: false, reason: 'bad_key' };
    fields[k] = decodeValue(a.slice(i + 1));
  }
  const event = { version: Number(ver), seq: Number(seq), category, event: name, fields, raw: line };
  if (event.version !== 1) event.unsupported = true;
  return { ok: true, event };
}

module.exports = { parseLine, MAX_LEN };
