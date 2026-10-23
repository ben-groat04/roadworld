#!/usr/bin/env node
// Registers every picture that already exists under docs/reference in .claude/state/ref_log.jsonl (by "seed"), once. The main session runs it in the MAIN checkout after the
// reference folder was rebuilt or the register is new; from then on the PostToolUse hook (tools/hooks/ref_log.mjs) registers new files itself.
// Usage: node tools/ref_seed.mjs [--force]   (refuses to run in a worktree: its .claude/state is not the live one)
import { ROOT, STATE_DIR } from './hooks/lib.mjs';
import { isMainCheckout } from './hooks/notice_lib.mjs';
import { scanReferences } from './hooks/ref_lib.mjs';

if (!isMainCheckout(ROOT) && !process.argv.includes('--force')) { console.error('REFUSED: this copy is in a git worktree; run it from the main checkout (or --force for a test).'); process.exit(1); }
const r = scanReferences(ROOT, STATE_DIR, 'seed');
console.log(`reference register: ${r.added} picture(s) newly registered, ${r.total} under docs/reference (log: ${STATE_DIR}/ref_log.jsonl)`);
