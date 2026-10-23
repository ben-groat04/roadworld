#!/usr/bin/env node
// Stub: the logic is in git_guard_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('git_guard', './git_guard_impl.mjs');
