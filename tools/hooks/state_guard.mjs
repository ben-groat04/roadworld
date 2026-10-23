#!/usr/bin/env node
// Stub: the logic is in state_guard_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('state_guard', './state_guard_impl.mjs');
