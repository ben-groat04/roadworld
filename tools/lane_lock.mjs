#!/usr/bin/env node
// The Unreal lane lock (gates14 decision 4, rebuilt gates15 ruling 4). One Unreal editor or plugin build at a time.
//   node tools/lane_lock.mjs take    --agent <your agent id> --round <round> --purpose "<what you run>"   takes the lane; fails (exit 1) while another agent holds it
//   node tools/lane_lock.mjs release --agent <your agent id>                                                releases it when you hold it (`--agent main` releases any holder: the main session)
//   node tools/lane_lock.mjs status                                                                          prints the holder, or "free"
// WHO HOLDS THE LANE is decided by gate.log, not by a file an agent could write: take and release append a `{gate:"lane", decision:"take"|"release", agent, id, ...}` line (through the hook logger, the
// one writer state_guard leaves to the hooks), and the holder is the fold of those lines in order (lib.mjs laneFold: a take while held is ignored, a release counts from the holder or `main`).
// The folder <state>/unreal_lane.lock and its owner.txt are a HUMAN HINT only: `status` and every check ignore them, so a hand-written owner.txt naming another agent changes nothing.
// state_guard (PreToolUse) still checks that `--agent` is the caller's own agent id and refuses preloads of this tool. The tool itself refuses to run under NODE_OPTIONS or a node --import / --require
// flag (a preload could rewrite argv). CP_GATE_STATE_DIR redirects the state folder (tests).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { STATE_DIR, log, laneHolder } from './hooks/lib.mjs';

const LOCK = path.join(STATE_DIR, 'unreal_lane.lock');
const argv = process.argv.slice(2);
const die = (m, code = 1) => { console.error('lane_lock: ' + m); process.exit(code); };
const PRELOAD = /(?:^|\s)(?:--import|--require|--loader|--experimental-loader|-r)(?:[=\s]|$)/;
if (process.env.NODE_OPTIONS || process.env.NODE_PATH || process.execArgv.some((a) => PRELOAD.test(a + ' '))) die('refusing to run under NODE_OPTIONS, NODE_PATH or a node --import / --require preload (a preload could rewrite the arguments and take the lane as another agent); run `node tools/lane_lock.mjs ...` plainly', 2);
const sub = argv.find((a) => !a.startsWith('--')) || '';
const opt = (n) => { const i = argv.findIndex((a) => a === n || a.startsWith(n + '=')); if (i < 0) return null; return argv[i].includes('=') ? argv[i].slice(argv[i].indexOf('=') + 1) : (argv[i + 1] ?? null); };
// the hint file's agent (kept for the human reader and for tests; never trusted)
export const ownerOf = (dir) => { try { const m = fs.readFileSync(path.join(dir, 'owner.txt'), 'utf8').match(/^[ \t]*agent(?:[ _-]?id)?[ \t]*[:=][ \t]*([\w.-]+)/im); return m ? m[1] : null; } catch { return null; } };
const agent = opt('--agent');
const okId = (a) => typeof a === 'string' && /^[\w.-]+$/.test(a);
const fmt = (h) => `agent: ${h.agent}\nround: ${h.round}\ntime: ${h.time}\npurpose: ${h.purpose}`;
const dropFolder = () => { try { fs.rmSync(LOCK, { recursive: true, force: true }); } catch { /* the folder is only a hint */ } };

if (sub === 'status') {
  const h = laneHolder();
  if (!h) { console.log(fs.existsSync(LOCK) ? 'free (a leftover lock folder exists but gate.log says nobody holds the lane)' : 'free'); process.exit(0); }
  const hint = ownerOf(LOCK);
  console.log('held (by gate.log):\n' + fmt(h) + (hint && hint !== h.agent ? `\n(note: owner.txt says ${hint}; it is a hint and is ignored)` : ''));
  process.exit(0);
}
if (sub === 'take') {
  if (!okId(agent)) die('--agent <your agent id> is required (letters, digits, . _ -)', 2);
  const round = opt('--round'), purpose = opt('--purpose');
  if (!round || !purpose) die('--round <round> and --purpose "<text>" are required', 2);
  const h0 = laneHolder();
  if (h0 && h0.agent === agent) { console.log(`lane_lock: already held by ${agent}`); process.exit(0); }
  if (h0) die(`the Unreal lane is held by agent ${h0.agent} (round ${h0.round}, since ${h0.time}). Wait and retry in 60 s.`);
  const id = crypto.randomBytes(8).toString('hex');
  log({ gate: 'lane', decision: 'take', agent, id, round, purpose });
  const h1 = laneHolder();
  if (!h1 || h1.id !== id) die(`lost the race: the Unreal lane went to agent ${h1 ? h1.agent : '?'} at the same moment. Wait and retry in 60 s.`);
  try { fs.mkdirSync(LOCK, { recursive: true }); fs.writeFileSync(path.join(LOCK, 'owner.txt'), fmt(h1) + '\n'); } catch { /* the folder is only a hint */ }
  console.log(`lane_lock: taken by ${agent} (${round})`);
  process.exit(0);
}
if (sub === 'release') {
  if (!okId(agent)) die('--agent <your agent id> is required', 2);
  const h = laneHolder();
  if (h && h.agent !== agent && agent !== 'main') die(`the lane is held by agent ${h.agent}, not ${agent}: only the holder releases it (the main session passes --agent main)`);
  if (h) log({ gate: 'lane', decision: 'release', agent, round: h.round });
  const had = fs.existsSync(LOCK);
  dropFolder();
  console.log(h || had ? 'lane_lock: released' : 'lane_lock: not held');
  process.exit(0);
}
die('usage: node tools/lane_lock.mjs take --agent <id> --round <round> --purpose "<text>" | release --agent <id> | status', 2);
