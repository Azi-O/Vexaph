const isNode = typeof process !== 'undefined' && !!(process.versions && process.versions.node);

export async function loadBytes(src) {
  if (src instanceof Uint8Array) return src;
  if (src instanceof ArrayBuffer) return new Uint8Array(src);
  if (typeof src === 'string' && isNode && !/^https?:/.test(src)) {
    const fs = await import('node:fs/promises');
    const b = await fs.readFile(src.startsWith('file:') ? new URL(src) : src);
    return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
  }
  const r = await fetch(src);
  if (!r.ok) throw new Error(`fetch ${src}: HTTP ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}

const norm = (p) => p.replace(/^\.\//, '').replace(/\/+/g, '/');

export async function instantiate({ wasm, files = {}, onStdout = null, onStderr = null }) {
  const bytes = await loadBytes(wasm);
  const vfs = new Map();
  for (const [p, d] of Object.entries(files)) vfs.set(norm(p), d instanceof Uint8Array ? d : new Uint8Array(d));
  const handles = [null];
  const dec = new TextDecoder('utf-8');
  const enc = new TextEncoder();
  let mem = null;
  let inst = null;

  const cstr = (ptr) => {
    const u8 = new Uint8Array(mem.buffer);
    let e = ptr;
    while (u8[e] !== 0) e++;
    return dec.decode(u8.subarray(ptr, e));
  };

  const env = {
    host_now: () => performance.now(),
    host_file_open(p) {
      const f = vfs.get(norm(cstr(p)));
      if (!f) return 0;
      handles.push(f);
      return handles.length - 1;
    },
    host_file_close(h) { handles[h] = null; },
    host_file_size: (h) => handles[h].length,
    host_file_read(h, dst, off, len) {
      const f = handles[h];
      if (!f || off >= f.length) return 0;
      const n = Math.min(len, f.length - off);
      new Uint8Array(mem.buffer, dst, n).set(f.subarray(off, off + n));
      return n;
    },
    host_file_stat(p) {
      const path = norm(cstr(p));
      if (vfs.has(path)) return 1;
      const pre = path.replace(/\/$/, '') + '/';
      for (const k of vfs.keys()) if (k.startsWith(pre)) return 2;
      return 0;
    },
    host_write(fd, ptr, n) {
      const s = dec.decode(new Uint8Array(mem.buffer, ptr, n));
      const cb = fd === 2 ? onStderr : onStdout;
      if (cb) cb(s);
    },
    host_abort() { throw new Error('vexaph: abort()'); },
  };

  const mod = await WebAssembly.instantiate(bytes, { env });
  inst = mod.instance;
  mem = inst.exports.memory;
  const ex = inst.exports;

  const alloc = (s) => {
    const b = enc.encode(s);
    const p = ex.malloc(b.length + 1);
    if (!p) throw new Error('vexaph: out of memory');
    const u8 = new Uint8Array(mem.buffer);
    u8.set(b, p);
    u8[p + b.length] = 0;
    return p;
  };

  const call = (name, ...args) => {
    const ptrs = [];
    const conv = args.map((a) => {
      if (typeof a === 'string') { const p = alloc(a); ptrs.push(p); return p; }
      if (typeof a === 'boolean') return a ? 1 : 0;
      return a;
    });
    try {
      const r = ex[name](...conv);
      return r;
    } finally {
      for (const p of ptrs) ex.free(p);
    }
  };

  const callStr = (name, ...args) => cstr(call(name, ...args));
  const callJson = (name, ...args) => {
    const s = callStr(name, ...args);
    return s === '' ? null : JSON.parse(s);
  };

  return {
    exports: ex,
    vfs,
    cstr,
    call,
    callStr,
    callJson,
    addFile: (p, d) => vfs.set(norm(p), d instanceof Uint8Array ? d : new Uint8Array(d)),
    memoryBytes: () => mem.buffer.byteLength,
  };
}
