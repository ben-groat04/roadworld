#!/usr/bin/env node
// Stub: the logic is in gate_notice_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('notice', './gate_notice_impl.mjs');
