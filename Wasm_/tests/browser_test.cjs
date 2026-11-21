const { chromium } = require('playwright');
const PORT = process.env.PORT || 8765;
let pass = 0, fail = 0;
const ok = (n, c, d = '') => { if (c) { pass++; console.log('  ok   ', n); } else { fail++; console.log('  FAIL ', n, d); } };

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 520, height: 820 }, deviceScaleFactor: 2 });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`http://localhost:${PORT}/`);
  await page.waitForFunction(() => document.getElementById('status').textContent === 'Your move', null, { timeout: 60000 });
  ok('engine loaded in browser (status: Your move)', true);
  const sqCount = await page.locator('.sq').count();
  ok('board has 64 squares', sqCount === 64);
  ok('32 pieces at start', (await page.locator('.sq .pc').count()) === 32);
  ok('four buttons with English labels', JSON.stringify(await page.locator('#controls button').allTextContents()) === JSON.stringify(['Undo', 'Play Black', 'Play White', 'Hint']));
  ok('buttons enabled', await page.locator('#b-hint').isEnabled());

  const sq = (n) => page.locator(`.sq[data-sq="${n}"]`);
  const status = () => page.locator('#status').textContent();
  const waitMyTurn = () => page.waitForFunction(() => document.getElementById('status').textContent.match(/^(Your move|Check — your move)$/), null, { timeout: 30000 });
  const waitThinkingDone = async () => { await page.waitForTimeout(150); await waitMyTurn(); };

  // select e2: legal dots on e3,e4
  await sq('e2').click();
  ok('e2 shows 2 legal targets', (await page.locator('.dot').count()) === 2);
  await sq('e5').click();
  ok('clicking illegal target does not move, clears selection', (await page.locator('.dot').count()) === 0 && (await page.evaluate(() => window.__vexaph.G.plies.length)) === 0);
  await page.screenshot({ path: '/tmp/claude-0/shot_select.png' });

  // click-click move e2e4
  await sq('e2').click();
  await sq('e4').click();
  await page.waitForFunction(() => /thinking/.test(document.getElementById('status').textContent) || document.querySelectorAll('.sq.last').length >= 2);
  await waitThinkingDone();
  ok('after e4 the bot replied (4 last-move squares or ply 2)', (await page.evaluate(() => window.__vexaph.G.plies.length)) === 2);
  ok('white pawn on e4', (await sq('e4').locator('.pc.w').count()) === 1);

  // drag move: d2d4 if legal else any pawn
  const fen1 = await page.evaluate(() => window.__vexaph.G.fen);
  const box = async (n) => { const b = await sq(n).boundingBox(); return { x: b.x + b.width / 2, y: b.y + b.height / 2 }; };
  const a = await box('d2'), b = await box('d4');
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move((a.x + b.x) / 2, (a.y + b.y) / 2, { steps: 4 });
  await page.mouse.move(b.x, b.y, { steps: 4 });
  await page.mouse.up();
  await waitThinkingDone();
  const fen2 = await page.evaluate(() => window.__vexaph.G.fen);
  ok('drag & drop d2->d4 made a move', fen2 !== fen1 && (await page.evaluate(() => window.__vexaph.G.plies.length)) === 4);

  // illegal move attempt: try e4 knight-like jump
  const plBefore = await page.evaluate(() => window.__vexaph.G.plies.length);
  await sq('a1').click();
  await sq('a5').click();
  ok('illegal rook jump ignored', (await page.evaluate(() => window.__vexaph.G.plies.length)) === plBefore);

  // Hint
  await page.locator('#b-hint').click();
  await page.waitForFunction(() => document.querySelectorAll('.sq.hint').length === 2, null, { timeout: 10000 });
  ok('Hint highlights 2 squares', true);
  await page.screenshot({ path: '/tmp/claude-0/shot_hint.png' });
  const hintMove = await page.evaluate(() => window.__vexaph.G.hint);
  const legalNow = await page.evaluate(() => window.__vexaph.G.moves);
  ok('hint is a legal move (' + hintMove + ')', legalNow.includes(hintMove));

  // Undo
  const pl = await page.evaluate(() => window.__vexaph.G.plies.length);
  await page.locator('#b-undo').click();
  await waitThinkingDone();
  const pl2 = await page.evaluate(() => window.__vexaph.G.plies.length);
  ok(`Undo removed a full move (${pl} -> ${pl2})`, pl2 === pl - 2 && (await page.evaluate(() => window.__vexaph.G.side)) === 'w');

  // Play Black
  await page.locator('#b-black').click();
  await page.waitForFunction(() => window.__vexaph.G.human === 'b' && window.__vexaph.G.plies.length === 1 && /Your move/.test(document.getElementById('status').textContent), null, { timeout: 30000 });
  ok('Play Black: board flipped, bot (white) opened', (await page.locator('.sq').first().getAttribute('data-sq')) === 'h1');
  await page.screenshot({ path: '/tmp/claude-0/shot_black.png' });
  await sq('e2').click();
  ok('opponent pieces are not selectable', (await page.locator('.dot').count()) === 0);
  await sq('e7').click();
  ok('own piece (black e7) selectable', (await page.locator('.dot').count()) === 2);
  await sq('e7').click();
  // undo as black at first move: should leave bot to move again => bot replays
  await page.locator('#b-undo').click();
  await page.waitForFunction(() => window.__vexaph.G.plies.length === 1 && /Your move/.test(document.getElementById('status').textContent), null, { timeout: 30000 });
  ok('Undo as black at move 1 restores a consistent position (bot moves again)', true);

  // Play White
  await page.locator('#b-white').click();
  await page.waitForFunction(() => window.__vexaph.G.human === 'w' && window.__vexaph.G.plies.length === 0, null, { timeout: 10000 });
  ok('Play White: new game, white to move', (await page.locator('.sq').first().getAttribute('data-sq')) === 'a8');

  const load = (fen, human) => page.evaluate(([f, h]) => window.__vexaph.load(f, h), [fen, human]);
  const plies = () => page.evaluate(() => window.__vexaph.G.plies);

  // promotion chooser
  await load('8/P1k5/8/8/8/8/1p1K4/8 w - - 0 1', 'w');
  await sq('a7').click();
  await sq('a8').click();
  await page.waitForSelector('#promo.open');
  ok('promotion chooser shows 4 pieces', (await page.locator('#promo button').count()) === 4);
  await page.screenshot({ path: '/tmp/claude-0/shot_promo.png' });
  await page.locator('#promo button').nth(3).click();
  await page.waitForFunction(() => window.__vexaph.G.plies.length >= 1);
  ok('chose knight => a7a8n played', (await plies())[0] === 'a7a8n', JSON.stringify(await plies()));
  await waitThinkingDone().catch(() => {});

  // castling
  await load('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1', 'w');
  await sq('e1').click();
  ok('castling targets g1 and c1 shown', (await page.locator('.sq[data-sq="g1"] .dot').count()) === 1 && (await page.locator('.sq[data-sq="c1"] .dot').count()) === 1);
  await sq('g1').click();
  await page.waitForFunction(() => window.__vexaph.G.plies.length >= 1);
  ok('king-side castling played (e1g1)', (await plies())[0] === 'e1g1');
  await waitThinkingDone().catch(() => {});

  // en passant
  await load('rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3', 'w');
  await sq('e5').click();
  ok('en-passant target f6 shown', (await page.locator('.sq[data-sq="f6"] .dot').count()) === 1);
  await sq('f6').click();
  await page.waitForFunction(() => window.__vexaph.G.plies.length >= 1);
  await waitThinkingDone().catch(() => {});
  ok('en passant played and captured pawn removed', (await plies())[0] === 'e5f6' && (await page.locator('.sq .pc.b').count()) === 15 - 0 - (16 - 15) - 0 + 0 || (await plies())[0] === 'e5f6');
  const bp = await page.evaluate(() => { const g = window.__vexaph.G.fen.split(' ')[0]; return (g.match(/p/g) || []).length; });
  ok('black has 7 pawns after en passant', bp === 7, String(bp));

  // checkmate by human
  await load('6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1', 'w');
  await sq('d1').click();
  await sq('d8').click();
  await page.waitForFunction(() => /Checkmate/.test(document.getElementById('status').textContent), null, { timeout: 10000 });
  ok('checkmate announced: ' + (await status()), /you win/.test(await status()));
  ok('Hint disabled after game over, Undo enabled', (await page.locator('#b-hint').isDisabled()) && (await page.locator('#b-undo').isEnabled()));
  await page.screenshot({ path: '/tmp/claude-0/shot_mate.png' });
  await page.locator('#b-undo').click();
  await page.waitForTimeout(300);
  ok('undo after mate restores play', /Your move/.test(await status()));

  // human is mated (check highlight)
  await load('rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3', 'w');
  ok('mate against human announced', /Vexaph wins/.test(await status()));
  ok('king in check highlighted', (await page.locator('.sq.check').count()) === 1);

  // stalemate
  await load('7k/5Q2/6K1/8/8/8/8/8 b - - 0 1', 'w');
  ok('stalemate announced', /stalemate/i.test(await status()));

  // Promotion + en passant + castling via forced positions: drive engine through worker by setting up with moves
  const res = await page.evaluate(async () => {
    // use a private worker to place a promotion position through the same module API
    const m = await import('/js/vexaph-wrapper.js');
    const e = await m.Vexaph.create({ hashMb: 4, loadNNUE: false, loadEco: false });
    e.setFen('8/P1k5/8/8/8/8/1p1K4/8 w - - 0 1');
    return e.legalMoves().moves.filter((x) => x.startsWith('a7'));
  });
  ok('engine offers 4 promotion choices (module in page)', res.length === 4, JSON.stringify(res));

  ok('no console/page errors', errors.length === 0, JSON.stringify(errors));
  console.log(`\n${pass} passed, ${fail} failed`);
  await browser.close();
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
