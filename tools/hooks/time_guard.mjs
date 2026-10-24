#!/usr/bin/env node
// Stub: the logic is in time_guard_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('time_guard', './time_guard_impl.mjs');
