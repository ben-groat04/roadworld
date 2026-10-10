// config.js: imports, region/road-type data, tunables T and constants.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

// ===== Config (data-driven; UK values are just the first region pack) =====
const DRIVING_SIDE = 'left';
const WORLD = 2048;
const ROAD_TYPES = {
  1: { id: 'residential', name: 'Residential street', carriageway: 10.0, total: 14.3, lanes: 1, speedKph: 50,  minR: 20,   maxGrade: 10, kc: 10,  ks: 9,  dual: false, edge: '#8f8d86' },
  2: { id: 'b_road',      name: 'B road / main road', carriageway: 6.5,  total: 10.8, lanes: 1, speedKph: 70,  minR: 180,  maxGrade: 8,  kc: 30,  ks: 20, dual: false, edge: '#8f8d86' },
  3: { id: 'a_road_dual', name: 'A road / dual c\'way', carriageway: 8.8, total: 10.8, lanes: 2, speedKph: 100, minR: 720,  maxGrade: 6,  kc: 100, ks: 26, dual: true, gap: 2.5, edge: '#5f7d4a' },
  4: { id: 'motorway',    name: 'Motorway',           carriageway: 14.95, total: 17.45, lanes: 3, speedKph: 120, minR: 1020, maxGrade: 4, kc: 182, ks: 37, dual: true, gap: 3.0, edge: '#5f7d4a' },
};
const T = { snapAngle: 15, snapLen: 8, wallCap: 12, wallStart: 6, batter: 2, kMul: 1, minSpan: 4, water: 14 };
const SHOULDER = 1.0, FORM_DROP = 0.15, PAVE_H = 0.12, DECK_T = 1.2, TUNNEL_H = 5.5, PARAPET_H = 1.0;
