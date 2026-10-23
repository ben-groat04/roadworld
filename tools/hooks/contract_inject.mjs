#!/usr/bin/env node
// Stub: the logic is in contract_inject_impl.mjs; guard.mjs turns any load or runtime error into a logged FAIL-OPEN allow.
import { runGuarded } from './guard.mjs';
runGuarded('inject', './contract_inject_impl.mjs');
