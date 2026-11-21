import { Vexaph } from '../js/vexaph-wrapper.js';
const t0 = performance.now();
const e = await Vexaph.create({ hashMb: 16 });
console.log('create ms', Math.round(performance.now() - t0));
console.log(e.initInfo, e.nnueInfo);
console.log(JSON.stringify(e.bookInfo).slice(0, 600));
console.log(e.perft(4).nodes);
