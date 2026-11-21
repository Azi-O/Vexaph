import { Vexaph } from './vexaph-wrapper.js';

let eng = null;
const THINK_MS = 1000;

function state() {
  const lm = eng.legalMoves();
  const f = eng.getFen();
  return { fen: f.fen, side: f.side, moves: lm.moves, status: lm.status, inCheck: lm.in_check };
}

self.onmessage = async (e) => {
  const m = e.data;
  try {
    let r;
    switch (m.type) {
      case 'init':
        eng = await Vexaph.create({ hashMb: 64 });
        r = { ready: true, nnue: !!(eng.nnueInfo && eng.nnueInfo.active), book: !!(eng.bookInfo && eng.bookInfo.ready) };
        break;
      case 'fen':
        eng.setFen(m.fen);
        r = state();
        break;
      case 'new':
        eng.newGame();
        r = state();
        break;
      case 'move': {
        const mv = eng.makeMove(m.uci);
        r = { ok: mv.ok, ...state() };
        break;
      }
      case 'undo': {
        const u = eng.undo();
        r = { ok: u.ok, ...state() };
        break;
      }
      case 'bot': {
        const s = eng.search({ movetime: THINK_MS, useBook: true });
        if (s.bestmove && s.bestmove !== '0000') eng.makeMove(s.bestmove);
        r = { best: s.bestmove, ...state() };
        break;
      }
      case 'hint': {
        const s = eng.search({ movetime: THINK_MS, useBook: true });
        r = { best: s.bestmove };
        break;
      }
      default:
        throw new Error('unknown message ' + m.type);
    }
    self.postMessage({ id: m.id, ...r });
  } catch (err) {
    self.postMessage({ id: m.id, error: String((err && err.message) || err) });
  }
};
