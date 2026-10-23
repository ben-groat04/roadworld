// Reference photo register: .claude/state/ref_log.jsonl of the main checkout, one line per picture file that appeared (or changed) under docs/reference:
// { ts, path (relative to docs/reference), sha1, size, by }. Written by the PostToolUse hook (ref_log.mjs) after tool calls, and once by `node tools/ref_seed.mjs` for the
// photos that already exist. The hand-in gate (gates_lib.referenceProblems) accepts a reference photo only when its sha1 is in this log or in a docs/reference INDEX.md,
// so a picture made up inside the round (a copy of the render, a composite) is not a reference.
// WHAT IT CANNOT SEE: a made-up picture that is NOT a copy of the round's own pictures and lands in docs/reference during a tool call is registered like a download
// (the hook sees files appear, not where they came from); `by` and `ts` tell the auditor which agent and when.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export const REF_EXT = /\.(?:jpe?g|png|webp|gif|bmp|tiff?)$/i;
const sha1File = (p) => crypto.createHash('sha1').update(fs.readFileSync(p)).digest('hex');

// all picture files below docs/reference except _compare (composites are the rounds' own pictures); links are not followed
export function listReferenceFiles(mainRoot) {
  const base = path.join(mainRoot, 'docs', 'reference'), out = [];
  const walk = (d, depth) => {
    if (depth > 6) return;
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      const p = path.join(d, e.name);
      if (e.isSymbolicLink()) continue;
      if (e.isDirectory()) { if (e.name !== '_compare') walk(p, depth + 1); } else if (REF_EXT.test(e.name)) out.push(p);
    }
  };
  walk(base, 0);
  return out.map((p) => ({ abs: p, rel: path.relative(base, p).split(path.sep).join('/') }));
}

// Registers files that are new or changed since the last scan (size + mtime key in ref_cache.json). Returns { added, total }.
export function scanReferences(mainRoot, stateDir, by = 'hook', now = Date.now()) {
  const files = listReferenceFiles(mainRoot);
  if (!files.length) return { added: 0, total: 0 };
  const cacheFile = path.join(stateDir, 'ref_cache.json');
  let cache = {};
  try { cache = JSON.parse(fs.readFileSync(cacheFile, 'utf8')) || {}; } catch { cache = {}; }
  const lines = [];
  for (const f of files) {
    let st; try { st = fs.statSync(f.abs); } catch { continue; }
    const key = `${st.size}:${Math.round(st.mtimeMs)}`;
    if (cache[f.rel] && cache[f.rel].key === key) continue;
    let sha; try { sha = sha1File(f.abs); } catch { continue; }
    // gates6 (audit item 26): a picture whose PATH already had another sha1 was overwritten, not downloaded: written by a subagent it is logged as replaced and never counts as a reference
    const prevSha = cache[f.rel] && cache[f.rel].sha1; const replaced = !!prevSha && prevSha !== sha && /^agent:/.test(by);
    cache[f.rel] = { key, sha1: sha };
    lines.push(JSON.stringify({ ts: new Date(now).toISOString(), path: f.rel, sha1: sha, size: st.size, by, ...(replaced ? { replaced: true, prev_sha1: prevSha } : {}) }));
  }
  if (lines.length) {
    fs.mkdirSync(stateDir, { recursive: true });
    fs.appendFileSync(path.join(stateDir, 'ref_log.jsonl'), lines.join('\n') + '\n');
    fs.writeFileSync(cacheFile, JSON.stringify(cache));
  }
  return { added: lines.length, total: files.length };
}
