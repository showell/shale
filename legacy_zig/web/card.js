// Shale's picture (web/card.svg, the landing page's and lynrummy.com's home
// row's): lesson 13's tag rename as the tutorial shows it, drawn from the
// real run, the script, the lines it changed, what it reported, and the
// blog's files with the changed ones marked. `node web/make_card.mjs` writes
// it; web/smoke_test.mjs fails when the file is not what this draws now.

import { tokens } from "./highlight.js";

const LESSON = 12; // lesson 13, from 0
const CELL = 1;

const W = 600;
const H = 420;
const MONO = "'IBM Plex Mono', ui-monospace, Menlo, Consolas, 'DejaVu Sans Mono', monospace";
const SANS = "'IBM Plex Sans', system-ui, -apple-system, 'Segoe UI', sans-serif";
const INK = { "": "#1c2228", key: "#7a3b8f", str: "#2b6b45", num: "#a55a17", call: "#1f5c99", comment: "#7b858e" };

const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

function text(x, y, s, { size = 11.5, fill = INK[""], font = MONO, weight = 400 } = {}) {
  return `<text x="${x}" y="${y}" font-family="${font}" font-size="${size}" font-weight="${weight}" fill="${fill}" xml:space="preserve">${esc(s)}</text>`;
}

function card(x, y, w, h, title) {
  return [
    `<rect x="${x + 2}" y="${y + 3}" width="${w}" height="${h}" rx="8" fill="#141c22" opacity="0.45"/>`,
    `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="8" fill="#fbfbfa"/>`,
    `<path d="M${x} ${y + 8} a8 8 0 0 1 8 -8 h${w - 16} a8 8 0 0 1 8 8 v20 h-${w} z" fill="#e9ecef"/>`,
    text(x + 12, y + 18, title, { size: 10.5, fill: "#5a6570", font: SANS, weight: 600 }),
  ].join("\n");
}

// The run, from the lessons before it: {code, printed, effects, ending, files}.
export function cardRun(shale, lessons) {
  shale.clear();
  for (let i = 0; i < LESSON; i++) for (const c of lessons[i].cells) shale.run(c.code);
  for (let j = 0; j < CELL; j++) shale.run(lessons[LESSON].cells[j].code);
  const code = lessons[LESSON].cells[CELL].code;
  const r = shale.run(code);
  const lines = r.report.split("\n").filter((l) => l);
  return {
    code,
    title: lessons[LESSON].title,
    changed: r.output.split("\n").filter((l) => /^[-+][^-+]/.test(l)),
    effects: lines.filter((l) => !l.startsWith("── ")),
    ending: lines.at(-1).replace(/^── /, ""),
    files: shale.list(),
  };
}

export function cardSvg(shale, lessons) {
  const run = cardRun(shale, lessons);
  const out = [];
  out.push(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-label="Shale: a tutorial script renames a tag across a blog's posts; beside it, the lines it changed and the files, two marked changed">`);
  out.push(`<!-- Drawn by shale's web/card.js from the tutorial's own lesson ${LESSON + 1}, run through the wasm build: the script, its diff's changed lines, its report, and the blog's files after it. Do not edit by hand: node web/make_card.mjs. -->`);

  // The ground: shale's layers, darker toward the bottom.
  out.push(`<rect width="${W}" height="${H}" fill="#24303a"/>`);
  const bands = [34, 22, 46, 18, 30, 52, 26, 40, 20, 36, 48, 48];
  let y = 0;
  bands.forEach((h, i) => {
    const shade = ["#2b3843", "#26323c", "#2f3d48", "#232e37"][i % 4];
    out.push(`<rect y="${y}" width="${W}" height="${h}" fill="${shade}"/>`);
    y += h;
  });

  // The script, coloured as the tutorial colours it.
  const codeLines = run.code.split("\n");
  const top = { x: 24, y: 22, w: 552, h: 44 + codeLines.length * 17 };
  out.push(card(top.x, top.y, top.w, top.h, `Lesson ${LESSON + 1} · ${run.title}`));
  out.push(`<rect x="${top.x + top.w - 58}" y="${top.y + 5}" width="46" height="18" rx="4" fill="#a55a17"/>`);
  out.push(text(top.x + top.w - 47, top.y + 18, "Run", { size: 10.5, fill: "#ffffff", font: SANS, weight: 600 }));
  codeLines.forEach((line, i) => {
    const spans = tokens(line)
      .map((t) => `<tspan fill="${INK[t.kind]}"${t.kind === "key" ? ' font-weight="500"' : ""}>${esc(t.text)}</tspan>`)
      .join("");
    out.push(`<text x="${top.x + 14}" y="${top.y + 48 + i * 17}" font-family="${MONO}" font-size="11.5" xml:space="preserve">${spans}</text>`);
  });

  // What it did.
  const below = top.y + top.h + 16;
  const left = { x: 24, y: below, w: 316, h: H - below - 22 };
  out.push(card(left.x, left.y, left.w, left.h, "What it did"));
  let ly = left.y + 46;
  for (const l of run.changed) {
    out.push(text(left.x + 14, ly, l, { size: 11, fill: l.startsWith("+") ? "#2b7149" : "#b0261c" }));
    ly += 16;
  }
  ly += 6;
  for (const l of run.effects) {
    out.push(text(left.x + 14, ly, l, { size: 10.5, fill: "#5a6570" }));
    ly += 15;
  }
  out.push(`<rect x="${left.x + 14}" y="${ly - 3}" width="3" height="16" fill="#2b7149"/>`);
  out.push(text(left.x + 24, ly + 9, run.ending, { size: 10.5, fill: "#2b7149" }));

  // The blog's files.
  const right = { x: 356, y: below, w: 220, h: left.h };
  out.push(card(right.x, right.y, right.w, right.h, "The project"));
  const changed = new Set(run.effects.map((l) => l.slice(l.indexOf(" ") + 1, l.lastIndexOf(": "))));
  let ry = right.y + 46;
  for (const e of run.files) {
    const depth = e.path.split("/").length - 1;
    const name = e.path.split("/").at(-1) + (e.dir ? "/" : "");
    const hit = changed.has(e.path);
    out.push(text(right.x + 14 + depth * 12, ry, name, { size: 11, fill: e.dir ? "#5a6570" : "#1c2228", weight: hit ? 500 : 400 }));
    if (hit) out.push(`<circle cx="${right.x + right.w - 16}" cy="${ry - 4}" r="4" fill="#a55a17"/>`);
    ry += 17;
  }
  out.push(text(right.x + 14, right.y + right.h - 12, "● changed by the run", { size: 10, fill: "#a55a17", font: SANS }));

  out.push("</svg>");
  return out.join("\n") + "\n";
}
