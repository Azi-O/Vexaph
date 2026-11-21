import { instantiate, loadBytes } from './vexaph.js';

export const ECO_FILES = ['ecoA.json', 'ecoB.json', 'ecoC.json', 'ecoD.json', 'ecoE.json', 'eco_interpolated.json'];
export const NNUE_FILE = 'nn-190f102a22c3.nnue';

export class Vexaph {
  constructor(rt) {
    this.rt = rt;
    this.output = [];
    this.ready = false;
  }

  static async create({
    wasm = new URL('../wasm/vexaph.wasm', import.meta.url).href,
    ecoBase = new URL('../eco/', import.meta.url).href,
    netUrl = new URL('../net/' + NNUE_FILE, import.meta.url).href,
    hashMb = 128,
    loadNNUE = true,
    loadEco = true,
    onOutput = null,
  } = {}) {
    const sink = [];
    const rt = await instantiate({
      wasm,
      onStdout: (s) => { sink.push(s); if (onOutput) onOutput(s); },
      onStderr: (s) => { sink.push(s); if (onOutput) onOutput(s); },
    });
    const eng = new Vexaph(rt);
    eng.output = sink;
    eng.onOutput = onOutput;
    const jobs = [];
    if (loadEco) for (const f of ECO_FILES) jobs.push(loadBytes(ecoBase + f).then((b) => rt.addFile('data/eco/' + f, b)));
    if (loadNNUE) jobs.push(loadBytes(netUrl).then((b) => rt.addFile(NNUE_FILE, b)));
    await Promise.all(jobs);
    eng.initInfo = rt.callJson('wasm_init', hashMb);
    if (loadNNUE) eng.nnueInfo = rt.callJson('wasm_load_nnue', NNUE_FILE);
    if (loadEco) eng.bookInfo = rt.callJson('wasm_load_book_dir', 'data/eco');
    eng.ready = true;
    return eng;
  }

  newGame() { return this.rt.callJson('wasm_new_game'); }
  setFen(fen) { return this.rt.callJson('wasm_set_fen', fen); }
  getFen() { return this.rt.callJson('wasm_get_fen'); }
  makeMove(uci) { return this.rt.callJson('wasm_make_move', uci); }
  undo() { return this.rt.callJson('wasm_undo'); }
  legalMoves() { return this.rt.callJson('wasm_legal_moves'); }
  setHash(mb) { return this.rt.callJson('wasm_set_hash', mb); }
  setBook(enabled, variety = 0.7) { return this.rt.callJson('wasm_set_book', enabled ? 1 : 0, variety, 1); }
  bookStats() { return this.rt.callJson('wasm_book_stats'); }
  opening() { return this.rt.callJson('wasm_opening'); }
  eval() { return this.rt.callJson('wasm_eval'); }
  nnueStatus() { return this.rt.callJson('wasm_nnue_status'); }
  perft(depth) { return this.rt.callJson('wasm_perft', depth); }
  search({ depth = 0, movetime = 0, useBook = true } = {}) {
    return this.rt.callJson('wasm_search', depth, movetime, useBook ? 1 : 0);
  }
  stop() { return this.rt.callJson('wasm_stop'); }

  loadBookTexts(texts) {
    const t = ECO_FILES.map((f) => (typeof texts[f] === 'string' ? texts[f] : new TextDecoder().decode(texts[f])));
    return this.rt.callJson('wasm_load_book', t[0], t[1], t[2], t[3], t[4], t[5]);
  }

  uci(line) {
    const out = this.rt.callStr('wasm_uci', line);
    return out.split('\n').filter((l) => l.length);
  }
}
