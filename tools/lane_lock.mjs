#!/usr/bin/env node
// The Unreal lane lock (gates14, main-session decision 4). One Unreal editor or plugin build at a time: the lock is the folder <state>/unreal_lane.lock holding owner.txt.
//   node tools/lane_lock.mjs take    --agent <your agent id> --round <round> --purpose "<what you run>"   makes the folder and writes owner.txt; fails (exit 1) while another agent holds it
//   node tools/lane_lock.mjs release --agent <your agent id>                                                removes the lock when you hold it (or when it has no readable owner)
//   node tools/lane_lock.mjs status                                                                          prints the holder, or "free"
// owner.txt: "agent: <id>", "round: ...", "time: <ISO>", "purpose: ...". The id is the one you pass with --agent. state_guard (PreToolUse) checks that the id on the command line is the caller's own
// agent id and refuses every other write into the lock folder (mkdir, redirect, tee, Set-Content, Out-File, cp, mv, sed -i, the Write and Edit tools), so an owner.txt naming another agent can only come
// from this tool run as that agent. The main session (no agent id) passes --agent main. CP_GATE_STATE_DIR redirects the state folder (tests).
import fs from 'node:fs';
import path from 'node:path';
import { STATE_DIR } from './hooks/lib.mjs';

const LOCK = path.join(STATE_DIR, 'unreal_lane.lock');
const argv = process.argv.slice(2);
const sub = argv.find((a) => !a.startsWith('--')) || '';
const opt = (n) => { const i = argv.findIndex((a) => a === n || a.startsWith(n + '=')); if (i < 0) return null; return argv[i].includes('=') ? argv[i].slice(argv[i].indexOf('=') + 1) : (argv[i + 1] ?? null); };
const die = (m, code = 1) => { console.error('lane_lock: ' + m); process.exit(code); };
export const ownerOf = (dir) => { try { const m = fs.readFileSync(path.join(dir, 'owner.txt'), 'utf8').match(/^[ \t]*agent(?:[ _-]?id)?[ \t]*[:=][ \t]*([\w.-]+)/im); return m ? m[1] : null; } catch { return null; } };
const agent = opt('--agent');
const okId = (a) => typeof a === 'string' && /^[\w.-]+$/.test(a);

if (sub === 'status') {
  if (!fs.existsSync(LOCK)) { console.log('free'); process.exit(0); }
  let txt = ''; try { txt = fs.readFileSync(path.join(LOCK, 'owner.txt'), 'utf8').trim(); } catch { /* no owner file */ }
  console.log(txt ? 'held:\n' + txt : 'held, but owner.txt is missing or unreadable (anyone may release it)');
  process.exit(0);
}
if (sub === 'take') {
  if (!okId(agent)) die('--agent <your agent id> is required (letters, digits, . _ -)', 2);
  const round = opt('--round'), purpose = opt('--purpose');
  if (!round || !purpose) die('--round <round> and --purpose "<text>" are required', 2);
  fs.mkdirSync(path.dirname(LOCK), { recursive: true });
  try { fs.mkdirSync(LOCK); } catch (e) {
    if (e && e.code === 'EEXIST') { const o = ownerOf(LOCK); die(`the Unreal lane is held${o ? ' by agent ' + o : ' (the lock has no readable owner.txt: release it with --agent <id> if it is a crashed run\'s leftover)'}. Wait and retry in 60 s.`); }
    die('cannot create the lock: ' + e.message);
  }
  fs.writeFileSync(path.join(LOCK, 'owner.txt'), `agent: ${agent}\nround: ${round}\ntime: ${new Date().toISOString()}\npurpose: ${purpose}\n`);
  console.log(`lane_lock: taken by ${agent} (${round})`);
  process.exit(0);
}
if (sub === 'release') {
  if (!okId(agent)) die('--agent <your agent id> is required', 2);
  if (!fs.existsSync(LOCK)) { console.log('lane_lock: not held'); process.exit(0); }
  const o = ownerOf(LOCK);
  if (o && o !== agent && agent !== 'main') die(`the lock is held by agent ${o}, not ${agent}: only the holder releases it`);
  fs.rmSync(LOCK, { recursive: true, force: true });
  console.log('lane_lock: released');
  process.exit(0);
}
die('usage: node tools/lane_lock.mjs take --agent <id> --round <round> --purpose "<text>" | release --agent <id> | status', 2);
