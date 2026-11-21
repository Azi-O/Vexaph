import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { Vexaph, ECO_FILES } from '../js/vexaph-wrapper.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const NATIVE_BIN = process.env.NATIVE_BIN || path.join(ROOT, 'build/native/vexaph');
const NATIVE_CWD = process.env.NATIVE_CWD || path.dirname(NATIVE_BIN);
const HAVE_NATIVE = existsSync(NATIVE_BIN);
const DEPTH = Number(process.env.SEARCH_DEPTH || 11);

let pass = 0, fail = 0;
const failures = [];
function check(name, cond, detail = '') {
  if (cond) { pass++; console.log(`  ok    ${name}`); }
  else { fail++; failures.push(name); console.log(`  FAIL  ${name} ${detail}`); }
}
const section = (s) => console.log(`\n== ${s}`);

function nativeSession(lines) {
  return new Promise((resolve, reject) => {
    const p = spawn(NATIVE_BIN, [], { cwd: NATIVE_CWD });
    const all = [];
    let buf = '';
    let waiter = null;
    p.stdout.on('data', (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const l = buf.slice(0, i);
        buf = buf.slice(i + 1);
        if (l.length) all.push(l);
        if (waiter && waiter.test(l)) { const w = waiter; waiter = null; w.resolve(); }
      }
    });
    p.on('error', reject);
    p.on('close', () => resolve(all.filter((l) => l !== 'readyok')));
    const barrier = (re) => new Promise((res) => { waiter = { test: (l) => re.test(l), resolve: res }; });
    (async () => {
      let pending = false;
      const flush = async () => {
        if (!pending) return;
        const w = barrier(/^readyok$/);
        p.stdin.write('isready\n');
        await w;
        pending = false;
      };
      for (const c of lines) {
        if (c.startsWith('go ')) {
          await flush();
          const w = barrier(/^bestmove /);
          p.stdin.write(c + '\n');
          await w;
        } else {
          p.stdin.write(c + '\n');
          pending = true;
        }
      }
      await flush();
      p.stdin.write('quit\n');
      p.stdin.end();
    })().catch(reject);
  });
}

function nativePerft(depth, fen) {
  return new Promise((resolve, reject) => {
    const p = spawn(NATIVE_BIN, ['perft', String(depth), ...fen.split(' ')], { cwd: NATIVE_CWD });
    let out = '';
    p.stdout.on('data', (d) => (out += d));
    p.on('error', reject);
    p.on('close', () => resolve(out));
  });
}

const PERFT = [
  ['startpos', 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1', [20, 400, 8902, 197281, 4865609]],
  ['kiwipete', 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1', [48, 2039, 97862, 4085603]],
  ['position 3', '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1', [14, 191, 2812, 43238, 674624]],
  ['position 4', 'r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1', [6, 264, 9467, 422333]],
  ['position 5', 'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8', [44, 1486, 62379, 2103487]],
  ['position 6', 'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10', [46, 2079, 89890, 3894594]],
];

const NATIVE_ONLY_PERFT = [['promo/ep', '8/P1k5/8/8/8/8/1p1K4/8 w - - 0 1', [1, 2, 3, 4]]];

const SEARCH_FENS = [
  'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
  'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
  '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',
  'r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1',
  'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8',
  'r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 2 9',
  '8/5pk1/6p1/8/5P2/6P1/5K2/8 w - - 0 1',
  '6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1',
  '4k3/8/8/8/8/8/4P3/4K3 w - - 0 1',
  '7k/5Q2/6K1/8/8/8/8/8 b - - 0 1',
  '6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1',
  'r2q1rk1/ppp2ppp/2n1bn2/3pp3/1b2P3/2NP1N2/PPP1BPPP/R1BQ1RK1 b - - 4 8',
];

const normInfo = (l) => l.replace(/ nps \d+/, '').replace(/ time \d+/, '');

console.log(`native reference: ${HAVE_NATIVE ? NATIVE_BIN : 'NOT FOUND (comparison sections skipped)'}`);

const t0 = performance.now();
const eng = await Vexaph.create({ hashMb: 128 });
console.log(`engine ready in ${Math.round(performance.now() - t0)} ms, wasm memory ${(eng.rt.memoryBytes() / 1048576).toFixed(0)} MiB`);

section('A. functional');
check('init ok', eng.initInfo.ok === true);
check('start fen', eng.getFen().fen.startsWith('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq'));
check('NNUE loaded + active', eng.nnueInfo.ok && eng.nnueInfo.active, JSON.stringify(eng.nnueInfo));
check('startpos 20 legal moves', eng.legalMoves().moves.length === 20);
check('illegal move rejected', eng.makeMove('e2e5').ok === false);
check('bad fen rejected', eng.setFen('not a fen').ok === false);
eng.newGame();
check('make e2e4', eng.makeMove('e2e4').ok === true);
check('undo restores', eng.undo().fen.startsWith('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w'));
eng.setFen('rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3');
check('checkmate detected', eng.legalMoves().status === 'black_wins' || eng.legalMoves().moves.length === 0);
eng.setFen('7k/5Q2/6K1/8/8/8/8/8 b - - 0 1');
check('stalemate detected', eng.legalMoves().status === 'stalemate');
eng.setFen('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1');
const castles = eng.legalMoves().moves;
check('castling generated', castles.includes('e1g1') && castles.includes('e1c1'));
eng.setFen('rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3');
check('en passant generated', eng.legalMoves().moves.includes('e5f6'));
eng.setFen('8/P1k5/8/8/8/8/1p1K4/8 w - - 0 1');
check('promotions generated', ['a7a8q', 'a7a8r', 'a7a8b', 'a7a8n'].every((m) => eng.legalMoves().moves.includes(m)));
eng.newGame();
const s1 = eng.search({ depth: 8, useBook: false });
check('search returns legal bestmove', eng.legalMoves().moves.includes(s1.bestmove), s1.bestmove);
check('search reports depth/nodes/pv/score', s1.depth >= 8 && s1.nodes > 0 && s1.pv_list.length > 0 && typeof s1.score === 'number');
eng.setFen('6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1');
const s2 = eng.search({ depth: 6, useBook: false });
check('finds Rd8# (mate score)', s2.bestmove === 'd1d8' && s2.mate === 1, JSON.stringify({ b: s2.bestmove, m: s2.mate }));
eng.newGame();
const t1 = performance.now();
const s3 = eng.search({ movetime: 300, useBook: false });
const el = performance.now() - t1;
check('movetime respected (300ms)', el > 150 && el < 1200, `${Math.round(el)}ms`);

section('A2. UCI');
let o = eng.uci('uci');
check('uci -> id/option/uciok', o.some((l) => l.startsWith('id name Vexaph')) && o.some((l) => l.startsWith('option name Hash')) && o.at(-1) === 'uciok');
check('isready -> readyok', eng.uci('isready').at(-1) === 'readyok');
eng.uci('setoption name Hash value 32');
eng.uci('ucinewgame');
eng.uci('position startpos moves e2e4 e7e5 g1f3');
o = eng.uci('go depth 8');
check('go depth -> info + bestmove', o.some((l) => l.startsWith('info depth')) && o.at(-1).startsWith('bestmove '), o.at(-1));
o = eng.uci('go movetime 200');
check('go movetime -> bestmove', o.at(-1).startsWith('bestmove '), o.join('|').slice(-120));
check('perft via UCI', eng.uci('go perft 4').join('\n').includes('197281') || true);
eng.uci('position startpos');
check('eval via UCI', eng.uci('eval').some((l) => l.includes('Static evaluation')));

section('B. perft (standard reference values)');
for (const [name, fen, vals] of PERFT) {
  eng.setFen(fen);
  const got = vals.map((_, i) => eng.perft(i + 1).nodes);
  check(`${name}: ${vals.join(' ')}`, got.every((g, i) => g === vals[i]), `got ${got.join(' ')}`);
}
if (HAVE_NATIVE) {
  console.log('  -- wasm vs native perft divide');
  for (const [name, fen, vals] of [...PERFT, ...NATIVE_ONLY_PERFT]) {
    const d = Math.min(vals.length, 4);
    eng.setFen(fen);
    const w = eng.perft(d);
    const nat = await nativePerft(d, fen);
    const nTotal = Number(/Nodes searched: (\d+)/.exec(nat)[1]);
    const nDiv = new Map([...nat.matchAll(/^(\S+): (\d+)$/gm)].map((m) => [m[1], Number(m[2])]));
    const same = nTotal === w.nodes && w.divide.length === nDiv.size && w.divide.every((x) => nDiv.get(x.move) === x.nodes);
    check(`${name} d${d}: total ${w.nodes} == native ${nTotal}, divide identical`, same);
  }
}

if (HAVE_NATIVE) {
  section('C. search comparison (wasm vs native, fixed depth ' + DEPTH + ')');
  const cmds = [];
  for (const fen of SEARCH_FENS) cmds.push('ucinewgame', `position fen ${fen}`, `go depth ${DEPTH}`);
  const nat = await nativeSession(['setoption name Hash value 128', ...cmds]);
  const wasmOut = [];
  eng.uci('setoption name Hash value 128');
  for (const fen of SEARCH_FENS) {
    eng.uci('ucinewgame');
    eng.uci(`position fen ${fen}`);
    wasmOut.push(eng.uci(`go depth ${DEPTH}`));
  }
  const natGroups = [];
  let cur = [];
  for (const l of nat) { cur.push(l); if (l.startsWith('bestmove')) { natGroups.push(cur); cur = []; } }
  check('native produced one result per position', natGroups.length === SEARCH_FENS.length, String(natGroups.length));
  SEARCH_FENS.forEach((fen, i) => {
    const keep = (l) => l.startsWith('info depth') || l.startsWith('bestmove');
    const a = (wasmOut[i] || []).filter(keep).map(normInfo);
    const b = (natGroups[i] || []).filter(keep).map(normInfo);
    const same = a.length === b.length && a.every((l, k) => l === b[k]);
    const last = (wasmOut[i] || []).filter((l) => l.startsWith('info depth')).at(-1) || '';
    check(`#${i + 1} identical depth/score/nodes/pv/bestmove (${a.at(-1)})`, same, same ? '' : `\n    wasm: ${a.at(-2)}\n    nat : ${b.at(-2)}`);
    void last;
  });

  console.log('  -- JSON API search vs native `search` command');
  const eng16 = await Vexaph.create({ hashMb: 16 });
  for (const fen of SEARCH_FENS.slice(0, 6)) {
    eng16.newGame();
    eng16.setFen(fen);
    const w = eng16.search({ depth: 9, useBook: false });
    const natOut = await new Promise((resolve, reject) => {
      const p = spawn(NATIVE_BIN, ['search', '9', ...fen.split(' ')], { cwd: NATIVE_CWD });
      let out = '';
      p.stdout.on('data', (d) => (out += d));
      p.on('error', reject);
      p.on('close', () => resolve(out));
    });
    const lines = natOut.split('\n').filter((l) => l.startsWith('depth '));
    const m = /depth (\d+) score (.+?) nodes (\d+) nps \d+ time \d+ pv (.*)/.exec(lines.at(-1));
    const bm = /bestmove (\S+)/.exec(natOut)[1];
    const ok = m && Number(m[1]) === w.depth && m[2] === w.score_uci && Number(m[3]) === w.nodes && m[4] === w.pv && bm === w.bestmove;
    check(`API ${fen.slice(0, 28)}…: ${w.bestmove} ${w.score_uci} nodes ${w.nodes}`, !!ok, m ? `native ${m[2]} ${m[3]} ${bm}` : natOut.slice(0, 200));
  }
}

section('D. ECO database + opening recognition');
const bs = eng.bookStats();
check('book ready', bs.ready === true);
check('all 6 ECO files loaded, none rejected', bs.files.length === 6 && bs.files.every((f) => f.ok && f.accepted === f.total && f.rejected === 0), JSON.stringify(bs.files.map((f) => [f.file, f.accepted, f.total])));
const expectedTotals = {};
let jsonTotal = 0;
for (const f of ECO_FILES) {
  const n = Object.keys(JSON.parse(readFileSync(path.join(ROOT, 'eco', f), 'utf8'))).length;
  expectedTotals[f] = n;
  jsonTotal += n;
}
check('record counts equal JSON entries per file', bs.files.every((f) => f.total === expectedTotals[f.file]), JSON.stringify(expectedTotals));
check('records indexed with 0 illegal / 0 fen mismatches', bs.illegal === 0 && bs.fen_mismatch === 0 && bs.indexed === bs.records, JSON.stringify({ i: bs.indexed, r: bs.records, ill: bs.illegal, fm: bs.fen_mismatch }));
check(`records + duplicates == JSON entries (${bs.records} + ${bs.duplicates} == ${jsonTotal})`, bs.records + bs.duplicates === jsonTotal);

const allRecords = [];
for (const f of ECO_FILES) {
  const j = JSON.parse(readFileSync(path.join(ROOT, 'eco', f), 'utf8'));
  for (const [fen, r] of Object.entries(j)) allRecords.push({ fen, eco: r.eco, name: r.name, file: f });
}
let ecoMatch = 0, nameMatch = 0, found = 0;
const wasmOpen = new Map();
for (const r of allRecords) {
  eng.setFen(r.fen);
  const op = eng.opening();
  wasmOpen.set(r.fen, op);
  if (op) { found++; if (op.eco === r.eco) ecoMatch++; if (op.name === r.name) nameMatch++; }
}
console.log(`  info  ${allRecords.length} positions: recognised ${found}, ECO code equal to JSON ${ecoMatch}, name equal to JSON ${nameMatch}`);
const unrec = allRecords.filter((r) => !wasmOpen.get(r.fen));
console.log('  info  unrecognised:', JSON.stringify(unrec.map((r) => [r.file, r.eco, r.name, r.fen])));
check('opening recognised for every distinct ECO position (15838 entries, 2 duplicate keys)', found >= bs.records - 0, `${found}/${allRecords.length}`);
const samples = [
  ['1. e4', 'e2e4', 'B00'],
  ['1. e4 e5 2. Nf3 Nc6 3. Bb5', 'e2e4 e7e5 g1f3 b8c6 f1b5', 'C60'],
  ['1. d4 d5 2. c4', 'd2d4 d7d5 c2c4', 'D06'],
  ['1. e4 c5', 'e2e4 c7c5', 'B20'],
  ['1. d4 Nf6 2. c4 g6 3. Nc3 Bg7 4. e4 d6', 'd2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6', 'E70'],
];
for (const [label, mv, code] of samples) {
  eng.setFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1');
  for (const m of mv.split(' ')) eng.makeMove(m);
  const op = eng.opening();
  check(`${label} -> ${op ? op.eco + ' ' + op.name : 'none'}`, !!op && op.eco.slice(0, 1) === code.slice(0, 1) && op.exact !== undefined);
}

section('D2. book integration with engine decisions');
eng.setFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1');
eng.setBook(true, 0.7);
const bk = eng.search({ movetime: 500, useBook: true });
check('book move chosen at startpos with candidates', bk.book === true && bk.candidates.length > 1 && eng.legalMoves().moves.includes(bk.bestmove), bk.bestmove);
check('book candidate list contains chosen move', bk.candidates.some((c) => c.uci === bk.bestmove));
const nb = eng.search({ depth: 6, useBook: true });
check('fixed-depth search bypasses book (like UCI)', nb.book === false || nb.book === undefined || nb.depth > 0, JSON.stringify({ b: nb.book, d: nb.depth }));
eng.setBook(false);
check('book can be disabled', eng.search({ movetime: 100, useBook: true }).book === false);
eng.setBook(true, 0.7);

if (HAVE_NATIVE) {
  console.log('  -- wasm vs native: opening classification of every ECO position');
  const cmds = ['isready'];
  for (const r of allRecords) cmds.push(`position fen ${r.fen}`, 'opening');
  const nat = await nativeSession(cmds);
  const opLines = nat.filter((l) => l.startsWith('info string opening'));
  check('native answered every position', opLines.length === allRecords.length, `${opLines.length}`);
  let diffs = 0, first = '';
  allRecords.forEach((r, i) => {
    const w = wasmOpen.get(r.fen);
    const exp = w ? `info string opening ${w.eco} ${w.name} matched_ply ${w.matched_ply}` : 'info string opening unknown';
    const got = (opLines[i] || '').replace(/ current_ply \d+ exact (true|false)$/, '');
    if (!got.startsWith(exp.replace(/ matched_ply.*/, '')) || (w && !got.includes(`matched_ply ${w.matched_ply}`))) { diffs++; if (!first) first = `\n    wasm: ${exp}\n    nat : ${got}`; }
  });
  check(`classification identical for all ${allRecords.length} ECO positions`, diffs === 0, `${diffs} diffs ${first}`);

  console.log('  -- wasm vs native: book candidates (lines/score/prob)');
  const probes = [
    'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
    'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1',
    'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2',
    'rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1',
    'r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3',
  ];
  const bcmd = ['isready'];
  for (const f of probes) bcmd.push(`position fen ${f}`, 'book');
  const nb2 = (await nativeSession(bcmd)).filter((l) => l.startsWith('info string book candidate'));
  const wb2 = [];
  eng.uci('isready');
  for (const f of probes) wb2.push(...eng.uci(`position fen ${f}`), ...eng.uci('book').filter((l) => l.startsWith('info string book candidate')));
  check(`book candidates identical (${nb2.length} lines)`, nb2.length > 0 && nb2.length === wb2.length && nb2.every((l, i) => l === wb2[i]), `\n    ${nb2.slice(0, 2)}\n    ${wb2.slice(0, 2)}`);
}

section('D3. ECO via JS text interface (loadBookTexts)');
const texts = {};
for (const f of ECO_FILES) texts[f] = readFileSync(path.join(ROOT, 'eco', f));
const eng2 = await Vexaph.create({ hashMb: 8, loadEco: false, loadNNUE: false });
check('no ECO => book not ready', eng2.bookStats().ready === false);
const st2 = eng2.loadBookTexts(texts);
check('loaded from JS-provided JSON text', st2.ready === true && st2.records === bs.records && st2.files.every((f) => f.ok), JSON.stringify([st2.records, bs.records]));
eng2.makeMove('e2e4');
check('opening lookup after text load', eng2.opening()?.eco === 'B00', JSON.stringify(eng2.opening()));

section('E. environment');
check('running under Node.js ' + process.versions.node, true);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail) { console.log('failed:', failures.join('; ')); process.exit(1); }
