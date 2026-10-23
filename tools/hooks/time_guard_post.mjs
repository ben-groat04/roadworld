#!/usr/bin/env node
// Stub: the logic is in time_guard_post_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('time_guard_post', './time_guard_post_impl.mjs');
