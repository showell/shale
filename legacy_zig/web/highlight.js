// Shale source as coloured HTML (tutorial.html) or as runs a caller draws
// (make_card.mjs's SVG): comments, strings, numbers, keywords and calls.

const KEYWORDS = new Set("and break const continue do else elseif end false for function if in local nil not or return then true while".split(" "));
const TOKEN = /(--\[(=+)\[[\s\S]*?\]\2\]|--[^\n]*)|(\[(=+)\[[\s\S]*?\]\4\]|"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?)|(\b\d+\b)|([A-Za-z_]\w*)(\s*\()?/g;

export function escapeHtml(s) {
  return s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
}

// `src` as [{text, kind}], kind one of comment, str, num, key, call, or ""
// for the rest; the texts joined are `src`.
export function tokens(src) {
  const out = [];
  let last = 0;
  const plain = (text) => text && out.push({ text, kind: "" });
  for (const m of src.matchAll(TOKEN)) {
    plain(src.slice(last, m.index));
    last = m.index + m[0].length;
    if (m[1]) out.push({ text: m[0], kind: "comment" });
    else if (m[3]) out.push({ text: m[0], kind: "str" });
    else if (m[5]) out.push({ text: m[0], kind: "num" });
    else if (KEYWORDS.has(m[6])) {
      out.push({ text: m[6], kind: "key" });
      plain(m[7] ?? "");
    } else if (m[7]) {
      out.push({ text: m[6], kind: "call" });
      plain(m[7]);
    } else plain(m[6]);
  }
  plain(src.slice(last));
  return out;
}

// `src` as HTML, each token in a `hl-<kind>` span. A last newline is added,
// so a last empty line takes its height as a textarea's does.
export function highlight(src) {
  return tokens(src).map((t) => (t.kind ? `<span class="hl-${t.kind}">${escapeHtml(t.text)}</span>` : escapeHtml(t.text))).join("") + "\n";
}
