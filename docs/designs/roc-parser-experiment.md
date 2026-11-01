# Shale's Roc recipes on roc-parser: an experiment

**The question:** could Shale's Roc recipes read their tree from roc-parser
(github.com/lukewilliamboswell/roc-parser, its `RocSyntax` module: a
pure-Roc parser of Roc with lossless tokens, byte spans and error recovery)
instead of roc-ast (the compiler's own parser), and what would each recipe
gain or lose? Nothing here replaces roc-ast or changes a recipe's default.

## What was built

- **`tools/roc_parser_flat/main.roc`**, an app on basic-cli 0.25.0, takes
  `.roc` paths on stdin and writes each file's roc-parser tree in roc-ast's
  `--flat` shape. The header line is `roc-parser flat 1 3.0.0-3-gbafa803`.
  roc-parser stores no strings, so names are read from the source by span
  (`raw`, `method`, `field`, `op`, `name`).
  - **The mapping is a thin layer in the emitter**: the compiler's kinds
    where the recipes read them, each node's own roc-parser kind kept as
    its `rp` attribute, and a kind with no match written `rp-<Kind>`.
  - **Built** by `tools/roc_parser_flat/install.shale`, with roc-parser
    checked out beside Shale and Roc at roc-ast's pin.
- **`roc_tree` reads either.**
  - The host's reader (`shale_host_roc.zig`) accepts both headers, with a
    test for each.
  - The Linux prelude's `roc_tree` takes the run's `parser=roc-parser`
    argument; roc-ast stays the default.
  - roc-parser's output is about twice roc-ast's (the `rp` lines), so a run
    of more than 200 files goes 200 at a time to stay under `run`'s
    64 MB.

**How it was run.** This container has no roc-ast that writes `--flat`.
Building one needs two GitHub archives that this session's network policy
refuses (lukewilliamboswell/bytebox and allyourcodebase/zstd). So the
baseline is Shale at d1fe92a, whose host reads the installed roc-ast's
printed tree into the same nodes.

Both sides ran the same recipe files from this branch. The baseline's
`methods.shale` had its `call_depth=1000` budget removed, because that
older binary does not know the budget; nothing else differed.

## The two recipes, old vs new

| Recipe | Repo | roc-ast | roc-parser | Difference |
|---|---|---|---|---|
| nest_depth | roc-chat (59 files) | 90 nested | 90 | none, line for line |
| nest_depth | roc-markdown (20) | 15 | 15 | none |
| nest_depth | roc-webbox (98) | 92 | 92 | one line: a file both refuse, worded differently |
| nest_depth | dog-park (21) | 29 | 29 | none |
| nest_depth | roc-apps (1,452) | 5,327 | 5,327 | none, line for line |
| nest_depth | self-test (plants) | PASS | PASS | none |
| methods (dry run) | roc-markdown package/ | 547 candidates, 2 rewritten | the same | none, line for line |
| methods (dry run) | roc-chat app/, files 1–28 | 1,479 candidates, 63 rewritten | the same | none, line for line |
| methods (dry run) | roc-chat app/, files 29–53 | 1,099 candidates, 5 rewritten | the same | `Topics.roc`'s reasons differ (below) |
| methods | self-test (plants) | every plant as it says | the same | none |

- **roc-webbox's one line** is `docs/roc-chat-probes/p7/neg/oldsyntax.roc`,
  which both parsers refuse. roc-ast says "4 parse problem(s)"; roc-parser
  says `Recovered: UnexpectedToken at 6:13`. Same verdict, different words.
- **roc-chat's `Topics.roc`** is the one real difference. **It is a bug in
  `methods.shale` that roc-ast's tree exposes and roc-parser's does not.**
  - The candidate `JsonOut.string(match topic.kind() {...})` has a `match`
    as its first argument. The compiler's tree gives `e-match` no span.
  - `emit_node` writes the parenthesised receiver from the node's span:
    `{from = 0, to = 0}`, so `sub(text, 0, -1)`, which is the whole file.
  - The trial text then holds the file twice. `roc check` fails on that
    garbage ("unused variable at 631"), and the file is put back.
  - roc-parser spans the `match`, so its trial is the right text,
    `(match topic.kind() {...}).string()`. `roc check` refuses that for a
    real reason ("missing method at 482"), and the file is put back too.
  - Every call in the file is left either way; only the reasons differ.
  - **The fix belongs in `methods.shale` whichever parser it reads:** a
    first argument with no span takes its children's extent, as `spanned`
    already does elsewhere.

## Where the mapping has to guess

- **A call's callee `a.b(...)`.** roc-parser has only
  `Call(FieldAccess ...)`.
  - When the receiver is a chain of capitalised names (`Str.concat`,
    `Principal.HumanId.from_u64`), the node is an `e-apply` of an
    `e-ident` whose `raw` is the whole path, as in the compiler's tree.
  - Any other receiver makes an `e-method-call` with `receiver` and `args`
    wrappers.
  - **The guess:** a capitalised name is a module or a type, never a value.
    It held on every file here: methods' candidate lists agree exactly. But
    it is a convention of the language's style, not something the parser
    knows.
- **A capitalised identifier is a tag** (`e-tag`), and a path ending in one
  (`Principal.Id.Human`) is a qualified tag. roc-parser has no tag kind.
- **`Rules.poop.x`** (a module's value, then a field) is an access whose
  receiver is the qualified `Rules.poop`. My first emitter got this wrong,
  making the receiver `Rules` with fields `poop.x`, until the tree
  comparison caught it in dog-park.
- **`a.b.c` is one `e-field-access`** with a segment per name, as the
  compiler writes it; roc-parser nests one access per dot. `t.0` becomes
  `e-tuple-access`.
- **Parentheses become a one-item `e-tuple`**, and statement and
  interpolation wrappers are dropped, as the compiler writes them.
- **Not mapped** (no recipe here reads them):
  - literals in patterns (`p-int`, `p-string`, `p-single-quote`), which
    need a pattern context the emitter does not track;
  - statement versus expression forms of `crash`, `dbg` and `return`;
  - nominal patterns (`p-tag`);
  - the header's pieces (`app`, `packages`, `provides`, `exposes`);
  - `where` methods.

  Each of these is written `rp-<Kind>`, or with the nearest expression kind.

## Every place roc-parser's spans disagree with the compiler's

The comparison covered every `.roc` file in roc-chat, roc-markdown,
roc-webbox, dog-park, roc-apps and Shale's plants: 1,200,711 nodes with a
span in the compiler's trees. For each compiler span missing from
roc-parser's tree, the nearest roc-parser node was found by shared start or
end.

1. **A function type next to a `(`: all 311 `ty-fn` disagreements.**
   - roc-parser starts the function type one byte earlier, at the `(`; the
     end always agrees.
   - The parenthesised case LC named: `f : (U8 -> Bool) -> U64` has the
     inner `TypeFunction` at 16..27 (1-based columns 16–27, starting at
     `(`), where the compiler says 17..27.
   - Two multi-line cases are the same thing: the `(` sits at the end of
     the line before.
   - The zero-argument function type `() -> World` too. There the compiler
     starts at the `)`, which is arguably its own oddity.
   - **This is a roc-parser span bug worth reporting upstream.**
2. **The compiler has spans roc-parser has no node for.** None of these
   matters to a recipe.
   - Zero-width `e-string-part`s at each interpolation's edges: 1,201.
   - `type-mod`, the type module's marker: 605.
   - Header pieces (`provides`, `exposes`): 730.
   - The `file` node: the compiler's runs from the first token after the
     header's doc comment to the last token; roc-parser's covers the whole
     source.
3. **roc-parser has spans the compiler lacks.**
   - Every `match`: 3,592. The compiler's `e-match` has no span, and that
     gap is what caused the `methods.shale` bug above.
   - `_` patterns: 2,590.
   - Names that the compiler keeps as attributes and roc-parser keeps as
     nodes: a declaration's name in its annotation, record and import
     names, 37,408 `e-ident`s.
   - The parenthesised type itself (`TypeParenthesized`), and the
     platform-header and import-path nodes.

Apart from item 1, every expression node a recipe reads agreed in kind,
span and `raw`.

## Gains and losses, by recipe

**`nest_depth`: it could move today.**
- **Gains:**
  - no compiler checkout or build;
  - a pure-Roc tool on basic-cli;
  - roc-parser's recovery would let it hunt a file the compiler refuses,
    if the emitter wrote recovered trees (it now refuses them, as roc-ast
    does).
- **Losses:**
  - the capitalisation guess;
  - about twice the bytes per run, most of it the diagnostic `rp` lines,
    which a production emitter could drop;
  - a second parser to pin (roc-parser's version) beside the Roc it
    judges.

**`methods`: it could move, with care.**
- **Gains:** spanned `match` (and `_`) nodes, which remove the bug above.
- **Losses:** the module-versus-method decision becomes the emitter's
  capitalisation guess instead of the parser's fact (`e-method-call`).
  - The compiler judges every rewrite anyway (`roc check`, `roc test`), so
    a wrong guess costs a trial, never a wrong rewrite.
  - The candidate lists agreed exactly on 2,578 calls here.

**The other recipes** (`errors`, `name_hunt`, `boundaries`, `stack`): not
run, as LC asked.
- They read type annotations and names more than expressions.
- Type annotations are where the parentheses span bug lives.
- A name that the compiler keeps as an attribute is a node in roc-parser's
  tree, so the emitter's mapping would have to fold those names back into
  attributes.

## Recommendation

**Yes for `nest_depth`, and yes for `methods` once the span fix below is
in. Keep roc-ast for the type-reading recipes until roc-parser's
parenthesised function type is fixed upstream.**

The cost:
- the emitter (about 500 lines of Roc) and its pin;
- the mapping kept in step with both parsers;
- one more dependency to build, basic-cli 0.25.0;
- roc-parser's checkout beside Shale, or its release URL in the app's
  header.

Before any of that, two fixes stand on their own:
- **`methods.shale`:** a first argument with no span (the compiler's
  `e-match`) must not be written from offset 0.
- **roc-parser:** a `TypeFunction` inside parentheses starts at the `(`.
