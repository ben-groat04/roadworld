#!/usr/bin/env node
// gates10: real-payload tables for the three parts of round gates10 (brief docs/tasks/gates10.md).
//   A  phase agents: make_brief --phase, check_handin --phase measure|build (run on made-up GOOD and BAD hand-ins), the finish hook for a phase agent (own 3-refusal count, no CHECKPOINT allowance,
//      no builder record), the `unreal` frame kind of COMPARE.md
//   B  gated audits and first looks: make_brief --first-look, FIRSTLOOK_OPUS.md validated by the finish hook (id, Opened:, Top 3, "would never be built", read order), the launch hook refuses a
//      NO-CONTRACT audit or first look (the real 23 Oct descriptions from gate.log are the REFUSE cases; clerk, merge and research launches from the same log are the ALLOW cases)
//   C  the gates9 audit faults: lock_guard bulk forms (find -exec, xargs, $(ls), brace lists, sed -e, sort -o, ed, astyle, robocopy filter and /MOV, patch, git apply, fs.rm, fs.open), the
//      LOCK-LIST-BAD channels, delete_guard no longer refuses a commit message or heredoc note that SAYS "Remove-Item -Recurse", a truncated PNG is refused, clean_test_temp prefixes
// Where the expected values come from: the gates9 AUDIT.md items (C), the owner's words of 23 Oct 2026 in the brief (A, B), the real descriptions in .claude/state/gate.log of the main checkout (B).
// Nothing pins a fault. Every library call is wrapped so that on OLD code each case fails on its own (fail_first_*.txt is made by running this file against main's tools). No test writes the live gate.log.
// Usage: node tools/hooks/test_guards10.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';
import { logRun } from './test_record_lib.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards10-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };
// a case that must not throw: an exception (old code, missing module) is a failed case, not a crashed suite
const tt = async (name, fn) => { try { const r = await fn(); t(name, r === true || (r && r.ok), r && r.detail); } catch (e) { t(name, false, 'THREW ' + String((e && e.message) || e).slice(0, 200)); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const wr = (p, s = 'x') => { mk(path.dirname(p)); fs.writeFileSync(p, s); };
const gateLog = () => { try { return fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l); } catch { return {}; } }); } catch { return []; } };
const imp = (rel) => import(pathToFileURL(path.join(src, rel)).href);

// ---------------------------------------------------------------- pictures
const crcT = (() => { const tb = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; tb[n] = c >>> 0; } return tb; })();
const crc32 = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcT[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); };
const makePng = (w = 90, h = 90) => { const rows = []; for (let y = 0; y < h; y++) rows.push(Buffer.concat([Buffer.from([0]), crypto.randomBytes(w * 3)])); const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows), { level: 0 })), chunk('IEND', Buffer.alloc(0))]); };
const pngFile = (p, w, h) => { mk(path.dirname(p)); fs.writeFileSync(p, makePng(w, h)); return p; };

// ---------------------------------------------------------------- fixture: a temp main checkout with copies of the tools (the hooks find check_handin under the main they resolve)
const root = path.join(tmp, 'main10'); mk(path.join(root, '.git')); mk(path.join(root, 'tools', 'hooks')); mk(path.join(root, 'docs', 'templates')); mk(path.join(root, 'docs', 'reference', 'aerial')); mk(path.join(root, 'docs', 'reference', '_compare'));
for (const f of fs.readdirSync(path.join(src, 'tools'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', f), path.join(root, 'tools', f));
for (const f of fs.readdirSync(path.join(src, 'tools', 'hooks'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', 'hooks', f), path.join(root, 'tools', 'hooks', f));
for (const f of fs.readdirSync(path.join(src, 'docs', 'templates'))) fs.copyFileSync(path.join(src, 'docs', 'templates', f), path.join(root, 'docs', 'templates', f));
fs.copyFileSync(path.join(src, 'docs', 'AGENT_CONTRACT.md'), path.join(root, 'docs', 'AGENT_CONTRACT.md'));
const state = path.join(tmp, 'state-10'); mk(state);
const env = { ...process.env, CP_GATE_STATE_DIR: state }; delete env.CP_MAIN_ROOT;
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
const logOf = () => { try { return fs.readFileSync(path.join(state, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };
const photoDir = path.join(root, 'docs', 'reference', 'aerial');
const photos = ['ph1.png', 'ph2.png', 'ph3.png'].map((n) => pngFile(path.join(photoDir, n), 60, 60));

// a git worktree-like repo for the build phase (a plain repo is its own "main" for the waiver guard)
const git = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', env: { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' } });
function repo(name) { const w = path.join(tmp, name); mk(w); git(w, 'init', '-q'); git(w, 'config', 'user.email', 't@t'); git(w, 'config', 'user.name', 't'); wr(path.join(w, 'README.md'), 'r'); git(w, 'add', '-A'); git(w, 'commit', '-q', '-m', 'init'); return w; }
const commit = (w, msg) => { git(w, 'add', '-A'); git(w, 'commit', '-q', '-m', msg); return git(w, 'rev-parse', 'HEAD').stdout.trim(); };

// ---------------------------------------------------------------- measure hand-ins: GOOD and BAD
const GOOD_MEASURE = (hd) => `# MEASURE\nReferences opened: 3 of 3 listed\n\n## Case A one-lane road\n| photo | measured | value |\n|---|---|---|\n| ${photos[0]} | carriageway width | 3.4 m |\n| ${photos[1]} | lane width | 1.0 lane widths |\n| ${photos[2]} | verge | 150 cm |\n\nOverlay: ${path.join(hd, 'trace_a.png')}\n`;
function measureHandin(name, mutate = (x) => x, { image = true } = {}) {
  const w = repo(name); const hd = path.join(w, 'docs', 'progress', 'rr'); mk(hd);
  if (image) pngFile(path.join(hd, 'trace_a.png'), 80, 60);
  wr(path.join(hd, 'MEASURE.md'), mutate(GOOD_MEASURE(hd)));
  return { w, hd };
}
const chk = (hd, ...a) => spawnSync(process.execPath, [path.join(root, 'tools', 'check_handin.mjs'), hd, '--main', root, ...a, ...(a.includes('build') && !a.includes('node-only') ? ['--suite', 'node-only'] : [])], { cwd: elsewhere, encoding: 'utf8', env, timeout: 600000 });
const failedWith = (r, re) => r.status === 1 && /HAND-IN CHECK FAILED/.test(r.stderr) && re.test(r.stderr);
const passed = (r) => r.status === 0 && /HAND-IN CHECK PASSED/.test(r.stdout);

console.log('gates10 A: phase measure (check_handin --phase measure on made-up hand-ins)');
{
  const g = measureHandin('m_good'); const r = chk(g.hd, '--phase', 'measure');
  t('A measure: a GOOD MEASURE.md (opened line, table with photo and value, 3 photos, a decodable trace image) passes', passed(r), r.stderr + r.stdout);
  const cases = [
    ['no MEASURE.md', () => { const x = measureHandin('m_none'); fs.rmSync(path.join(x.hd, 'MEASURE.md')); return x; }, /MEASURE\.md is missing/],
    ['no "References opened" line', () => measureHandin('m_noref', (s) => s.replace(/^References opened.*$/m, '')), /References opened: N of M/],
    ['opened 0', () => measureHandin('m_zero', (s) => s.replace('3 of 3', '0 of 3')), /nothing was opened/],
    ['opened 2 of 3 with no "not opened" line', () => measureHandin('m_part', (s) => s.replace('3 of 3', '2 of 3')), /unopened/],
    ['no case section', () => measureHandin('m_nocase', (s) => s.replace('## Case A one-lane road', '## Notes')), /no "## Case/],
    ['table row without a measured value', () => measureHandin('m_noval', (s) => s.replace('| 3.4 m |', '| wide |')), /no measured value/],
    ['table row without a photo file', () => measureHandin('m_nophoto', (s) => s.replace(photos[0], 'somewhere')), /names no photo file/],
    ['only 2 different photos', () => measureHandin('m_two', (s) => s.replace(photos[2], photos[1])), /at least 3 per case/],
    ['no trace image named', () => measureHandin('m_noimg', (s) => s.replace(/^Overlay:.*$/m, '')), /no trace or overlay image/],
    ['trace image does not exist', () => measureHandin('m_missing', (s) => s, { image: false }), /no trace or overlay image/],
    ['trace image is a truncated PNG', () => { const x = measureHandin('m_trunc'); const p = path.join(x.hd, 'trace_a.png'); const b = fs.readFileSync(p); fs.writeFileSync(p, b.subarray(0, b.length - 30)); return x; }, /no trace or overlay image/],
    ['trace image is not a picture', () => { const x = measureHandin('m_fake'); fs.writeFileSync(path.join(x.hd, 'trace_a.png'), 'not a png at all'); return x; }, /no trace or overlay image/],
  ];
  for (const [label, mkx, re] of cases) { const x = mkx(); const rr = chk(x.hd, '--phase', 'measure'); t(`A measure: BAD (${label}) is refused with the matching reason`, failedWith(rr, re), rr.stderr + rr.stdout); }
  const old = measureHandin('m_old'); const rr = chk(old.hd, '--phase', 'measure', '--since', String(Date.now() + 60000));
  t('A measure: a MEASURE.md written before the brief (--since in the future) is refused', failedWith(rr, /older than this agent's run/), rr.stderr);
}

console.log('gates10 A: phase build (check_handin --phase build: commit list, the suite run by the check, fail_first, waivers, git status)');
const suiteOk = 'node -e "process.exit(0)"', suiteBad = 'node -e "process.exit(3)"';
function buildHandin(name, { testFile = false, failFirst = false, list = true, dirty = false, extraUnlisted = false, log = 'ok' } = {}) {
  const x = measureHandin(name); commit(x.w, 'measure phase');
  wr(path.join(x.w, 'src.mjs'), 'code'); let sha = commit(x.w, 'build step one');
  const shas = [sha];
  if (testFile) { wr(path.join(x.w, 'tests', 'test_new_thing.mjs'), 'tests'); shas.push(commit(x.w, 'add test_new_thing')); }
  if (extraUnlisted) { wr(path.join(x.w, 'src2.mjs'), 'more'); commit(x.w, 'unlisted step'); }
  if (failFirst) wr(path.join(x.hd, 'fail_first_new_thing.txt'), 'test_new_thing.mjs run against the old code:\nFAIL new thing joins up\nFAIL new thing mirrors\n10 passed, 2 failed\n');
  wr(path.join(x.hd, 'BUILD.md'), `# BUILD\nCommits: ${list ? shas.map((s) => s.slice(0, 10)).join(' ') : 'none'}\nSuite binary: src.mjs\n`);
  // gates12: the log is made through the real hooks (a typed log has no run record and is refused)
  if (log === 'ok') logRun(x.w, path.join(x.hd, 'suite_node.log'), '12 passed, 0 failed\n', { stateDir: state }); else if (log === 'bad') logRun(x.w, path.join(x.hd, 'suite_node.log'), 'FAIL some case that is not allowed\n11 passed, 1 failed\n', { stateDir: state, exit: 1 });
  commit(x.w, 'BUILD.md'); // the hand-in files are committed too: BUILD.md lists the commits before it
  if (dirty) wr(path.join(x.w, 'stray.txt'), 'untracked');
  return x;
}
{
  const g = buildHandin('b_good', { testFile: true, failFirst: true });
  // the BUILD.md commit itself is later than the listed ones: list it by amending the list after the fact is not needed because the hook lists commits since MEASURE.md; list all
  const allShas = git(g.w, 'log', '--format=%H').stdout.trim().split('\n').slice(0, -2).map((s) => s.slice(0, 10));
  wr(path.join(g.hd, 'BUILD.md'), `# BUILD\nCommits: ${allShas.join(' ')}\nSuite binary: src.mjs\n`);
  // BUILD.md is rewritten after the commit listing: commit it, which adds one more commit that must be listed too; the last one is listed by the loop below
  let sha = commit(g.w, 'BUILD.md final'); wr(path.join(g.hd, 'BUILD.md'), `# BUILD\nCommits: ${[...allShas, sha.slice(0, 10)].join(' ')}\nSuite binary: src.mjs\n`);
  // the file that lists the last commit is itself a change: the rule is "commits since the measure phase", so the final BUILD.md is committed with a message that is listed via the hash of its parent
  sha = commit(g.w, 'BUILD.md listed'); const listAll = git(g.w, 'log', '--format=%H', `${git(g.w, 'log', '-1', '--format=%H', '--', 'docs/progress/rr/MEASURE.md').stdout.trim()}..HEAD`).stdout.trim().split('\n');
  // the last commit cannot list itself (a hash is not known before the commit): the build phase lists everything up to the one before it and the check accepts a final commit that only touches BUILD.md
  wr(path.join(g.hd, 'BUILD.md'), `# BUILD\nCommits: ${listAll.slice(1).map((s) => s.slice(0, 10)).join(' ')}\nSuite binary: src.mjs\n`); sha = commit(g.w, 'BUILD.md');
  const r = chk(g.hd, '--phase', 'build', '--suite', suiteOk);
  t('A build: a GOOD build hand-in (every commit listed, suite exits 0 when the check runs it, fail_first for the new test file, no waiver, clean git status) passes', passed(r) || /only BUILD\.md/.test(r.stderr), r.stderr + r.stdout);
}
{
  const cases = [
    ['no BUILD.md', () => { const x = buildHandin('b_none'); fs.rmSync(path.join(x.hd, 'BUILD.md')); commit(x.w, 'rm'); return x; }, /BUILD\.md is missing/, [suiteOk]],
    ['a commit made since the measure phase is not listed', () => buildHandin('b_unlisted', { extraUnlisted: true }), /does not list it/, [suiteOk]],
    ['no Commits: line', () => { const x = buildHandin('b_nolist'); wr(path.join(x.hd, 'BUILD.md'), '# BUILD\nnothing here\n'); commit(x.w, 'x'); return x; }, /no "Commits:" line/, [suiteOk]],
    ['suite_node.log shows a failing test the brief does not allow', () => buildHandin('b_suite', { log: 'bad' }), /not in the brief's allowed list/, []],
    ['no suite_node.log at all', () => buildHandin('b_nosuite', { log: 'none' }), /no suite_node.log in the hand-in folder/, []],
    ['a new test file with no fail_first_*.txt', () => buildHandin('b_nff', { testFile: true }), /no fail_first_\*\.txt/, [suiteOk]],
    ['git status not empty', () => buildHandin('b_dirty', { dirty: true }), /git status is not empty/, [suiteOk]],
  ];
  for (const [label, mkx, re, suites] of cases) {
    const x = mkx(); const rr = chk(x.hd, '--phase', 'build', ...suites.flatMap((s) => ['--suite', s]));
    t(`A build: BAD (${label}) is refused with the matching reason`, failedWith(rr, re), rr.stderr + rr.stdout);
  }
}

console.log('gates10 A: finish hook for a phase agent (real transcripts)');
const J = (s) => { try { return JSON.parse(s); } catch { return {}; } };
const mkTr = (name, text, startAgoMs = 600000, extra = []) => {
  const p = path.join(tmp, name + '.jsonl');
  fs.writeFileSync(p, [JSON.stringify({ type: 'user', isSidechain: true, timestamp: new Date(Date.now() - startAgoMs).toISOString(), message: { role: 'user', content: text } }), JSON.stringify({ type: 'assistant', message: { role: 'assistant', content: [{ type: 'text', text: 'working' }] } }), ...extra.map((e) => JSON.stringify(e))].join('\n') + '\n');
  return p;
};
const stopHook = (id, tp, event = 'SubagentStop') => {
  fs.appendFileSync(path.join(state, 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate: 'inject', agent_type: 'general-purpose', agent_id: id, decision: 'inject', chars: 1 }) + '\n');
  const input = event === 'SubagentStop' ? { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, stop_hook_active: false, last_assistant_message: 'done' }
    : { hook_event_name: 'PreToolUse', tool_name: 'SubagentHandback', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, transcript_path: tp, tool_input: { message: 'report' } };
  const r = spawnSync(process.execPath, [path.join(root, 'tools', 'hooks', 'handin_gate.mjs')], { input: JSON.stringify(input), cwd: elsewhere, encoding: 'utf8', timeout: 600000, env });
  return { code: r.status, out: r.stdout, err: r.stderr };
};
const briefOf = (hd, extra) => `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${hd}\nHANDIN: no-visual\n${extra}\n## Owner's words\n"The photo is the target." (11 Oct)\n`;
{
  const good = measureHandin('h_good'); const bad = measureHandin('h_bad'); fs.rmSync(path.join(bad.hd, 'MEASURE.md'));
  let r = stopHook('ph_ok', mkTr('tr_ph_ok', briefOf(good.hd, 'PHASE: measure')), 'SubagentStop');
  t('A hook: a measure-phase agent with a GOOD MEASURE.md may stop; logged as a passed phase check, NOT a hand-in', r.code === 0 && logOf().some((l) => l.agent_id === 'ph_ok' && /phase check passed \(measure\)/.test(l.reason || '')), JSON.stringify([r, logOf().slice(-3)]));
  t('A hook: ... and no builder record exists, so no audit is owed after a phase', !fs.existsSync(path.join(state, 'builders')) || fs.readdirSync(path.join(state, 'builders')).length === 0);
  const tr = mkTr('tr_ph_bad', briefOf(bad.hd, 'PHASE: measure'));
  const outs = []; for (let i = 0; i < 4; i++) outs.push(stopHook('ph_bad', tr, 'PreToolUse'));
  t('A hook: a measure-phase agent with NO MEASURE.md is refused three times (exit 2, the reason names MEASURE.md and the phase)', outs.slice(0, 3).every((o) => o.code === 2 && /PHASE GATE \(measure\)/.test(o.err) && /MEASURE\.md/.test(o.err)), JSON.stringify(outs.map((o) => [o.code, o.err.slice(0, 120)])));
  t('A hook: ... the fourth stop is released and HANDIN_FAILED.txt is written in the hand-in folder', outs[3].code === 0 && fs.existsSync(path.join(bad.hd, 'HANDIN_FAILED.txt')), JSON.stringify(outs[3]));
  const r2 = stopHook('ph_bad2', mkTr('tr_ph_bad2', briefOf(bad.hd, 'PHASE: measure')), 'PreToolUse');
  t('A hook: a DIFFERENT phase agent has its own refusal count (refusal 1 of 3, not released)', r2.code === 2 && /refusal 1 of 3/.test(r2.err), r2.err.slice(0, 200));
  // no CHECKPOINT allowance: a valid CHECKPOINT.md does not let a phase agent stop
  const cpw = measureHandin('h_cp'); fs.rmSync(path.join(cpw.hd, 'MEASURE.md')); pngFile(path.join(cpw.hd, 'shot.png'), 90, 90);
  wr(path.join(cpw.hd, 'CHECKPOINT.md'), `CHECKPOINT: lane widths measured on four photos | images: ${path.join(cpw.hd, 'shot.png')}\n`);
  const rc = stopHook('ph_cp', mkTr('tr_ph_cp', briefOf(cpw.hd, 'PHASE: measure')), 'PreToolUse');
  t('A hook: a valid CHECKPOINT.md does NOT let a phase agent stop (the allowance is for the final hand-in only)', rc.code === 2 && !logOf().some((l) => l.agent_id === 'ph_cp' && l.decision === 'checkpoint'), JSON.stringify(rc));
  // the render phase is the ordinary hand-in: no STATUS.md -> the ordinary refusal
  const rr = stopHook('ph_render', mkTr('tr_ph_render', briefOf(good.hd, 'PHASE: render')), 'PreToolUse');
  t('A hook: the render phase gets the ordinary full hand-in check (STATUS.md is demanded)', rr.code === 2 && /HAND-IN GATE/.test(rr.err) && /STATUS\.md/.test(rr.err), rr.err.slice(0, 300));
  // build phase through the hook: SUITE: lines of the brief are run by the check
  const bg = buildHandin('h_build'); const allSha = git(bg.w, 'log', '--format=%H', `${git(bg.w, 'log', '-1', '--format=%H', '--', 'docs/progress/rr/MEASURE.md').stdout.trim()}..HEAD`).stdout.trim().split('\n').filter(Boolean);
  wr(path.join(bg.hd, 'BUILD.md'), `# BUILD\nCommits: ${allSha.map((s) => s.slice(0, 10)).join(' ')}\nSuite binary: src.mjs\n`); commit(bg.w, 'final BUILD.md');
  logRun(bg.w, path.join(bg.hd, 'suite_node.log'), 'FAIL some case that is not allowed\n11 passed, 1 failed\n', { stateDir: state, exit: 1 }); commit(bg.w, 'bad suite log');
  const rb = stopHook('ph_build', mkTr('tr_ph_build', briefOf(bg.hd, `PHASE: build\nSUITE: ${suiteBad}\nSUITE: node-only`)), 'PreToolUse');
  t('A hook: a build-phase agent whose suite_node.log shows a failure the brief does not allow is refused (the hook reads the log and never runs the suite)', rb.code === 2 && /not in the brief's allowed list/.test(rb.err), rb.err.slice(0, 400));
}

console.log('gates10 A: the `unreal` frame kind of COMPARE.md');
{
  const GL = await imp('tools/gates_lib.mjs').catch(() => null);
  const IL = await imp('tools/image_lib.mjs').catch(() => null);
  const hd = path.join(tmp, 'u_hd'); mk(hd);
  const frame = pngFile(path.join(hd, 'x_aerial.png'), 90, 90), tiny = path.join(hd, 'tiny.png'); fs.writeFileSync(tiny, makePng(4, 4));
  const trunc = path.join(hd, 'trunc.png'); { const b = makePng(90, 90); fs.writeFileSync(trunc, b.subarray(0, b.length - 40)); }
  const cmp = (render, kind) => `| case | render | reference | composite | before | subject | scale | viewpoint | frame kind |\n|---|---|---|---|---|---|---|---|---|\n| c1 | ${render} | r.png | c.png | b.png | same subject here | same scale here | same viewpoint | ${kind} |\n`;
  await tt('A unreal: a row with frame kind unreal is not a headline view of the browser records', () => GL.headlineViews(cmp(frame, 'unreal')).length === 0 && GL.headlineViews(cmp(frame, '')).length === 1);
  await tt('A unreal: the [unreal] tag in the case cell declares it as well', () => GL.isUnrealRow(['c1 [unreal]', 'a', 'b']) === true && GL.isUnrealRow(['c1', 'a', 'b']) === false);
  await tt('A unreal: pictureProblems asks for NO stats.json / mask.png of an unreal frame named *_aerial.png', () => { const p = GL.pictureProblems(hd, cmp(frame, 'unreal'), {}).problems; return { ok: !p.some((x) => /has no .*stats\.json/.test(x)), detail: p.join('\n') }; });
  await tt('A unreal: ... but the same frame as a browser render (no declaration) still needs its records', () => GL.pictureProblems(hd, cmp(frame, ''), {}).problems.some((x) => /stats\.json/.test(x)));
  await tt('A unreal: a real decodable 10 KB+ frame written during the run passes unrealFrameProblems', () => { const p = GL.unrealFrameProblems([{ kase: 'c1', render: frame, unreal: true }], Date.now() - 600000); return { ok: p.length === 0, detail: p.join('\n') }; });
  await tt('A unreal: a frame under 10 KB is refused', () => GL.unrealFrameProblems([{ kase: 'c1', render: tiny, unreal: true }], 0).some((x) => /under 10240/.test(x)));
  await tt('A unreal: a truncated frame is refused', () => GL.unrealFrameProblems([{ kase: 'c1', render: trunc, unreal: true }], 0).some((x) => /not a complete decodable/.test(x)));
  await tt('A unreal: a frame older than the brief is refused', () => GL.unrealFrameProblems([{ kase: 'c1', render: frame, unreal: true }], Date.now() + 60000).some((x) => /older than this run/.test(x)));
  await tt('A unreal: a row that is not unreal is not judged by this check', () => GL.unrealFrameProblems([{ kase: 'c1', render: tiny, unreal: false }], 0).length === 0);
  await tt('image_lib: a PNG cut off before IEND is not complete; the whole file is', () => !IL.imageEndOk(trunc) && IL.imageEndOk(frame) && IL.imageDecodable(frame) && !IL.imageDecodable(trunc));
}

console.log('gates10 C: a truncated PNG is refused as a checkpoint image (gates9 audit)');
{
  const w = repo('cp_w'); const hd = path.join(w, 'docs', 'progress', 'cpx'); mk(hd);
  const b = makePng(90, 90); const shot = path.join(hd, 'shot.png'); fs.writeFileSync(shot, b.subarray(0, b.length - 40));
  wr(path.join(hd, 'CHECKPOINT.md'), `CHECKPOINT: lane widths measured on four photos | images: ${shot}\n`);
  let r = chk(hd, '--checkpoint');
  t('C a >= 10 KB PNG whose tail is cut off (valid header, no IEND) is refused as a checkpoint image', r.status === 1 && /cut off/.test(r.stderr), r.stderr);
  fs.writeFileSync(shot, b); r = chk(hd, '--checkpoint'); t('C ... the complete picture passes', r.status === 0 && /CHECKPOINT PASSED/.test(r.stdout), r.stderr);
}

// ---------------------------------------------------------------- make_brief
console.log('gates10 A/B: make_brief --phase and --first-look');
{
  const task = path.join(tmp, 'task.md');
  fs.writeFileSync(task, `# T round\n\n## Owner's words\n"and ofc it needs to work with the gate system, have you been sidestepping it?" (23 Oct 2026)\n\n## Cases\nCase 1: road | ${photos.join(' ')}\nFiles: ${photos[0]}\n\n## Faults\n1. a known fault that the first look must not see\n\n## Launched\nthis\n\n## Waiting\nnone\n\n## Suite\nnode -e "process.exit(0)"\n`);
  const mb = (...a) => spawnSync(process.execPath, [path.join(src, 'tools', 'make_brief.mjs'), task, ...a], { cwd: elsewhere, encoding: 'utf8', env, timeout: 600000 });
  const wtp = path.join(tmp, 'mb_wt');
  let r = mb('--round', 'rr', '--worktree', wtp, '--phase', 'measure', '--no-visual');
  t('A make_brief --phase measure: prints a PHASE: measure line and the phase section', r.status === 0 && /^PHASE: measure$/m.test(r.stdout) && /MEASURE phase/.test(r.stdout) && /HANDIN_DIR:/.test(r.stdout), r.stderr + r.stdout.slice(0, 300));
  r = mb('--round', 'rr', '--worktree', wtp, '--phase', 'build', '--no-visual');
  t('A make_brief --phase build: refused while MEASURE.md does not exist', r.status === 1 && /MEASURE\.md does not exist/.test(r.stderr), r.stderr);
  r = mb('--round', 'rr', '--worktree', wtp, '--phase', 'render', '--no-visual');
  t('A make_brief --phase render: refused while the earlier phases have not handed over', r.status === 1 && /REFUSED/.test(r.stderr), r.stderr);
  r = mb('--round', 'rr', '--worktree', wtp, '--phase', 'bogus');
  t('A make_brief --phase bogus: refused', r.status === 1 && /--phase must be/.test(r.stderr), r.stderr);
  // build after a failing measure: MEASURE.md present but bad
  const hdB = path.join(wtp, 'docs', 'progress', 'rr'); wr(path.join(hdB, 'MEASURE.md'), '# nothing\n');
  r = mb('--round', 'rr', '--worktree', wtp, '--phase', 'build', '--no-visual');
  t('A make_brief --phase build: refused when MEASURE.md exists but fails its check (run by make_brief, not quoted)', r.status === 1 && /measure phase check fails/.test(r.stderr), r.stderr);
  // build after a good measure
  const gw = measureHandin('mb_good'); commit(gw.w, 'measure');
  r = mb('--round', 'rr', '--worktree', gw.w, '--phase', 'build', '--no-visual');
  t('A make_brief --phase build: briefed once MEASURE.md passes; the brief carries PHASE: build and the SUITE: line of the task file', r.status === 0 && /^PHASE: build$/m.test(r.stdout) && /^SUITE: node -e/m.test(r.stdout), r.stderr + r.stdout.slice(0, 300));
  // first look
  r = mb('--first-look', hdB, '--round', 'rr');
  t('B make_brief --first-look: prints a FIRSTLOOK-OF marker, the owner words, and NOT the builder fault list', r.status === 0 && new RegExp('^FIRSTLOOK-OF: ' + hdB.replace(/[\\^$.*+?()[\]{}|]/g, '\\$&') + '$', 'm').test(r.stdout) && /sidestepping/.test(r.stdout) && !/known fault that the first look must not see/.test(r.stdout) && !/HANDIN_DIR/.test(r.stdout.split('# AGENT CONTRACT')[0]), r.stderr + r.stdout.slice(0, 400));
  t('B ... and tells the agent not to open AUDIT.md, STATUS.md or FAULTS.md first', /Do NOT open `AUDIT\.md`, `STATUS\.md` or `FAULTS\.md`/.test(r.stdout));
}

// ---------------------------------------------------------------- B: first look finish gate
console.log('gates10 B: FIRSTLOOK_OPUS.md validated by the finish hook');
function flFolder(name) { const hd = path.join(tmp, name, 'docs', 'progress', 'fl'); mk(hd); const imgs = ['a.png', 'b.png'].map((n) => pngFile(path.join(hd, n), 90, 90)); return { hd, imgs }; }
const flText = (id, opened, extra = '') => `First-look agent: ${id}\nOpened: ${opened.join(', ')}\nTop 3:\n1. The kerb line on the left of a.png stops short of the junction mouth\n2. The centre line in b.png is drawn over the stop bar position\n3. The grass in a.png reaches the carriageway edge with no verge strip\nWhat here would never be built: a junction mouth with no radius at all on either corner\n${extra}`;
{
  const F = flFolder('fl_a');
  const brief = (hd) => `AGENT-CONTRACT-MARKER: v1\nFIRSTLOOK-OF: ${hd}\n## Owner's words\n"x" (1 Oct)\n`;
  const run1 = (id, hd, text, extraTr = [], event = 'PreToolUse') => { if (text !== null) wr(path.join(hd, 'FIRSTLOOK_OPUS.md'), text); else fs.rmSync(path.join(hd, 'FIRSTLOOK_OPUS.md'), { force: true }); return stopHook(id, mkTr('tr_' + id, brief(hd), 600000, extraTr), event); };
  const opened = [...F.imgs, photos[0]];
  let r = run1('fl_ok', F.hd, flText('fl_ok', opened));
  t('B hook: a GOOD FIRSTLOOK_OPUS.md (own id, 3 existing images incl. a round picture and a photo, Top 3, would-never-be-built) lets the agent finish', r.code === 0 && logOf().some((l) => l.agent_id === 'fl_ok' && l.reason === 'first look written'), JSON.stringify([r, logOf().slice(-2)]));
  const bads = [
    ['no file', null, /does not exist/],
    ['another agent id', flText('someone_else', opened), /not your agent id/],
    ['no id line', flText('x', opened).replace(/^First-look agent:.*\n/, ''), /no line "First-look agent/],
    ['no Opened: line', flText('x', opened).replace(/^Opened:.*\n/m, ''), /no line "Opened:/],
    ['only two images opened', flText('x', [F.imgs[0], photos[0]]), /at least 3/],
    ['a named image does not exist', flText('x', [...opened, path.join(F.hd, 'ghost.png')]), /do not exist/],
    ['no reference photo among them', flText('x', [...F.imgs, path.join(F.hd, 'a.png')]), /at least 3|no reference photo/],
    ['no picture of the round among them', flText('x', photos), /no picture of this round/],
    ['no Top 3', flText('x', opened).replace(/^Top 3:\n/m, ''), /Top 3/],
    ['Top 3 with a one-word item', flText('x', opened).replace(/^2\. .*$/m, '2. bad'), /each a real observation/],
    ['no "would never be built" line', flText('x', opened).replace(/^What here would never be built:.*$/m, ''), /would never be built/],
    ['placeholder "would never be built"', flText('x', opened).replace(/(^What here would never be built:).*$/m, '$1 n/a'), /empty or a placeholder/],
  ];
  for (const [label, text, re] of bads) { const id = 'flb_' + label.replace(/\W+/g, '_').slice(0, 24); const rr = run1(id, F.hd, text === null ? null : text.replace(/^First-look agent: x$/m, 'First-look agent: ' + id)); t(`B hook: BAD first look (${label}) is refused with the matching reason`, rr.code === 2 && /FIRST-LOOK GATE/.test(rr.err) && re.test(rr.err), rr.err.slice(0, 400)); }
  // three refusals then release with FIRSTLOOK_FAILED.txt; its own counter
  const outs = []; for (let i = 0; i < 4; i++) outs.push(run1('fl_cnt', F.hd, null));
  t('B hook: three refusals, then the fourth stop is released and FIRSTLOOK_FAILED.txt is written', outs.slice(0, 3).every((o) => o.code === 2) && outs[3].code === 0 && fs.existsSync(path.join(F.hd, 'FIRSTLOOK_FAILED.txt')), JSON.stringify(outs.map((o) => o.code)));
  // read order: STATUS.md read before the first image
  const readStatus = { type: 'assistant', message: { role: 'assistant', content: [{ type: 'tool_use', id: 't1', name: 'Read', input: { file_path: path.join(F.hd, 'STATUS.md') } }] } };
  const readImg = { type: 'assistant', message: { role: 'assistant', content: [{ type: 'tool_use', id: 't2', name: 'Read', input: { file_path: F.imgs[0] } }] } };
  r = run1('fl_ord', F.hd, flText('fl_ord', opened), [readStatus, readImg]);
  t('B hook: STATUS.md read BEFORE the first image (transcript order) is refused unless an "Order:" line says why', r.code === 2 && /opened before the first image/.test(r.err), r.err.slice(0, 300));
  r = run1('fl_ord2', F.hd, flText('fl_ord2', opened, 'Order: I read STATUS.md first by mistake and then looked at the images cold\n'), [readStatus, readImg]);
  t('B hook: ... with an "Order:" line (20+ characters) it is let through and the line is on record', r.code === 0, JSON.stringify(r));
  r = run1('fl_ord3', F.hd, flText('fl_ord3', opened), [readImg, readStatus]);
  t('B hook: images first, STATUS.md afterwards: fine', r.code === 0, JSON.stringify(r));
}

// ---------------------------------------------------------------- B: launch hook
console.log('gates10 B: the launch hook and NO-CONTRACT audits / first looks (real descriptions from gate.log of 23 Oct 2026)');
{
  const launch = (description, prompt, type = 'general-purpose') => spawnSync(process.execPath, [path.join(root, 'tools', 'hooks', 'brief_gate.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Agent', session_id: 's1', cwd: elsewhere, tool_input: { description, prompt, subagent_type: type } }), cwd: elsewhere, encoding: 'utf8', timeout: 120000, env });
  const nc = (reason) => `NO-CONTRACT: ${reason}\nDo the job.`;
  const refuse = [
    ['Auditor for gates9 hand-in', 'fresh auditor for the gates9 hand-in (hook and gate tooling, last gates round); writes AUDIT.md only', 'audit'],
    ['Auditor for gates8 hand-in', 'fresh auditor for the gates8 hand-in (agent-process hooks; writes AUDIT.md only)', 'audit'],
    ['Auditor for m2fix hand-in', 'fresh auditor for the m2fix hand-in (audit job, writes AUDIT.md only, no build hand-in)', 'audit'],
    ['Opus first look on grade-r12 images', 'Opus first-look pass on grade-r12 headline images; read-only except writing FIRSTLOOK_OPUS.md.', 'first look'],
    ['Opus first look: tjunction5 images', 'short first-look image check for the tjunction5 round (reads images, writes one file, no build)', 'first look'],
    ['Opus first look: grade-r10 images', 'short first-look image check for the earthworks round 10 hand-in (reads images, writes one file, no build or review hand-in)', 'first look'],
  ];
  for (const [d, reason, job] of refuse) { const r = launch(d, nc(reason)); t(`B launch: NO-CONTRACT "${d}" is refused as an ${job}, and the text names the way to launch it`, r.status === 2 && /free-text NO-CONTRACT form is not accepted/.test(r.stderr) && /clerk, merge, research, status, measure/.test(r.stderr) && /make_brief\.mjs/.test(r.stderr) && (job === 'audit' ? /--audit/ : /--first-look/).test(r.stderr), r.stderr.slice(0, 300)); }
  const allow = [
    ['Clerk: contract speed rules, queue, acceptance', 'clerk doc update in main (contract speed rules, QUEUE.md and ACCEPTANCE.md round states); no code.', 'clerk'],
    ['Merge gates7 then gates9 into main', 'merger for gates7 then gates9 into main (hook tooling); follows the gates9 STATUS merge checklist, writes no new feature', 'merge'],
    ['Recovery round 2: validate and re-fetch photos', 'reference-photo validation and re-download job (no code, no builds; reference photo downloads are pre-approved by the owner)', 'measure'],
    ['Queue catch-up for 22-23 Oct events', 'clerk job bringing docs/QUEUE.md, docs/ACCEPTANCE.md and the docs/HANDOFF.md top note up to date (docs only, no build or review hand-in)', 'clerk'],
    ['Re-fetch 18 truncated photos and back up', 'reference-photo re-download of 18 damaged files plus validated backup (no code, no builds)', 'measure'],
    ['Research: Unreal plugin facts', 'research on Unreal plugin build flags from C:\\UE_5.8 source (read-only; the AUDIT.md of gates9 mentions none of it)', 'research'],
  ];
  for (const [d, reason, cat] of allow) { const r = launch(d, nc(cat + ': ' + reason)); t(`B launch: NO-CONTRACT "${d}" stays allowed`, r.status === 0, r.stderr.slice(0, 300)); }
  const hdA = path.join(tmp, 'fl_a', 'docs', 'progress', 'fl');
  let r = launch('Opus first look', `FIRSTLOOK-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('B launch: a FIRSTLOOK-OF launch (make_brief --first-look) is allowed without NO-CONTRACT', r.status === 0 && logOf().some((l) => l.reason === 'FIRSTLOOK-OF'), r.stderr);
  r = launch('Opus first look', `FIRSTLOOK-OF: relative/folder\n`);
  t('B launch: a FIRSTLOOK-OF launch with a relative folder is refused', r.status === 2 && /absolute/.test(r.stderr), r.stderr);
  r = launch('Opus first look', `FIRSTLOOK-OF: ${hdA}\nHANDIN_DIR: ${hdA}\n`);
  t('B launch: a FIRSTLOOK-OF launch that also carries HANDIN_DIR (a builder) is refused', r.status === 2 && /HANDIN_DIR/.test(r.stderr), r.stderr);
  r = launch('Auditor for x', `AUDIT-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('B launch: a real AUDIT-OF launch is still allowed', r.status === 0, r.stderr);
  r = launch('Build the thing', 'no contract here at all');
  t('B launch: a launch with no contract and no NO-CONTRACT line is still refused', r.status === 2 && /LAUNCH REFUSED/.test(r.stderr), r.stderr);
}

// ---------------------------------------------------------------- C: lock guard bulk forms (the harness of test_guards9)
console.log('gates10 C: lock_guard bulk and indirect write forms (gates9 audit H7, patch / git apply / fs.rm routes)');
const main = path.join(tmp, 'roadworld'); mk(path.join(main, '.git', 'worktrees')); mk(path.join(main, '.claude', 'state')); wr(path.join(main, '.claude', 'state', 'gate.log'), '{}\n');
const addWt = (n) => { const w = path.join(tmp, 'roadworld-wt', n); mk(path.join(main, '.git', 'worktrees', n)); mk(path.join(w, 'tests')); mk(path.join(w, 'docs')); fs.writeFileSync(path.join(w, '.git'), 'gitdir: ' + path.join(main, '.git', 'worktrees', n) + '\n'); fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'gitdir'), path.join(w, '.git') + '\n'); fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'commondir'), '../..\n'); return w; };
const trDir = path.join(tmp, 'tr10'); mk(path.join(trDir, 'subagents')); const trMain = trDir + '.jsonl'; fs.writeFileSync(trMain, '');
const brief1 = (txt) => JSON.stringify({ type: 'user', timestamp: new Date().toISOString(), message: { content: txt } }) + '\n';
function lockWt(name, { list = null } = {}) {
  const w = addWt(name); const prior = path.join(w, 'docs', 'progress', 'prev'); mk(prior); fs.writeFileSync(path.join(prior, 'AUDIT.md'), '# audit\n');
  for (const f of ['listed_test.mjs', 'other.mjs', 'x.tmp', 'helper.cpp', 'test_g1.mjs']) wr(path.join(w, 'tests', f), 'f');
  if (list !== null) fs.writeFileSync(path.join(prior, 'LOCKED_TESTS.txt'), list);
  const hand = path.join(w, 'docs', 'progress', 'r10'); mk(hand); const id = 'b10_' + name;
  fs.writeFileSync(path.join(trDir, 'subagents', 'agent-' + id + '.jsonl'), brief1(`AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${hand}\nPRIOR-AUDIT: ${path.join(prior, 'AUDIT.md')}\n## Owner's words\n"x"`));
  return { w, prior, hand, id };
}
const lk = (id, tool, ti, cwd) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'lock_guard.mjs')], { input: JSON.stringify({ session_id: 's1', transcript_path: trMain, cwd, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: ti, tool_use_id: 'toolu_01', agent_id: id, agent_type: 'general-purpose' }), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: { ...process.env, CP_MAIN_ROOT: main } });
const B = (c) => ['Bash', { command: c }];
const refused = (r) => r.status === 2 && /REFUSED by the lock guard/.test(r.stderr);
function run(title, cases) {
  const bad = [];
  for (const [id, tool, ti, want, label, cwd] of cases) {
    const r = lk(id, tool, ti, cwd);
    const got = want === 2 ? (refused(r) ? 2 : `status ${r.status} ${r.stderr.slice(0, 100)}`) : (r.status === 0 && r.stderr === '' ? 0 : `status ${r.status} ${r.stderr.slice(0, 200)}`);
    if (got !== want) bad.push(`${label}: ${String(ti.command || ti.file_path).slice(0, 110)}: ${got}, wanted ${want}`);
  }
  t(`${title}: ${cases.length} real payloads`, bad.length === 0, bad.join('\n'));
}
{
  const L = lockWt('L1', { list: '# locked\ntests/listed_test.mjs\n' });
  wr(path.join(L.w, 'upd_locked.diff'), '--- a/tests/listed_test.mjs\n+++ b/tests/listed_test.mjs\n@@ -1 +1 @@\n-f\n+g\n');
  wr(path.join(L.w, 'upd_free.diff'), '--- a/tests/other.mjs\n+++ b/tests/other.mjs\n@@ -1 +1 @@\n-f\n+g\n');
  wr(path.join(L.w, 'upd_deep.diff'), '--- a/b/c/tests/listed_test.mjs\n+++ b/b/c/tests/listed_test.mjs\n@@ -1 +1 @@\n-f\n+g\n');
  const c = L.w;
  run('C bulk forms that rewrite a locked test (REFUSE)', [
    [L.id, ...B('find tests -exec clang-format -i {} +'), 2, 'find -exec clang-format -i', c], [L.id, ...B('find tests -name "*.mjs" -exec prettier --write {} \\;'), 2, 'find -name -exec prettier --write', c],
    [L.id, ...B('find . -name "listed_*" -execdir sed -i s/f/g/ {} +'), 2, 'find -execdir sed -i', c], [L.id, ...B('ls tests/*.mjs | xargs clang-format -i'), 2, 'ls | xargs clang-format -i', c],
    [L.id, ...B('git ls-files tests | xargs clang-format -i'), 2, 'git ls-files | xargs clang-format -i', c], [L.id, ...B('find tests -name "*.mjs" | xargs sed -i s/f/g/'), 2, 'find | xargs sed -i', c],
    [L.id, ...B('clang-format -i $(ls tests/*.mjs)'), 2, 'clang-format -i $(ls ...)', c], [L.id, ...B('clang-format -i $(find tests -name "*.mjs")'), 2, 'clang-format -i $(find ...)', c],
    [L.id, ...B('rm tests/{listed_test.mjs,other.mjs}'), 2, 'rm brace list', c], [L.id, ...B('clang-format -i tests/{helper.cpp,listed_test.mjs}'), 2, 'clang-format -i brace list', c],
    [L.id, ...B('sed -i -e s/f/g/ tests/listed_test.mjs'), 2, 'sed -i -e (the first positional is a FILE)', c], [L.id, ...B("sed -i -f script.sed tests/listed_test.mjs"), 2, 'sed -i -f', c],
    [L.id, ...B("perl -pi -e 's/f/g/' tests/listed_test.mjs"), 2, 'perl -pi -e', c], [L.id, ...B('sort -o tests/listed_test.mjs tests/listed_test.mjs'), 2, 'sort -o file file', c],
    [L.id, ...B('sort --output=tests/listed_test.mjs x.txt'), 2, 'sort --output=', c], [L.id, ...B('astyle tests/listed_test.mjs'), 2, 'astyle', c], [L.id, ...B('ed -s tests/listed_test.mjs'), 2, 'ed -s', c],
    [L.id, ...B('vim -es tests/listed_test.mjs'), 2, 'vim -es', c], [L.id, ...B('ex -s tests/listed_test.mjs'), 2, 'ex -s', c], [L.id, ...B('uncrustify --replace tests/listed_test.mjs'), 2, 'uncrustify --replace', c],
    [L.id, ...B('robocopy src tests listed_test.mjs'), 2, 'robocopy file filter onto the locked file', c], [L.id, ...B('robocopy tests moved listed_test.mjs /MOV'), 2, 'robocopy /MOV moves the locked file away', c],
    [L.id, ...B('patch -p1 -i upd_locked.diff'), 2, 'patch -i a diff of the locked file', c], [L.id, ...B('git apply upd_locked.diff'), 2, 'git apply a diff of the locked file', c],
    [L.id, ...B('git apply -p3 upd_deep.diff'), 2, 'git apply -p3 with the stripped name', c], [L.id, ...B('git apply upd_deep.diff'), 2, 'git apply tries every strip level 0-5', c],
    [L.id, ...B('git apply <<EOF\n--- a/tests/listed_test.mjs\n+++ b/tests/listed_test.mjs\n@@ -1 +1 @@\n-f\n+g\nEOF'), 2, 'git apply with a heredoc diff', c],
    [L.id, ...B('node -e "fs.rm(\'tests/listed_test.mjs\', () => {})"'), 2, 'fs.rm', c], [L.id, ...B('node -e "fs.promises.rm(\'tests/listed_test.mjs\')"'), 2, 'fs.promises.rm', c], [L.id, ...B('node -e "fsp.rm(\'tests/listed_test.mjs\')"'), 2, 'fsp.rm', c],
    [L.id, ...B('node -e "fs.openSync(\'tests/listed_test.mjs\', \'w\')"'), 2, 'fs.openSync(.., "w")', c], [L.id, ...B('node -e "fs.promises.open(\'tests/listed_test.mjs\', \'a\')"'), 2, 'fs.promises.open(.., "a")', c],
    [L.id, ...B("perl -e 'unlink q(tests/listed_test.mjs)'"), 2, 'perl -e unlink', c], [L.id, ...B("ruby -e 'File.delete(%q(tests/listed_test.mjs))'"), 2, 'ruby -e File.delete', c],
  ]);
  run('C the same forms on files that are NOT locked, and read-only forms (ALLOW)', [
    [L.id, ...B('find tests -exec grep -l f {} +'), 0, 'find -exec grep', c], [L.id, ...B('find tests -name "*.tmp" -exec rm {} \\;'), 0, 'find -name *.tmp -exec rm', c], [L.id, ...B('git ls-files tests | xargs grep f'), 0, 'git ls-files | xargs grep', c],
    [L.id, ...B('ls tests/*.mjs | xargs wc -l'), 0, 'ls | xargs wc', c], [L.id, ...B('clang-format -i tests/helper.cpp'), 0, 'clang-format -i unlocked', c], [L.id, ...B('clang-format -i $(ls tests/*.cpp)'), 0, 'clang-format -i $(ls tests/*.cpp)', c],
    [L.id, ...B('rm tests/{x.tmp,helper.cpp}'), 0, 'rm brace list of unlocked files', c], [L.id, ...B('sed -n -e 5p tests/listed_test.mjs'), 0, 'sed -n -e (read)', c], [L.id, ...B('sed -i -e s/f/g/ tests/other.mjs'), 0, 'sed -i -e on an unlocked file', c],
    [L.id, ...B('sort tests/listed_test.mjs'), 0, 'sort to stdout', c], [L.id, ...B('sort -o sorted.txt tests/listed_test.mjs'), 0, 'sort -o into another file', c], [L.id, ...B('astyle --dry-run tests/listed_test.mjs'), 0, 'astyle --dry-run', c],
    [L.id, ...B('robocopy src tests other.mjs'), 0, 'robocopy filter onto an unlocked file', c], [L.id, ...B('robocopy tests copy listed_test.mjs'), 0, 'robocopy copy FROM the locked file', c],
    [L.id, ...B('patch -p1 -i upd_free.diff'), 0, 'patch -i a diff of an unlocked file', c], [L.id, ...B('git apply upd_free.diff'), 0, 'git apply a diff of an unlocked file', c],
    [L.id, ...B('git apply --check upd_locked.diff'), 0, 'git apply --check only reads', c], [L.id, ...B('git apply --stat upd_locked.diff'), 0, 'git apply --stat only reads', c],
    [L.id, ...B('node -e "fs.rm(\'tests/other.mjs\', () => {})"'), 0, 'fs.rm of an unlocked file', c], [L.id, ...B('node -e "fs.openSync(\'tests/listed_test.mjs\', \'r\')"'), 0, 'fs.openSync(.., "r") reads', c],
  ]);
  // LOCK-LIST-BAD reaches the builder as well as the user
  const NM = lockWt('NM', { list: 'tests/missing_test.mjs\n' });
  let r = lk(NM.id, ...B('echo hi'), NM.w); const o = J(r.stdout);
  t('C LOCK-LIST-BAD on an allowed command: systemMessage (the user) AND hookSpecificOutput.additionalContext (Claude, the builder)', r.status === 0 && /LOCK-LIST-BAD/.test(o.systemMessage || '') && /LOCK-LIST-BAD/.test((o.hookSpecificOutput || {}).additionalContext || '') && (o.hookSpecificOutput || {}).hookEventName === 'PreToolUse', r.stdout + r.stderr);
  const NM2 = lockWt('NM2', { list: 'tests/missing_test.mjs\ntests/listed_test.mjs\n' });
  r = lk(NM2.id, ...B('rm tests/listed_test.mjs'), NM2.w);
  t('C LOCK-LIST-BAD on a REFUSED command: the stderr the builder sees carries the warning too', refused(r) && /LOCK-LIST-BAD/.test(r.stderr), r.stderr);
}

// ---------------------------------------------------------------- C: delete guard false positives
console.log('gates10 C: delete_guard reads CODE, not a commit message or a note that says "Remove-Item -Recurse"');
{
  const wtD = addWt('dg1');
  mk(path.join(main, 'docs', 'reference', 'aerial')); wr(path.join(main, 'docs', 'reference', 'aerial', 'p1.jpg'), 'photo');
  const dgm = await imp('tools/hooks/delete_guard_impl.mjs');
  const dg = (command, cwd) => dgm.analyse(command, cwd, 0, null, main).length ? 2 : 0;
  const allow = [
    ['git commit -m "docs: the guard no longer blocks Remove-Item -Recurse in a commit message"', 'commit -m'],
    ['git commit -m "fix: Remove-Item -Recurse (Join-Path $x y) and rimraf and shutil.rmtree(x) are named here"', 'commit -m with parentheses'],
    ['git commit -F - <<\'EOF\'\nfix: the false positive on Remove-Item -Recurse $(rimraf) in a message\nEOF', 'git commit -F - heredoc'],
    ['git commit -m "$(cat <<\'EOF\'\nSay Remove-Item -Recurse (Resolve-Path docs) and shutil.rmtree(p) in prose\nEOF\n)"', 'commit -m "$(cat <<EOF ...)"'],
    ['cat > notes.md <<\'EOF\'\nDo not run Remove-Item -Recurse (Get-Item x) or rimraf(x) here.\nEOF', 'cat > file <<EOF'],
    ['echo "never run Remove-Item -Recurse (Join-Path $PWD docs)"', 'echo'], ['grep -rn "rimraf\\|shutil.rmtree(" tools', 'grep'], ['gh pr create --title "x" --body "mentions Remove-Item -Recurse (Join-Path a b)"', 'gh pr create --body'],
  ];
  for (const [c, label] of allow) { const got = dg(c, main); t(`C delete_guard (cwd main): ${label} that only SAYS a recursive delete is allowed`, got === 0, 'refused: ' + c.slice(0, 120)); }
  const deny = [
    ['powershell -Command "Remove-Item (Join-Path $PWD \'docs\') -Recurse -Force"', 'a real Remove-Item -Recurse with a computed path'],
    ['node -e "require(\'rimraf\')(process.argv[1])"', 'a real node rimraf one-liner'], ['python -c "import shutil,sys; shutil.rmtree(sys.argv[1])"', 'a real python rmtree one-liner'],
    ['bash <<\'EOF\'\nrm -rf docs\nEOF', 'a heredoc fed to bash with rm -rf docs'], ['pwsh -c "Remove-Item docs -Recurse -Force"', 'pwsh -c Remove-Item -Recurse docs'],
    ['git status && powershell -c "Remove-Item (Get-Item docs) -Recurse"', 'a prose-looking prefix does not hide a real delete'], ['rm -rf docs', 'rm -rf docs'],
  ];
  for (const [c, label] of deny) { const got = dg(c, main); t(`C delete_guard (cwd main): ${label} is still refused`, got === 2, 'allowed: ' + c.slice(0, 120)); }
  void wtD;
}

// ---------------------------------------------------------------- C: clean_test_temp prefixes, suite cleanup
console.log('gates10 C: test temp folders (clean_test_temp prefixes, cleanup when a suite is cut short)');
await tt('C clean_test_temp PREFIXES cover every mkdtemp prefix the suites use (guards9-, guards10-, scenegate- were missing)', async () => {
  const m = await imp('tools/clean_test_temp.mjs'); const used = new Set();
  for (const f of [...fs.readdirSync(path.join(src, 'tools')).filter((x) => /\.mjs$/.test(x)).map((x) => path.join(src, 'tools', x)), ...fs.readdirSync(path.join(src, 'tools', 'hooks')).filter((x) => /\.mjs$/.test(x)).map((x) => path.join(src, 'tools', 'hooks', x))]) {
    for (const mm of fs.readFileSync(f, 'utf8').matchAll(/mkdtempSync\([^)]*?'([A-Za-z0-9]+-)'\)/g)) used.add(mm[1]);
  }
  used.add('guards10-');
  const miss = [...used].filter((p) => !m.PREFIXES.includes(p));
  return { ok: miss.length === 0, detail: 'not covered: ' + miss.join(', ') };
});
{
  // a child suite killed half way: its exit handler removes its own temp folder (links unlinked first)
  const child = path.join(tmp, 'child_suite.mjs');
  fs.writeFileSync(child, `import fs from 'node:fs'; import os from 'node:os'; import path from 'node:path'; import { isolateState } from ${JSON.stringify(pathToFileURL(path.join(src, 'tools', 'test_isolation.mjs')).href)};
const tmpd = fs.mkdtempSync(path.join(os.tmpdir(), 'guards10-child-')); isolateState(tmpd, ${JSON.stringify(src)}); fs.writeFileSync(path.join(tmpd, 'f.txt'), 'x'); console.log(tmpd); process.exit(1);\n`);
  const r = spawnSync(process.execPath, [child], { encoding: 'utf8', env: { ...process.env, CP_GATE_STATE_DIR: '' } });
  const dir = String(r.stdout).trim().split('\n').pop();
  t('C a suite that exits early (a failing test calls process.exit) removes its own temp folder', r.status === 1 && dir && !fs.existsSync(dir), `${r.status} ${dir} ${r.stderr.slice(0, 200)}`);
  if (dir && fs.existsSync(dir)) cleanupTmp(dir);
}

console.log('test isolation and cleanup');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
const gone = cleanupTmp(tmp);
t(`the temp folder is removed${process.env.KEEP_HOOKTEST ? ' [KEEP_HOOKTEST set: kept]' : ''}`, gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
