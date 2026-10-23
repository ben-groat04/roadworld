// PreToolUse hook (matcher Read|Write|Edit|NotebookEdit|Grep|Glob|Bash|PowerShell): a SUBAGENT (the event carries an agent_id) may not read the secret, write
// anything under the main checkout's .claude/state, or run the scripts that write or acknowledge gate records. The main session (no agent_id) is not limited.
//
// Why (gates3 audit fault 2): records of who built and who audited a round are only worth anything if the agents being judged cannot write them. They are written
// by the hooks (which receive the harness's own agent_id), never by an agent; this hook closes the agent's own route to them. The HMAC on a record is TAMPER
// EVIDENCE only; the trust comes from who writes it.
//
// WHAT IT JUDGES (main session decision 23 Oct 2026, after the gates4 audit found it blocking legitimate work): ACTUAL TARGETS, never words. A word in a commit
// message, an echo, a grep pattern or a heredoc body is text and is ignored. A path under .claude/state counts when it is a real argument or redirect target of a
// command (or the path of a file tool). Always allowed for every agent:
//   - .claude/state/unreal_lane.lock: create, write, read, remove (the single Unreal lane; the owner's allow rules exist for it), by any tool;
//   - .claude/state/gate.log: read by any tool (Read, Grep, Glob, cat | tail, Get-Content, Select-String ...); never written, moved or removed.
// Refused: any other path under .claude/state (the folder, records, ref_log, ack_secret), a wildcard into it, a bare ack_secret argument, a redirect into it, an
// actual run of tools/hooks/gate_ack.mjs or tools/ref_seed.mjs, and inline node/python code that names the record writers (recordAudit, signRecord ...) or the state.
//
// WHAT IT CANNOT SEE: a script file that touches the state indirectly (the command only says `node x.mjs`), a path built from pieces or variables, a state path
// with a space in it (a quoted word with whitespace is read as text), a tool other than the ones matched, the secret reached through a Grep rooted above
// .claude (ripgrep skips the ignored state folder; not verified for every option). It reads tool input text and nothing else. FAIL OPEN (logged) on any internal error.
import { log, readStdin, parseJson } from './lib.mjs';
import { parseCommand, verbOf } from './shell_lex.mjs';

const norm = (p) => String(p || '').replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
const ALLOWED_ANY = new Set(['unreal_lane.lock']);
const WRITE_VERBS = new Set(['rm', 'del', 'erase', 'rmdir', 'rd', 'remove-item', 'ri', 'mv', 'move', 'move-item', 'mi', 'cp', 'copy', 'copy-item', 'cpi', 'tee', 'tee-object', 'set-content', 'add-content', 'out-file', 'new-item', 'ni', 'touch', 'mkdir', 'md', 'sc', 'ac', 'clear-content', 'rename-item', 'rni', 'ren', 'truncate', 'dd', 'install', 'ln', 'set-itemproperty']);
const TEXT_VERBS = new Set(['echo', 'printf', 'write-host', 'write-output', 'write-error', 'write-warning', 'write-verbose', 'git', 'gh', 'cmd', 'set', 'export']);
const PATTERN_VERBS = new Set(['grep', 'egrep', 'fgrep', 'rg', 'ag', 'select-string', 'sls', 'sed', 'awk', 'gawk', 'findstr']);
const RUNNERS = new Set(['node', 'nodejs', 'bun', 'deno', 'npx', 'tsx', 'sh', 'bash', 'zsh', 'pwsh', 'powershell', 'python', 'python3', 'py']);
const INLINE = new Set(['node', 'nodejs', 'python', 'python3', 'py', 'deno', 'bun']);
const SHELLS = new Set(['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd']);
const WRITER_WORDS = /\b(?:recordAudit|recordBuilder|signRecord|verifyRecord|ack_secret|gate_ack|ref_seed|audit_lib)\b/;
const RUN_SCRIPT = /^(?:gate_ack|ref_seed)(?:\.mjs)?$/i;

// what a path says about the state folder: null (not under it) or the part after .claude/state/ ('' for the folder itself)
export function stateRemainder(p) {
  let n = norm(p);
  if (!n.includes('.claude/state')) return null;
  n = n.replace(/\/\.\//g, '/');
  const i = n.indexOf('.claude/state');
  const before = n.slice(0, i), after = n.slice(i + '.claude/state'.length);
  if (before && !/(^|\/|:)$/.test(before) && !/[=("']$/.test(before)) return null;       // x.claude/state is another name
  if (after && !after.startsWith('/')) return null;                                       // .claude/state-old
  const rest = after.replace(/^\/+/, '');
  const parts = rest.split('/'); const out = [];
  for (const q of parts) { if (q === '..') out.pop(); else if (q && q !== '.') out.push(q); }
  return out.join('/');
}

// one path of a tool or command: [] when fine, else a reason. `write` = the path is written, moved or removed.
function judgePath(raw, { write = false, what = 'path' } = {}) {
  const r = stateRemainder(raw);
  if (r === null) return /(^|[\\/])ack_secret$/i.test(String(raw)) && !write ? [`${what} ${raw}: the ack secret is never read by an agent`] : [];
  if (/[*?]/.test(r)) return [`${what} ${raw}: a wildcard into .claude/state (it could match the secret)`];
  if (ALLOWED_ANY.has(r)) return [];
  if (r === 'gate.log') return write ? [`${what} ${raw}: gate.log is written by the hooks only (reading it is allowed)`] : [];
  return [`${what} ${raw}: the gate records under .claude/state are written by the hooks only (allowed: reading gate.log, using unreal_lane.lock)`];
}

export function judgeCommand(cmd, depth = 0) {
  const reasons = [];
  if (depth > 4) return reasons;
  const { segments } = parseCommand(cmd);
  for (const seg of segments) {
    for (const rd of seg.redirects) reasons.push(...judgePath(rd.target, { write: true, what: 'a redirect into' }));
    if (!seg.words.length) continue;
    const { verb, index, args } = verbOf(seg.words);
    // nested shells and inline code
    if (SHELLS.has(verb)) {
      const k = args.findIndex((a) => /^(?:-c|-command|\/c|\/k|-encodedcommand)$/i.test(a.v));
      if (k >= 0 && args[k + 1]) reasons.push(...judgeCommand(args.slice(k + 1).map((a) => a.v).join(' '), depth + 1));
    }
    if (INLINE.has(verb)) {
      const k = args.findIndex((a) => /^(?:-e|--eval|-p|--print|-c)$/.test(a.v));
      if (k >= 0 && args[k + 1]) {
        const code = args[k + 1].v;
        if (WRITER_WORDS.test(code)) reasons.push(`inline ${verb} code that names the record writers or the secret (${(code.match(WRITER_WORDS) || [''])[0]})`);
        else { const m = code.match(/[^\s"'`]*\.claude[\\/]+state[^\s"'`]*/gi); for (const x of m || []) reasons.push(...judgePath(x, { write: true, what: 'inline code touching' })); }
      }
    }
    let list = args;
    if (TEXT_VERBS.has(verb)) list = [];
    else if (PATTERN_VERBS.has(verb)) {
      const out = []; let patternSeen = false;
      for (let a = 0; a < list.length; a++) {
        const v = list[a].v;
        if (/^-(?:e|f|pattern|regexp)$/i.test(v)) { a++; patternSeen = true; continue; }
        if (/^-/.test(v)) continue;
        if (!patternSeen) { patternSeen = true; continue; }
        out.push(list[a]);
      }
      list = out;
    }
    const writeVerb = WRITE_VERBS.has(verb) || (verb === 'sed' && args.some((a) => /^-i/.test(a.v)));
    let firstNonFlag = true;
    for (const a of list) {
      const v = a.v;
      if (/\s/.test(v) && !/^[A-Za-z]:[\\/]\S*$/.test(v)) continue;     // text with spaces: a message, a script body
      if (!/^-/.test(v) && RUN_SCRIPT.test(v.replace(/^.*[\\/]/, '')) && firstNonFlag && RUNNERS.has(verb) && !args.some((x) => /^(?:--check|-c)$/.test(x.v))) {
        reasons.push(`running ${v.replace(/^.*[\\/]/, '')}: an agent does not write or acknowledge gate records or the reference register`);
      }
      if (!/^-/.test(v)) firstNonFlag = false;
      const vv = /^-\w+:/.test(v) ? v.replace(/^-\w+:/, '') : v;   // -Path:x
      if (/^-/.test(vv)) continue;
      for (const piece of vv.split(',')) reasons.push(...judgePath(piece, { write: writeVerb }));
    }
    // the verb itself may be the script (./tools/ref_seed.mjs) or a path inside the state folder
    const vraw = seg.words[index] ? seg.words[index].v : '';
    if (RUN_SCRIPT.test(vraw.replace(/^.*[\\/]/, ''))) reasons.push(`running ${vraw.replace(/^.*[\\/]/, '')}: an agent does not write or acknowledge gate records or the reference register`);
    else if (vraw && !/\s/.test(vraw)) reasons.push(...judgePath(vraw, { write: true, what: 'the command' }));
  }
  return [...new Set(reasons)];
}

export function judge(input) {
  const tool = input.tool_name || '';
  const ti = input.tool_input || {};
  if (!input.agent_id) return [];   // the main session
  if (tool === 'Read') return judgePath(ti.file_path || ti.path || '', { what: 'Read of' });
  if (tool === 'Write' || tool === 'Edit' || tool === 'NotebookEdit') return judgePath(ti.file_path || ti.notebook_path || ti.path || '', { write: true, what: tool + ' on' });
  if (tool === 'Glob') return [];     // names only: never file content
  if (tool === 'Grep') {
    const reasons = [];
    const p = ti.path || '';
    const r = p ? stateRemainder(p) : null;
    if (r !== null) {
      const g = String(ti.glob || '');
      const ok = ALLOWED_ANY.has(r) || r === 'gate.log' || (r === '' && /^(?:\*\*\/)?(?:gate\.log|unreal_lane\.lock)$/.test(g));
      if (!ok) reasons.push(`Grep over ${p}: the state folder holds the ack secret (a Grep of gate.log or unreal_lane.lock is allowed)`);
    }
    if (ti.glob && /ack_secret/i.test(String(ti.glob))) reasons.push('Grep with a glob that selects the ack secret');
    return reasons;
  }
  if (/^(Bash|PowerShell)$/.test(tool)) return judgeCommand(String(ti.command || ''));
  return [];
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'state_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  const reasons = judge(input);
  if (!reasons.length) return 0;
  log({ gate: 'state_guard', decision: 'deny', reason: 'agent touched the gate state', agent_id: input.agent_id, tool: input.tool_name, reasons });
  process.stderr.write('REFUSED by the state guard (tools/hooks/state_guard.mjs):\n - ' + reasons.join('\n - ') + '\nAllowed: reading .claude/state/gate.log with any tool, and using .claude/state/unreal_lane.lock. If this blocks work you were told to do, say so in your report (BLOCKED line); do not work around it.\n');
  return 2;
}
