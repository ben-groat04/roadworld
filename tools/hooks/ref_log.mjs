#!/usr/bin/env node
// Stub: the logic is in ref_log_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('ref_log', './ref_log_impl.mjs');
