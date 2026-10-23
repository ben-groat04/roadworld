#!/usr/bin/env node
// Stub: the logic is in brief_gate_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('brief', './brief_gate_impl.mjs');
