// Runs the built module under Node: a tree seeded, a script that reads,
// writes and walks it, and the tree read back. `zig build web-test`, which
// passes the module's path.

import { readFileSync } from "node:fs";
import { loadShale } from "./shale.js";
import { sample, first } from "./sample.js";

const wasm = readFileSync(process.argv[2]);
const shale = await loadShale(wasm);

let failed = 0;
function expect(what, got, want) {
  if (got !== want) {
    failed++;
    console.log(`FAIL ${what}\n  got:  ${JSON.stringify(got)}\n  want: ${JSON.stringify(want)}`);
  }
}

shale.put("notes/a.txt", "alpha\nbeta\n");
shale.put("notes/b.txt", "gamma\n");

let r = shale.run(`print(read("notes/a.txt"))`);
expect("read", r.output, "alpha\nbeta\n\n");
expect("read finished", r.finished, true);

r = shale.run(`for e in walk_all("notes", {kind = "file"}) do print(e.path) end`);
expect("walk", r.output, "notes/a.txt\nnotes/b.txt\n");

r = shale.run(`replace("out/sum.txt", "two files")`);
expect("replace finished", r.finished, true);
expect("written", shale.read("out/sum.txt"), "two files");

r = shale.run(`edit("notes/b.txt", "gamma", "delta")`);
expect("edit", shale.read("notes/b.txt"), "delta\n");

r = shale.run(`print(arg("who", "nobody"))`, { who: "steve" });
expect("arg", r.output, "steve\n");

r = shale.run(`print(read("missing.txt"))`);
expect("stop", r.finished, false);
expect("stop says where", r.report.includes("stopped at script:1:"), true);

const paths = shale.list().map((e) => (e.dir ? e.path + "/" : e.path));
expect("list", paths.join(" "), "notes/ notes/a.txt notes/b.txt out/ out/sum.txt");

shale.remove("notes");
expect("removed", shale.list().map((e) => e.path).join(" "), "out out/sum.txt");

// The page's canned scripts, each on a fresh sample tree.
function fresh() {
  shale.clear();
  for (const [p, d] of Object.entries(sample)) shale.put(p, d);
}
const canned = (p, args) => shale.run(shale.read(p), args, p);

fresh();
r = canned("scripts/todos.shale");
expect("todos finished", r.finished, true);
expect("todos counted", r.output.endsWith("3 TODOs, kept in notes/todos.txt\n"), true);
expect("todos kept", shale.read("notes/todos.txt").split("\n").length, 4);

fresh();
r = canned("scripts/rename.shale");
expect("rename finished", r.finished, true);
expect("rename counted", r.output.endsWith("fetch_forecast -> load_forecast: 5 replaced\n"), true);
expect("rename left none", shale.read("src/weather/cli.py").includes("fetch_forecast"), false);

fresh();
r = canned("scripts/rename.shale", { from: "format_day", to: "render_day" });
expect("rename by args", r.output.includes("format_day -> render_day: "), true);
const left = shale.list().filter((e) => e.path.endsWith(".py") && /\bformat_day\b/.test(shale.read(e.path)));
expect("rename by args left none", left.map((e) => e.path).join(" "), "");

fresh();
r = canned("scripts/sizes.shale");
expect("sizes finished", r.finished, true);
expect("sizes total", r.output.includes(`${Object.keys(sample).length} files, `), true);

fresh();
r = canned("scripts/runaway.shale");
expect("runaway stopped", r.finished, false);
expect("runaway says why", r.report.includes("stopped at scripts/runaway.shale:6:9: budget steps"), true);

expect("first is a script", sample[first] !== undefined, true);

if (failed) {
  console.log(`${failed} failed`);
  process.exit(1);
}
console.log("web smoke test: ok");
