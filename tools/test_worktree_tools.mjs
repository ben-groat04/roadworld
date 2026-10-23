#!/usr/bin/env node
// Tests for tools/remove_agent_worktree.ps1 on a scratch git repository: every worktree has docs\reference, a directory junction to the private photos of the main
// checkout; the script must unlink it WITHOUT touching the target, check the photos are still there, and only then run git worktree remove. Windows only (junctions, powershell).
// Usage: node tools/test_worktree_tools.mjs   (exit 1 on any failure). The delete-guard hook tests are in tools/hooks/test_hooks.mjs.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
let pass = 0, fail = 0; const failed = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failed.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ').slice(0, 900) : '')); } };
if (process.platform !== 'win32') { console.log('skipped: junctions and powershell need Windows'); process.exit(0); }
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'wttools-'));
const git = (cwd, ...a) => { const r = spawnSync('git', a, { cwd, encoding: 'utf8' }); if (r.status !== 0) throw new Error('git ' + a.join(' ') + ': ' + r.stderr); return r.stdout.trim(); };
const script = path.join(src, 'tools', 'remove_agent_worktree.ps1');
const rm = (args, cwd = tmp) => spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, ...args], { cwd, encoding: 'utf8', timeout: 120000 });
const count = (d) => { let n = 0; const walk = (p) => { for (const e of fs.readdirSync(p, { withFileTypes: true })) { if (e.isDirectory()) walk(path.join(p, e.name)); else n++; } }; walk(d); return n; };

// scratch repo: <tmp>/roadworld (main), <tmp>/roadworld-wt/<name> (worktrees)
const main = path.join(tmp, 'roadworld'), wtRoot = path.join(tmp, 'roadworld-wt');
fs.mkdirSync(main); fs.mkdirSync(wtRoot);
git(main, 'init', '-q', '-b', 'trunk'); git(main, 'config', 'user.email', 't@t'); git(main, 'config', 'user.name', 't');
fs.writeFileSync(path.join(main, '.gitignore'), 'docs/reference/\n'); fs.writeFileSync(path.join(main, 'a.txt'), 'a\n'); fs.writeFileSync(path.join(main, 'CLAUDE.md'), 'rules\n');
git(main, 'add', '-A'); git(main, 'commit', '-qm', 'init');
const photos = path.join(main, 'docs', 'reference'); fs.mkdirSync(path.join(photos, 'aerial'), { recursive: true }); fs.mkdirSync(path.join(photos, 'street'), { recursive: true });
for (const f of ['aerial/a.jpg', 'aerial/b.jpg', 'street/c.jpg']) fs.writeFileSync(path.join(photos, f), 'photo ' + f);
const mkWt = (name) => {
  const wt = path.join(wtRoot, name);
  git(main, 'worktree', 'add', '-q', wt, '-b', 'agent/' + name);
  fs.mkdirSync(path.join(wt, 'docs'), { recursive: true });
  fs.symlinkSync(photos, path.join(wt, 'docs', 'reference'), 'junction');
  return wt;
};

console.log('remove_agent_worktree.ps1');
let wt = mkWt('foo');
t('fixture: the junction in the worktree reaches the main checkout photos', count(path.join(wt, 'docs', 'reference')) === 3);
let r = rm(['-Main', main, 'foo']);
t('a clean worktree with the docs\\reference junction: removed (exit 0)', r.status === 0 && !fs.existsSync(wt), r.stdout + r.stderr);
t('...and the photos in the main checkout are all still there (3 files)', count(photos) === 3 && fs.readFileSync(path.join(photos, 'street', 'c.jpg'), 'utf8') === 'photo street/c.jpg', r.stdout);
t('...the output says the junction was unlinked as a link and the count was checked', /link only/.test(r.stdout) && /still has 3 file\(s\)/.test(r.stdout), r.stdout);
t('...the branch is kept without -DeleteBranch', git(main, 'branch', '--list', 'agent/foo').includes('agent/foo'));

wt = mkWt('bar'); const extra = path.join(tmp, 'otherphotos'); fs.mkdirSync(extra); fs.writeFileSync(path.join(extra, 'x.jpg'), 'x');
fs.mkdirSync(path.join(wt, 'docs', 'progress', 'r'), { recursive: true }); fs.symlinkSync(extra, path.join(wt, 'docs', 'progress', 'r', 'ref2'), 'junction');
r = rm(['-Main', main, path.join('roadworld-wt', 'bar').replace('roadworld-wt', wtRoot), '-DeleteBranch']);
t('a worktree given by full path, with a SECOND junction elsewhere inside it: removed, both targets survive', r.status === 0 && !fs.existsSync(wt) && count(photos) === 3 && fs.existsSync(path.join(extra, 'x.jpg')), r.stdout + r.stderr);
t('-DeleteBranch removes a merged branch (git branch -d)', !git(main, 'branch', '--list', 'agent/bar').includes('agent/bar'), git(main, 'branch', '-a'));

wt = mkWt('dirty'); fs.writeFileSync(path.join(wt, 'uncommitted.txt'), 'work in progress\n');
r = rm(['-Main', main, 'dirty']);
t('a worktree with uncommitted files: REFUSED by git worktree remove (no --force), exit 1, files untouched', r.status === 1 && fs.existsSync(path.join(wt, 'uncommitted.txt')) && /refused/i.test(r.stdout), r.stdout + r.stderr);
t('...but the junction was already unlinked and the photos are intact', !fs.existsSync(path.join(wt, 'docs', 'reference')) && count(photos) === 3, r.stdout);
r = rm(['-Main', main, '-Force', 'dirty']);
t('-Force removes it after the links are gone, photos intact', r.status === 0 && !fs.existsSync(wt) && count(photos) === 3, r.stdout + r.stderr);

r = rm(['-Main', main, 'nosuch']); t('a name that is not a worktree folder: refused', r.status === 1 && /no such worktree folder/.test(r.stdout), r.stdout);
const stray = path.join(wtRoot, 'stray'); fs.mkdirSync(stray); fs.writeFileSync(path.join(stray, 'f.txt'), 'x');
r = rm(['-Main', main, 'stray']); t('a folder under roadworld-wt that git does not know as a worktree: refused, left alone', r.status === 1 && /not a registered worktree/.test(r.stdout) && fs.existsSync(path.join(stray, 'f.txt')), r.stdout);
r = rm(['-Main', main, main]); t('the main checkout itself: refused', r.status === 1 && /main checkout/.test(r.stdout) && fs.existsSync(path.join(photos, 'aerial', 'a.jpg')), r.stdout);
const odd = path.join(tmp, 'elsewhere-wt'); git(main, 'worktree', 'add', '-q', odd, '-b', 'agent/odd');
r = rm(['-Main', main, odd]); t('a registered worktree that is not under roadworld-wt: refused (not an agent worktree path)', r.status === 1 && /not under/.test(r.stdout) && fs.existsSync(odd), r.stdout);
spawnSync('git', ['worktree', 'remove', '--force', odd], { cwd: main });
t('after every case above the photos are still 3 files', count(photos) === 3);
const ps = fs.readFileSync(script, 'utf8');
t('the script never uses a recursive delete itself (no Remove-Item -Recurse, rm -r, rmdir /s)', !/Remove-Item[^\n]*-Recurse|\brm\s+-r|rmdir\s+\/s/i.test(ps.replace(/^#.*$/gm, '')));
const nw = fs.readFileSync(path.join(src, 'tools', 'new_agent_worktree.ps1'), 'utf8');
t('new_agent_worktree.ps1 tells the agent how to remove the worktree (the script, never a recursive delete)', /remove_agent_worktree\.ps1/.test(nw) && /Never recursive-delete/.test(nw));
t('new_agent_worktree.ps1 no longer recursive-deletes anything', !/Remove-Item[^\n]*-Recurse/.test(nw.replace(/^#.*$/gm, '').replace(/"[^"]*"/g, '')));

console.log('new_agent_worktree.ps1 (no junction) and unlink_reference_junctions.ps1');
{
  const nwPath = path.join(src, 'tools', 'new_agent_worktree.ps1');
  const nr = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', nwPath, 'fresh'], { cwd: main, encoding: 'utf8', timeout: 120000 });
  const fw = path.join(wtRoot, 'fresh');
  t('new_agent_worktree.ps1 in the scratch repo: worktree created (exit 0)', nr.status === 0 && fs.existsSync(path.join(fw, 'a.txt')), nr.stdout + nr.stderr);
  t('...it has NO docs\\reference link or folder (main session decision: nothing in a worktree leads to the photos)', !fs.existsSync(path.join(fw, 'docs', 'reference')), nr.stdout);
  t('...and the output names the photos by absolute path in the main checkout', nr.stdout.includes(photos), nr.stdout);
  const un = path.join(src, 'tools', 'unlink_reference_junctions.ps1');
  const ul = (args) => spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', un, '-Main', main, ...args], { cwd: tmp, encoding: 'utf8', timeout: 120000 });
  const w1 = mkWt('old1'), w2 = mkWt('old2'); const keep = path.join(tmp, 'keepme'); fs.mkdirSync(keep); fs.writeFileSync(path.join(keep, 'k.txt'), 'k');
  fs.mkdirSync(path.join(w2, 'build'), { recursive: true }); fs.symlinkSync(keep, path.join(w2, 'build', 'other'), 'junction');
  let u = ul(['-WhatIfOnly', '-All']);
  t('unlink script -WhatIfOnly -All: lists the two reference links and removes nothing', u.status === 0 && /would unlink/.test(u.stdout) && fs.existsSync(path.join(w1, 'docs', 'reference')) && fs.existsSync(path.join(w2, 'docs', 'reference')), u.stdout + u.stderr);
  u = ul(['-Name', 'old1']);
  t('-Name old1: its docs\\reference link is removed (link only), the other worktree is untouched, the photos are all there', u.status === 0 && !fs.existsSync(path.join(w1, 'docs', 'reference')) && fs.existsSync(path.join(w2, 'docs', 'reference')) && count(photos) === 3 && /still has 3 file/.test(u.stdout), u.stdout + u.stderr);
  u = ul(['-All']);
  t('-All: the second reference link goes, a link to somewhere else (build\\other) is LEFT ALONE and its target survives', u.status === 0 && !fs.existsSync(path.join(w2, 'docs', 'reference')) && fs.existsSync(path.join(w2, 'build', 'other', 'k.txt')) && /left alone/.test(u.stdout) && count(photos) === 3, u.stdout + u.stderr);
  u = ul([]); t('no -Name and no -All: refused', u.status === 1 && /REFUSED/.test(u.stdout), u.stdout);
  u = ul(['-Name', 'nosuch']); t('an unknown worktree name: refused', u.status === 1 && /no registered worktree/.test(u.stdout), u.stdout);
  const us = fs.readFileSync(un, 'utf8').replace(/^#.*$/gm, '');
  t('the script never uses a recursive delete (link-only cmd /c rmdir)', !/Remove-Item[^\n]*-Recurse|\brm\s+-r|rmdir\s+\/s/i.test(us) && /cmd\.exe \/c rmdir/.test(us));
  fs.rmdirSync(path.join(w2, 'build', 'other'));
}

console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
if (failed.length) console.log('FAILED TESTS:\n - ' + failed.join('\n - '));
// clean up: unlink anything link-like first, never a recursive delete through a junction
try { for (const d of fs.readdirSync(wtRoot)) { const w = path.join(wtRoot, d); try { fs.rmdirSync(path.join(w, 'docs', 'reference')); } catch { /* not a link */ } } } catch { /* none */ }
if (!process.env.KEEP_WTTEST) { try { fs.rmSync(tmp, { recursive: true, force: true }); } catch { /* temp */ } }
process.exit(fail ? 1 : 0);
