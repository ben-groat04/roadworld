#!/usr/bin/env node
// Stub: the logic is in handin_gate_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('handin', './handin_gate_impl.mjs');
