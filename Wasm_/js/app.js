const GLYPH = { k: '♚', q: '♛', r: '♜', b: '♝', n: '♞', p: '♟' };
const TEXT = '︎';

const boardEl = document.getElementById('board');
const statusEl = document.getElementById('status');
const promoEl = document.getElementById('promo');
const ghostEl = document.getElementById('ghost');
const btn = {
  undo: document.getElementById('b-undo'),
  black: document.getElementById('b-black'),
  white: document.getElementById('b-white'),
  hint: document.getElementById('b-hint'),
};

const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
let seq = 0;
const pending = new Map();
worker.onmessage = (e) => {
  const { id, ...r } = e.data;
  const p = pending.get(id);
  if (!p) return;
  pending.delete(id);
  if (r.error) p.reject(new Error(r.error));
  else p.resolve(r);
};
worker.onerror = (e) => setStatus('Engine failed to start: ' + (e.message || 'unknown error'), true);
const rpc = (type, data = {}) => new Promise((resolve, reject) => {
  const id = ++seq;
  pending.set(id, { resolve, reject });
  worker.postMessage({ id, type, ...data });
});

const G = {
  human: 'w',
  fen: '',
  side: 'w',
  moves: [],
  status: 'ongoing',
  inCheck: false,
  plies: [],
  selected: null,
  hint: null,
  busy: true,
  drag: null,
};

const sqName = (f, r) => 'abcdefgh'[f] + (r + 1);

function parseFen(fen) {
  const grid = {};
  const rows = fen.split(' ')[0].split('/');
  rows.forEach((row, i) => {
    let f = 0;
    for (const ch of row) {
      if (/\d/.test(ch)) f += Number(ch);
      else {
        grid[sqName(f, 7 - i)] = { c: ch === ch.toUpperCase() ? 'w' : 'b', t: ch.toLowerCase() };
        f++;
      }
    }
  });
  return grid;
}

function setStatus(text, alert = false) {
  statusEl.textContent = text;
  statusEl.classList.toggle('alert', alert);
}

function over() { return G.status !== 'ongoing'; }
function humanTurn() { return G.side === G.human && !over(); }

function statusText() {
  switch (G.status) {
    case 'white_wins': return G.human === 'w' ? 'Checkmate — you win' : 'Checkmate — Vexaph wins';
    case 'black_wins': return G.human === 'b' ? 'Checkmate — you win' : 'Checkmate — Vexaph wins';
    case 'stalemate': return 'Draw by stalemate';
    case 'draw_fifty': return 'Draw by fifty-move rule';
    case 'draw_material': return 'Draw by insufficient material';
    case 'draw_repetition': return 'Draw by threefold repetition';
  }
  if (G.busy) return 'Vexaph is thinking…';
  return G.inCheck ? 'Check — your move' : 'Your move';
}

function refreshControls() {
  const idle = !G.busy;
  btn.undo.disabled = !idle || G.plies.length === 0;
  btn.black.disabled = !idle;
  btn.white.disabled = !idle;
  btn.hint.disabled = !idle || !humanTurn();
  btn.black.classList.toggle('on', G.human === 'b');
  btn.white.classList.toggle('on', G.human === 'w');
}

function render() {
  const grid = parseFen(G.fen);
  const flip = G.human === 'b';
  const last = G.plies.length ? G.plies[G.plies.length - 1] : null;
  const targets = new Map();
  if (G.selected) for (const m of G.moves) if (m.slice(0, 2) === G.selected) targets.set(m.slice(2, 4), true);
  let kingSq = null;
  if (G.inCheck) for (const [s, p] of Object.entries(grid)) if (p.t === 'k' && p.c === G.side) kingSq = s;
  boardEl.textContent = '';
  for (let vr = 0; vr < 8; vr++) {
    for (let vf = 0; vf < 8; vf++) {
      const f = flip ? 7 - vf : vf;
      const r = flip ? vr : 7 - vr;
      const name = sqName(f, r);
      const el = document.createElement('div');
      el.className = 'sq ' + ((f + r) % 2 === 0 ? 'd' : 'l');
      el.dataset.sq = name;
      if (last && (name === last.slice(0, 2) || name === last.slice(2, 4))) el.classList.add('last');
      if (G.selected === name) el.classList.add('sel');
      if (G.hint && (name === G.hint.slice(0, 2) || name === G.hint.slice(2, 4))) el.classList.add('hint');
      if (name === kingSq) el.classList.add('check');
      if (vf === 0) { const c = document.createElement('span'); c.className = 'co rank'; c.textContent = r + 1; el.appendChild(c); }
      if (vr === 7) { const c = document.createElement('span'); c.className = 'co file'; c.textContent = 'abcdefgh'[f]; el.appendChild(c); }
      const p = grid[name];
      if (targets.has(name)) {
        const m = document.createElement('div');
        m.className = p ? 'ring' : 'dot';
        el.appendChild(m);
      }
      if (p) {
        const pc = document.createElement('div');
        pc.className = 'pc ' + p.c;
        pc.textContent = GLYPH[p.t] + TEXT;
        if (G.drag && G.drag.from === name && G.drag.active) pc.classList.add('drag-src');
        el.appendChild(pc);
      }
      boardEl.appendChild(el);
    }
  }
  setStatus(statusText(), over() || G.inCheck);
  refreshControls();
}

function apply(st) {
  G.fen = st.fen;
  G.side = st.side;
  G.moves = st.moves;
  G.status = st.status;
  G.inCheck = st.inCheck;
}

async function botMove() {
  if (over() || G.side === G.human) return;
  G.busy = true;
  render();
  try {
    const r = await rpc('bot');
    apply(r);
    if (r.best && r.best !== '0000') G.plies.push(r.best);
  } catch (err) {
    setStatus('Engine error: ' + err.message, true);
  }
  G.busy = false;
  G.selected = null;
  G.hint = null;
  render();
}

async function playMove(uci) {
  G.busy = true;
  G.selected = null;
  G.hint = null;
  promoEl.classList.remove('open');
  try {
    const r = await rpc('move', { uci });
    if (!r.ok) { G.busy = false; render(); return; }
    apply(r);
    G.plies.push(uci);
  } catch (err) {
    setStatus('Engine error: ' + err.message, true);
    G.busy = false;
    render();
    return;
  }
  G.busy = false;
  render();
  await botMove();
}

function askPromotion(from, to) {
  const bar = promoEl.firstElementChild;
  bar.textContent = '';
  for (const t of ['q', 'r', 'b', 'n']) {
    const b = document.createElement('button');
    const pc = document.createElement('div');
    pc.className = 'pc ' + G.human;
    pc.textContent = GLYPH[t] + TEXT;
    b.appendChild(pc);
    b.onclick = () => playMove(from + to + t);
    bar.appendChild(b);
  }
  promoEl.classList.add('open');
}
promoEl.addEventListener('pointerdown', (e) => {
  if (e.target === promoEl) { promoEl.classList.remove('open'); G.selected = null; render(); }
});

function tryMove(from, to) {
  const cands = G.moves.filter((m) => m.slice(0, 2) === from && m.slice(2, 4) === to);
  if (!cands.length) return false;
  if (cands.length > 1 || cands[0].length === 5) { G.selected = null; render(); askPromotion(from, to); return true; }
  playMove(cands[0]);
  return true;
}

function squareAt(x, y) {
  const el = document.elementFromPoint(x, y);
  const sq = el && el.closest ? el.closest('.sq') : null;
  return sq ? sq.dataset.sq : null;
}

boardEl.addEventListener('pointerdown', (e) => {
  if (G.busy || !humanTurn() || promoEl.classList.contains('open')) return;
  const sq = squareAt(e.clientX, e.clientY);
  if (!sq) return;
  if (G.selected && G.selected !== sq && tryMove(G.selected, sq)) return;
  const grid = parseFen(G.fen);
  const p = grid[sq];
  if (p && p.c === G.human) {
    G.hint = null;
    G.selected = sq;
    G.drag = { from: sq, x: e.clientX, y: e.clientY, active: false, piece: p };
    boardEl.setPointerCapture(e.pointerId);
  } else {
    G.selected = null;
  }
  render();
});

boardEl.addEventListener('pointermove', (e) => {
  const d = G.drag;
  if (!d) return;
  if (!d.active && Math.hypot(e.clientX - d.x, e.clientY - d.y) > 5) {
    d.active = true;
    ghostEl.className = 'pc ' + d.piece.c;
    ghostEl.textContent = GLYPH[d.piece.t] + TEXT;
    ghostEl.style.display = 'block';
    render();
  }
  if (d.active) { ghostEl.style.left = e.clientX + 'px'; ghostEl.style.top = e.clientY + 'px'; }
});

function endDrag(e, cancel) {
  const d = G.drag;
  if (!d) return;
  G.drag = null;
  ghostEl.style.display = 'none';
  if (d.active) {
    const to = cancel ? null : squareAt(e.clientX, e.clientY);
    if (to && to !== d.from && tryMove(d.from, to)) return;
    render();
  }
}
boardEl.addEventListener('pointerup', (e) => endDrag(e, false));
boardEl.addEventListener('pointercancel', (e) => endDrag(e, true));

async function newGame(color) {
  if (G.busy) return;
  G.busy = true;
  G.human = color;
  G.plies = [];
  G.selected = null;
  G.hint = null;
  promoEl.classList.remove('open');
  const st = await rpc('new');
  apply(st);
  G.busy = false;
  render();
  await botMove();
}

async function undo() {
  if (G.busy || !G.plies.length) return;
  G.busy = true;
  G.selected = null;
  G.hint = null;
  promoEl.classList.remove('open');
  let st = null;
  do {
    st = await rpc('undo');
    if (!st.ok) break;
    G.plies.pop();
  } while (G.plies.length && st.side !== G.human);
  if (st) apply(st);
  G.busy = false;
  render();
  await botMove();
}

async function hint() {
  if (G.busy || !humanTurn()) return;
  G.busy = true;
  G.selected = null;
  render();
  setStatus('Vexaph is thinking…');
  try {
    const r = await rpc('hint');
    G.hint = r.best && r.best !== '0000' ? r.best : null;
  } catch (err) {
    setStatus('Engine error: ' + err.message, true);
  }
  G.busy = false;
  render();
}

btn.undo.onclick = undo;
btn.black.onclick = () => newGame('b');
btn.white.onclick = () => newGame('w');
btn.hint.onclick = hint;

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && promoEl.classList.contains('open')) { promoEl.classList.remove('open'); G.selected = null; render(); }
});

(async () => {
  G.fen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
  render();
  try {
    await rpc('init');
    const st = await rpc('new');
    apply(st);
    G.busy = false;
    render();
    window.__vexaph = {
      G,
      load: async (fen, human) => {
        G.human = human;
        G.plies = [];
        G.selected = null;
        G.hint = null;
        apply(await rpc('fen', { fen }));
        render();
        await botMove();
      },
    };
  } catch (err) {
    setStatus('Engine failed to load: ' + err.message, true);
  }
})();
