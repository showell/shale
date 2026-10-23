// Runs the built module under Node: a tree seeded, a script that reads,
// writes and walks it, and the tree read back. `zig build web-test`, which
// passes the module's path.

import { readFileSync } from "node:fs";
import { loadShale } from "./shale.js";
import { sample, first } from "./sample.js";
import { lessons } from "./lessons.js";
import { cardSvg } from "./card.js";

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
r = canned("scripts/untested.shale");
expect("untested", r.output.endsWith("2 of 4 functions have no test\n"), true);
expect("untested names", r.output.includes("fetch_forecast") && r.output.includes("main"), true);

fresh();
r = canned("scripts/imports.shale");
expect("imports", r.output.includes("cli        uses fetch, format\n"), true);
expect("imports roots", r.output.endsWith("imported by no module: cli\n"), true);

fresh();
r = canned("scripts/add_header.shale");
expect("header added", r.output.endsWith("5 files changed\n"), true);
expect("header there", shale.read("src/weather/cli.py").startsWith("# Part of weather, MIT licensed.\nimport argparse"), true);
r = canned("scripts/add_header.shale");
expect("header twice", r.output, "0 files changed\n");

fresh();
r = canned("scripts/bump_version.shale");
expect("bump patch", r.output.endsWith("0.3.1 -> 0.3.2\n"), true);
r = canned("scripts/bump_version.shale", { part: "major" });
expect("bump major", r.output.endsWith("0.3.2 -> 1.0.0\n"), true);
r = canned("scripts/bump_version.shale", { part: "huge" });
expect("bump refused", r.finished, false);
expect("bump refused unchanged", shale.read("pyproject.toml").includes('version = "1.0.0"'), true);

fresh();
r = canned("scripts/runaway.shale");
expect("runaway stopped", r.finished, false);
expect("runaway says why", r.report.includes("stopped at scripts/runaway.shale:6:9: budget steps"), true);

expect("first is a script", sample[first] !== undefined, true);

// The tutorial's lessons, every cell in order on one tree, as the page
// replays them: each finishes, or stops where it says it stops on purpose.
shale.clear();
lessons.forEach((l, i) => {
  l.cells.forEach((c, j) => {
    r = shale.run(c.code, {}, `lesson ${i + 1}, script ${j + 1}`);
    expect(`lesson ${i + 1} script ${j + 1} ${c.stops ? "stops" : "finishes"}: ${r.report.trim().split("\n").at(-1)}`, r.finished, !c.stops);
  });
});
expect("the lessons build the blog", shale.read("index.md")?.startsWith("# Field Notes\n\n- 2026-10-11: [Field notes on crows]"), true);
expect("the lessons' tags", shale.read("tags.md"), "# Tags\n\n- birds (1)\n- coast (1)\n- meta (1)\n- walking (2)\n- weather (1)\n");
expect("fifteen lessons", lessons.length, 15);

// The picture is what the lessons draw now: a lesson changed redraws it
// (node web/make_card.mjs).
expect("card.svg is current (node web/make_card.mjs)", readFileSync(new URL("./card.svg", import.meta.url), "utf8") === cardSvg(shale, lessons), true);
const landing = readFileSync(new URL("./learn.html", import.meta.url), "utf8");
expect("the landing starts the tutorial", landing.includes('href="tutorial.html"') && landing.includes('src="card.svg"'), true);

if (failed) {
  console.log(`${failed} failed`);
  process.exit(1);
}
console.log("web smoke test: ok");
