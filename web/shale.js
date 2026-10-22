// Shale in a browser: the wasm module (src/shale_wasm.zig) behind a small
// API. The files live in the module's memory; this reads and writes them.

const enc = new TextEncoder();
const dec = new TextDecoder();

export async function loadShale(bytes) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const x = instance.exports;

  // Copies `s` into memory the module owns, runs `f(ptr, len)`, frees it.
  function withString(s, f) {
    const b = typeof s === "string" ? enc.encode(s) : s;
    const n = Math.max(b.length, 1);
    const p = x.alloc(n);
    if (!p) throw new Error("shale: out of memory");
    new Uint8Array(x.memory.buffer, p, b.length).set(b);
    try {
      return f(p, b.length);
    } finally {
      x.free(p, n);
    }
  }

  const text = (ptr, len) => dec.decode(new Uint8Array(x.memory.buffer, ptr, len));
  const answer = () => text(x.answerPtr(), x.answerLen());

  return {
    put(path, data) {
      const ok = withString(path, (pp, pl) => withString(data, (dp, dl) => x.putFile(pp, pl, dp, dl)));
      if (ok !== 0) throw new Error(`shale: cannot put ${path}`);
    },
    remove(path) {
      withString(path, (p, n) => x.removePath(p, n));
    },
    clear() {
      x.clearTree();
    },
    // [{path, dir, size}] in path order.
    list() {
      x.listTree();
      return answer()
        .split("\n")
        .filter((l) => l)
        .map((l) => {
          const [kind, path, size] = l.split("\t");
          return { path, dir: kind === "d", size: Number(size || 0) };
        });
    },
    read(path) {
      const missing = withString(path, (p, n) => x.readFile(p, n));
      return missing ? null : answer();
    },
    // {output, report, finished}. `args` is {name: value}; `name` is what
    // the report calls the script.
    run(source, args = {}, name = "script") {
      const argText = Object.entries(args)
        .map(([k, v]) => `${k}=${v}`)
        .join("\n");
      const code = withString(source, (sp, sl) =>
        withString(name, (np, nl) =>
          withString(argText, (ap, al) => x.run(sp, sl, np, nl, ap, al, BigInt(Date.now()))),
        ),
      );
      return { output: answer(), report: text(x.reportPtr(), x.reportLen()), finished: code === 0 };
    },
  };
}
