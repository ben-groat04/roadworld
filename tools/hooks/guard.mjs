// Runs a hook implementation behind a FAIL-OPEN guard. Imports ONLY node built-ins, so a syntax error or missing import in lib.mjs,
// audit_lib.mjs or the implementation itself still ends as: one FAIL-OPEN line in .claude/state/gate.log, exit 0 (gate audit fault 9).
// Usage in a hook file: import { runGuarded } from './guard.mjs'; runGuarded('brief', './brief_gate_impl.mjs');
// The implementation exports `async function main()` returning the exit code (0 allow, 2 deny where the hook supports it).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export function failOpenLog(gate, reason, error) {
  try {
    const dir = path.join(HERE, '..', '..', '.claude', 'state');
    fs.mkdirSync(dir, { recursive: true });
    fs.appendFileSync(path.join(dir, 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate, decision: 'allow', reason: 'FAIL-OPEN: ' + reason, error: String((error && (error.stack || error.message)) || error || '').slice(0, 500) }) + '\n');
  } catch { /* nothing more can be done */ }
}
export async function runGuarded(gate, implFile) {
  let code = 0;
  try {
    const mod = await import(new URL(implFile, import.meta.url).href);
    code = await mod.main();
  } catch (e) {
    failOpenLog(gate, 'hook implementation could not load or crashed', e);
    code = 0;
  }
  process.exit(Number.isInteger(code) ? code : 0);
}
