#!/usr/bin/env node
// Stub: the logic is in delete_guard_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('delete_guard', './delete_guard_impl.mjs');
