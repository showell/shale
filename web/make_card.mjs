// Writes web/card.svg (web/card.js says what it is). Run after a change to
// the lessons or the picture: node web/make_card.mjs <shale.wasm>
// (zig build web puts one in zig-out/web/).

import { readFileSync, writeFileSync } from "node:fs";
import { loadShale } from "./shale.js";
import { lessons } from "./lessons.js";
import { cardSvg } from "./card.js";

const shale = await loadShale(readFileSync(process.argv[2] ?? new URL("../zig-out/web/shale.wasm", import.meta.url)));
const out = new URL("./card.svg", import.meta.url);
writeFileSync(out, cardSvg(shale, lessons));
console.log(`wrote ${out.pathname}`);
