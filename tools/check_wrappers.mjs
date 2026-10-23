#!/usr/bin/env node
// Wrapper gate: every sim_core/src/*.cpp needs a matching Unreal wrapper
//   unreal/CityPainter/Plugins/CityPainterCore/Source/CityPainterSim/Private/SimCore/Wrap_<name>.cpp
// because the plugin compiles sim_core through those files. A sim_core source with no wrapper compiles in the CMake suite (all tests green) and then
// breaks the Unreal editor build (roadtool3 merge, 22 Oct: road_heat.cpp, tool_hud.cpp, tool_tuning.cpp). Fast, node only, no Unreal.
// Usage: node tools/check_wrappers.mjs [<repo root>]    exit 1 and the list of missing wrappers; 0 when complete.
// check_handin.mjs runs it for any round whose branch adds a sim_core/src/*.cpp; mergers and auditors can run it standalone before merging.
// Orphans (a wrapper whose sim_core source is gone) are reported as warnings, they do not fail.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const WRAP_DIR = 'unreal/CityPainter/Plugins/CityPainterCore/Source/CityPainterSim/Private/SimCore';
export function wrapperProblems(root) {
  const src = path.join(root, 'sim_core', 'src'), wrap = path.join(root, WRAP_DIR);
  if (!fs.existsSync(src)) return { error: 'no sim_core/src in ' + root };
  if (!fs.existsSync(wrap)) return { error: 'no wrapper folder ' + WRAP_DIR + ' in ' + root };
  const names = fs.readdirSync(src).filter((n) => n.endsWith('.cpp')).map((n) => n.replace(/\.cpp$/, ''));
  const wraps = new Set(fs.readdirSync(wrap).filter((n) => /^Wrap_.+\.cpp$/.test(n)).map((n) => n.slice(5, -4)));
  return {
    missing: names.filter((n) => !wraps.has(n)).map((n) => `sim_core/src/${n}.cpp has no ${WRAP_DIR}/Wrap_${n}.cpp (the Unreal build fails; copy the pattern of an existing Wrap_*.cpp)`),
    orphans: [...wraps].filter((n) => !names.includes(n) && !/^clipper2_/.test(n)).map((n) => `Wrap_${n}.cpp has no sim_core/src/${n}.cpp`),
    checked: names.length,
  };
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = path.resolve(process.argv[2] || path.join(path.dirname(fileURLToPath(import.meta.url)), '..'));
  const r = wrapperProblems(root);
  if (r.error) { console.error('check_wrappers: ' + r.error); process.exit(2); }
  r.orphans.forEach((o) => console.error('warning: ' + o));
  if (r.missing.length) { console.error(`WRAPPER CHECK FAILED (${r.missing.length} of ${r.checked} sim_core sources):\n - ` + r.missing.join('\n - ')); process.exit(1); }
  console.log(`WRAPPER CHECK PASSED: ${r.checked} sim_core sources, all have a Wrap_<name>.cpp`);
}
