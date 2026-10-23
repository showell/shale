// What a run did, as the pages show it (index.html, tutorial.html): what it
// printed, with an edit's diff coloured; the changes it made; how it ended.
// The classes are the page's to style: diff-add, diff-del, diff-hunk, none,
// effects, op, ending, stopped, trail, label.

// The report `shale.run` gives, read back: {effects: [{op, path, outcome}],
// ending, trail, steps, finished}.
export function parseReport(report) {
  const lines = report.split("\n").filter((l) => l);
  const effects = [];
  let ending = "";
  const trail = [];
  for (const l of lines) {
    if (l.startsWith("── effects")) continue;
    if (l.startsWith("── ")) ending = l.slice(3);
    else if (l.startsWith("   called from")) trail.push(l.trim());
    else {
      const sp = l.indexOf(" ");
      const colon = l.lastIndexOf(": ");
      effects.push({ op: l.slice(0, sp), path: l.slice(sp + 1, colon), outcome: l.slice(colon + 2) });
    }
  }
  const m = ending.match(/\((\d+) steps\)$/);
  return { effects, ending, trail, steps: m ? Number(m[1]) : null, finished: ending.startsWith("finished") };
}

const el = (tag, cls, text) => Object.assign(document.createElement(tag), { className: cls ?? "", textContent: text ?? "" });

function printed(text) {
  const pre = el("pre");
  if (!text) {
    pre.append(el("span", "none", "(nothing printed)"));
    return pre;
  }
  for (const line of text.split(/(?<=\n)/)) {
    let cls = "";
    if (line.startsWith("+++ ") || line.startsWith("--- ") || line.startsWith("@@")) cls = "diff-hunk";
    else if (line.startsWith("+")) cls = "diff-add";
    else if (line.startsWith("-")) cls = "diff-del";
    pre.append(cls ? el("span", cls, line) : document.createTextNode(line));
  }
  return pre;
}

// The run `r` ({output, report}) as elements, appended to `into`.
export function showRun(r, into) {
  const rep = parseReport(r.report);
  into.append(printed(r.output));
  if (rep.effects.length) {
    const box = el("div", "effects");
    box.append(el("span", "label", `Changes (${rep.effects.length})`));
    for (const e of rep.effects) {
      const row = el("div");
      row.append(el("span", "op", e.op), document.createTextNode(` ${e.path}: ${e.outcome}`));
      box.append(row);
    }
    into.append(box);
  }
  const end = el("div", "ending" + (rep.finished ? "" : " stopped"));
  end.append(document.createTextNode(rep.ending));
  for (const t of rep.trail) end.append(el("div", "trail", t));
  into.append(end);
  return rep;
}
