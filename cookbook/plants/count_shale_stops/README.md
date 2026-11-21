# count_shale_stops plants

Planted Claude Code transcripts, one JSON value a line, for
`cookbook/count_shale_stops/check_goldens.shale`. Each case's golden was
written from the Shale original's output.

- `outcomes.jsonl`: a launch of each ending: ran, stopped by Shale (twice
  with the same mistake and other quoted values, so they group), stopped by
  its own `fail`, refused by the gate, denied by the harness, a nonzero exit
  with no stop line; heredocs (`<<'EOF'`, `<<'CHECK'`) and script launches;
  a call with no answer; two calls and their answers in one line each; a
  Bash call that is no launch and a Read call, their answers left out; an
  answer whose content is a string, a list of parts (one an image, with no
  text) or empty; a stop in a prelude, one cut at 110 bytes, one with no
  words; `is_error` false, missing and null; and `\u` escapes (a surrogate
  pair, `\u+041`), numbers with a fraction or an exponent, `-0`, and a
  member that is `null` before the same key again.
- `many_calls.jsonl`: three heredocs calling 25 functions with ties, and
  `run` of five programs, for the cut at `most` and the order of ties.
- `empty.jsonl`, `quiet.jsonl`: no lines, and lines with no launch.
- `stops/`: one line each that stops the run, as it stopped the original:
  JSON that ends early, a bad escape, a raw control byte, a lone low
  surrogate, an integer past 64 bits, nesting past 64 levels, a `null` in an
  array, a key twice; a line with no `message`, a part with no `type`, a
  Bash call with no command, an answer whose content is a map.
