# The cookbook

Each file here is a recipe: a Shale script general enough for any project.
Most you run, with their values on the launch line; a few are libraries,
which a script loads with `use("cookbook:<path>")` and then calls. A recipe
you run is named for what it does, verb first (`check_disk_space.shale`); a
library is named for what it holds (`merge_conflicts.shale`).
`check_recipe_names.shale` holds every recipe to that in C's full tier
(`shale c/build.shale full=1`).

The launches below go through the `cookbook` mount (a line `cookbook =
/ABSOLUTE/PATH/TO/shale/cookbook` in `~/.config/shale/mounts`), so they work
from any checkout; run a recipe from the root of the repo it works on. Each
recipe's header is its full documentation, and most take `self_test=1`,
which runs it over its plants. A glob or a value with a space goes on stdin
with `--args-stdin`, one `NAME=VALUE` a line.

## Playbooks: a recurring ask, start to finish

A playbook is a recipe for one thing people keep asking for. It gathers what
the work is made from, stops when the world has moved too far for its
assumptions (each stop says what changed and what to do), and prints the
steps left for the writer. `<repos>` below is the folder that holds your
checkouts, and `<lanes>` the one that holds your lanes.

- `plan_repo_essay.shale`: for "give me an essay on our local repos" (a repo
  map). It surveys the checkouts, compares them with the last map, and
  prints the age table, the README mentions and the steps to a gated note on
  the essay surface. `shale cookbook:plan_repo_essay.shale`, from the folder
  its header names
- `remove_lane.shale`: for "remove that finished lane". It runs
  `retire_checkout.shale` on the lane and goes on only at a plain CLEAR;
  then it removes the worktree (or a standalone clone's folder) and deletes
  the lane's branch, here and on origin, once it is merged (or, unmerged,
  once a tag on origin holds it, as a parked lane's does). Use it in place
  of a bare `git worktree remove`.
  `cd <lanes> && shale cookbook:remove_lane.shale dir=<lane>`
- `push_rocco_fork.shale`: for "push that branch to our Rocco fork". It
  pushes one work branch of a Rocco checkout to its `fork` remote, only when
  that remote is github.com/showell/rocco-engine; it never pushes `main` or
  `master`, never forces and never deletes. Steve allows this one command
  without asking.
  `cd <repos> && shale cookbook:push_rocco_fork.shale dir=<mount path> branch=<name>`
- `report_rocco_upstream_status.shale`: for "what of our Rocco work is
  upstream already?". It fetches Braeden's Rocco and our fork over https and
  gives each commit of a work branch one state, by patch (then by subject):
  upstream, pending in a fork `upstream/<topic>` branch, or never sent; and
  it names each `upstream/*` branch that is wholly upstream, safe to retire.
  `check=1 commits=<hash,...>` fails on any commit already upstream or
  pending: run it before bundling a new PR. Read-only, no gh.
  `cd <repos> && shale cookbook:report_rocco_upstream_status.shale dir=repos:rocco-engine` (`work=` the branch, walk/outdoor-particles unset)
- `open_pull_request.shale`: for "open a PR from a fork". It opens a pull
  request on any GitHub repository from a branch of one of our forks, with
  the fenced token `keys:github-pr-token`, which `run` hands only to `gh pr
  create`, `view` and `close`. It checks the head's owner against the remotes
  file, the branch on the fork, the title and the body file; `dry=1` checks
  it all and prints the command. Run `dry=1` first, at the top level.
  `shale cookbook:open_pull_request.shale --args-stdin` with `repo=`,
  `base=`, `head=showell:<branch>`, `title=`, `body=<path>` and `dry=1` on stdin
- `open_issue.shale`: for "file that issue upstream". It files an issue on
  one of our repositories or an upstream we file against (`roc-lang/roc`
  first: c/host/host_run_secrets.c's HOST_FENCE_ISSUE_UPSTREAMS, the one
  list, which `issue_upstreams()` reads), with the same fenced token, which
  `run` hands to `gh issue create` and `view` alone, never `edit`, `close`,
  `comment` or `gh api`. It checks the repository against those lists, the
  title and the body file; `dry=1` checks it all and prints the command.
  Run `dry=1` first, at the top level.
  `shale cookbook:open_issue.shale --args-stdin` with `repo=`, `title=`,
  `body=<path>` and `dry=1` on stdin

## Doors: every outward action, one command each

Each action that reaches past the box (a push, a publish, a deploy, a
GitHub write, a read with a key) has one door: a script that checks its
inputs, refuses by name, and runs the one command. An agent runs a door
only as its column says; nothing here is run by hand outside Shale. A
secret is named by its mount path alone; its contents never reach a
script. "Steve's yes" is his sign-off on that one action; a dry run, where a
door has one, comes first and needs none.

| Action | The command | Who runs it | Documented in |
|---|---|---|---|
| Push a branch to its upstream | `git_push(repo)` in a heredoc (`{branch}`, `{to}`, `{delete}`, `{lease}`, `{tag}`) | agent, on Steve's yes | docs/SHALE-AGENTS.md, "Git, in the git prelude" |
| Push a work branch to our Rocco fork | `cd <repos> && shale cookbook:push_rocco_fork.shale dir=<mount path> branch=<name>` | agent, standing permission (never main or master, never a force) | its header |
| Open a pull request from a fork | `shale cookbook:open_pull_request.shale --args-stdin` (`repo=`, `base=`, `head=`, `title=`, `body=`, `dry=1` first) | agent, on Steve's "open it" | its header; the fence in docs/SHALE-AGENTS.md |
| File an issue on ours or an upstream | `shale cookbook:open_issue.shale --args-stdin` (`repo=`, `title=`, `body=`, `dry=1` first) | agent, on Steve's "file it" | its header; the fence in docs/SHALE-AGENTS.md |
| Read a Zulip topic | `shale cookbook:read_zulip_topic.shale --args-stdin` (`site=`, `email=`, `key=keys:<file>`, `stream=`, `topic=`, `num=`) | agent (read-only) | its header |
| Run a workflow on GitHub (c-full) | `shale cookbook:build_on_github.shale workflow=c-full.yml ref=<branch>` | agent, on Steve's yes | its header; c/docs/building.md |
| Publish an app to roc-apps' staging | `cd <roc-apps> && shale site/publish.shale app=<app>` | agent, on Steve's sign-off of dev | roc-apps: site/publish.shale's header |
| Publish roc-js-interpreter's playground and book | `cd <roc-apps> && shale site/publish_roc_js.shale` (`dry=1` first) | agent, on Steve's sign-off | roc-apps: site/publish_roc_js.shale's header |
| Publish Safari Walk | `cd <roc-apps> && shale site/publish_safari_walk.shale` (`dry=1`: the checks alone) | agent, on Steve's sign-off | roc-apps: site/publish_safari_walk.shale's header |
| Publish roc-markdown's playground | `cd <roc-apps> && shale site/publish_markdown.shale` (`dry=1` first) | agent, on Steve's sign-off | roc-apps: site/publish_markdown.shale's header |
| Publish roc-screensaver's pages | `cd <roc-apps> && shale site/publish_screensavers.shale` (`saver=<name>` for one) | agent, on Steve's sign-off | roc-apps: site/publish_screensavers.shale's header |
| Deploy roc-apps' staging to prod | `cd <roc-apps> && shale site/deploy.shale` (reads `keys:prod-ssh`) | agent, on Steve's sign-off of staging | roc-apps: site/deploy.shale's header, docs/production.md |
| Deploy roc-chat to prod | `cd <roc-chat> && shale tools/deploy.shale` (reads `keys:prod-ssh`; on `main`) | agent, on Steve's yes | roc-chat: docs/deploy.md, tools/deploy.shale's header |
| Release the essay surface | `cd <repos> && shale repos:essay-surface/scripts/release.shale data=essays: port=<port>` (`rollback=1` to swap back) | agent, on Steve's yes | essay-surface: scripts/release.shale's header |
| Store or replace a secret | `shale --secrets` (`store`, the secret's bare name at the prompt) | Steve, in his own terminal (the gate refuses it to an agent) | docs/SHALE-AGENTS.md, "Setting up your own secrets folder" |

A door launched at the top level is the one that reads its secret: a nested
`shale` (one a script runs) reaches no secrets mount and no remotes file.

## For any project

- `build_on_github.shale`: runs a workflow on GitHub instead of locally
  (`workflow=c-full.yml` is C Shale's full tier), and reports the run.
  `shale cookbook:build_on_github.shale workflow=c-full.yml ref=my-branch`
- `check_page_links.shale`: checks every link and image on a web page,
  through its redirects, by HEAD: what answers other than 200, and what
  moved.
  `shale cookbook:check_page_links.shale url=https://roc.lynrummy.com/`
- `check_conflict_markers.shale`: finds git conflict markers left in
  files. `shale cookbook:check_conflict_markers.shale files=a.zig,docs/b.md`
- `check_confinement.shale`: whether a program can run from here, confined
  to the root and the mounts (Landlock); a FAIL is the stop that refused it,
  naming no folder. `shale cookbook:check_confinement.shale`
- `check_disk_space.shale`: stops a run when the disk is under a floor of
  free space; `report=1` sizes the folders that grow. `shale cookbook:check_disk_space.shale floor=60`
- `check_recipe_names.shale`: checks that every recipe here is named by the
  standard above; `report=1` lists each break with a suggested name.
  `shale cookbook/check_recipe_names.shale report=1`
- `check_seals.shale`: checks that a Zig sealed value is made only in its own
  file. `shale cookbook:check_seals.shale dir=src seals=id_seal:src/player.zig`
- `check_syntax.shale`: checks each file's syntax by its kind (Zig, Roc, JS,
  Shale, Python, shell). `shale cookbook:check_syntax.shale files=src/a.zig,roc/Walk.roc`
- `count_shale_stops.shale`: counts how an agent's Shale launches went, from
  a Claude Code transcript; the launcher of a Roc script
  (`count_shale_stops/`, on `roc_platform/`, its JSON read in Roc as Shale's
  `json_read` reads it, held to its goldens by that folder's
  `check_goldens.shale`). `shale cookbook:count_shale_stops.shale` (`since=`, `transcript=`, `most=`)
- `fetch_page.shale`: fetches a page and prints its status and the lines that
  matter, or waits until it says something. `shale cookbook:fetch_page.shale url=https://example.org/version`
- `find_phrase.shale`: finds a phrase in wrapped prose, across line breaks,
  and with `new=` edits it. `shale cookbook:find_phrase.shale find=settled in=docs`
- `findings.shale` (library): `print_findings(summary, lines)`, the findings
  between two copies of their summary. `use("cookbook:findings.shale")`
- `folder_names.shale` (library): when a text names a folder, as a name
  standing alone.
  `use("cookbook:folder_names.shale")`
- `listen.shale`: listens to a roc-chat conversation for the posts meant for
  one agent. `shale cookbook:listen.shale site=https://chat.lynrummy.com conv=3 me=agent:2 key=keys:<file> once=1`
- `local_main_branch.shale` (library): a remoteless repo's main branch
  (`main` or `master`, whichever it has), which `retire_checkout.shale` and
  `remove_lane.shale` judge a lane merged against when there is no origin.
  `use("cookbook:local_main_branch.shale")`
- `look.shale`: shows lines by number, and a literal's lines across folders,
  mounts or a git revision. `shale cookbook:look.shale at=src/io.zig:690-730`
- `map_repos.shale`: maps the repos in a folder: branches, commits,
  uncommitted files, upstreams, and the repos each README names.
  `cd <repos> && shale cookbook:map_repos.shale`
- `merge_conflicts.shale` (library): parses the markers git leaves in a
  conflicted file, so a resolution is a few lines; `resolve_held(path)` resolves
  held counts two branches both moved (a line ending `-- held: additive`) as
  ours + theirs − base, printing each sum, and refuses `-- held: measured`
  and unmarked ones. `use("cookbook:merge_conflicts.shale")`
- `prune_merged_branches.shale`: finds the work branches merged into the
  default branch, and with `delete=1` deletes them. `shale cookbook:prune_merged_branches.shale`
- `read_wants.shale`: reads back the log of shell escapes past the gate, with
  counts by kind, tag and session. `shale cookbook:read_wants.shale since=<date>`
- `read_zulip_topic.shale`: prints a Zulip topic's newest messages.
  `shale cookbook:read_zulip_topic.shale --args-stdin` (site, email, key, stream, topic, num)
- `remote_tags.shale` (library): asks a remote for the tags that hold a
  branch's commits on no remote branch, as a branch parked as
  `parked/<name>` and its tag pushed is, and records what it has
  (`refs/remote-tags/`); `retire_checkout.shale` counts those commits on a
  remote by the records, and `remove_lane.shale` asks first and then
  deletes such a branch.
  `use("cookbook:remote_tags.shale")`
- `rename_words.shale`: renames a word across a project's files, whole words
  only. `shale cookbook:rename_words.shale from=fetchUser to=loadUser in=src ext=ts,tsx`
- `retire_checkout.shale`: says whether a checkout can be deleted, CLEAR or
  BLOCKED with the reasons, and only reports.
  `cd <repos> && shale cookbook:retire_checkout.shale dir=<checkout>`
  (`remove_lane.shale` runs it first.)
- `run.shale`: runs a command line and prints how each program ended, its
  time and the lines that matter. `shale cookbook:run.shale --args-stdin` (cmd, grep)
- `run_zig_build.shale`: runs `zig build STEP` and prints its verdict: the
  summary, each error once, the failed tests. `shale cookbook:run_zig_build.shale step=test`
- `survey_repos.shale`: surveys a folder of checkouts for a map of them:
  an age table, each README's opening, and which READMEs name which
  folders. `cd <repos> && shale cookbook:survey_repos.shale contexts=1`
- `test_times.shale`: tests the time-history library on canned runs.
  `shale cookbook:test_times.shale self_test=1`
- `times.shale` (library): `timed_run`, a stage's time against its recent
  runs. `use("cookbook:times.shale")`
- `write_tree_patch.shale`: turns a changed copy of a folder into one patch.
  `shale cookbook:write_tree_patch.shale old=roc new=<copy>/roc under=roc out=x.patch`

## prose/: a book's prose lints

- `prose/build_term_index.shale`: builds a book's index of terms and reports
  terms used before the chapter that teaches them. `shale cookbook:prose/build_term_index.shale book=book/book.md terms=book/index_terms.txt`
- `prose/find_negatives.shale`: finds the negative sentences a book's voice
  rule rewrites or keeps. `shale cookbook:prose/find_negatives.shale manuscript=book/book.md table=book/negatives.txt`

## roc/: Roc code ([README](roc/README.md))

- `roc/find_functions_and_parameters_that_dont_earn_their_keep.shale`: asks whether each function earns its boundary
  and each parameter is really one; the launcher of a Roc script
  (`roc/find_functions_and_parameters_that_dont_earn_their_keep/`, on `roc_platform/`, held to its goldens by that
  folder's `check_goldens.shale`). `shale cookbook:roc/find_functions_and_parameters_that_dont_earn_their_keep.shale`
- `roc/check_how_failure_is_reported.shale`: checks how Roc code reports failure.
  `shale cookbook:roc/check_how_failure_is_reported.shale`
- `roc/find_names_that_differ_across_lookalike_modules.shale`: checks that look-alike modules are spelled
  alike, one type name means one thing and a `to_<noun>` has a
  `from_<noun>`, not a bare `<noun>`; the launcher of a Roc script
  (`roc/find_names_that_differ_across_lookalike_modules/`, on `roc_platform/`, held to its goldens by that
  folder's `check_goldens.shale`). `shale cookbook:roc/find_names_that_differ_across_lookalike_modules.shale`
- `roc/check_another_projects_roc_code.shale`: runs the general checks over another
  project's Roc code, without our house style. `shale cookbook:roc/check_another_projects_roc_code.shale`
- `roc/check_modules_stay_under_500_lines.shale`: holds each Roc module to a size, the whole
  file counted. `shale cookbook:roc/check_modules_stay_under_500_lines.shale`
- `roc/find_names_that_disagree_with_their_types.shale`: finds names that disagree with their types;
  the launcher of a Roc script (`roc/find_names_that_disagree_with_their_types/`, on `roc_platform/`,
  held to its goldens by that folder's `check_goldens.shale`).
  `shale cookbook:roc/find_names_that_disagree_with_their_types.shale table=name_types.txt`
- `roc/check_and_fix_group_stacking.shale`: finds groups laid out against the stacking
  rule, and with `write=1` lays them out by it; the launcher of a Roc script
  (`roc/check_and_fix_group_stacking/`, on `roc_platform/`, held to its goldens by that
  folder's `check_goldens.shale`). `shale cookbook:roc/check_and_fix_group_stacking.shale`
- `roc/find_retired_words.shale`: holds a project to the words it has decided
  on; the launcher of a Roc script (`roc/find_retired_words/`, on
  `roc_platform/`, held to its goldens by that folder's `check_goldens.shale`).
  `shale cookbook:roc/find_retired_words.shale words=vocabulary.txt`
- `roc/compare_machine_code.shale`: says whether two builds of a Roc archive
  or program hold the same machine code, procedure by procedure. `shale cookbook:roc/compare_machine_code.shale old=a.a new=b.a`
- `roc/count_roc_modules_lines_and_tests.shale`: counts a Roc project's modules, lines, test modules,
  test lines and `expect`s, for each folder given; the launcher of a
  one-file Roc script (`roc/count_roc_modules_lines_and_tests/count_roc_modules_lines_and_tests.roc`, on `roc_platform/`,
  held to its goldens by that folder's `check_goldens.shale`). `cd <repos> && shale cookbook:roc/count_roc_modules_lines_and_tests.shale in=roc-apps/basic`
- `roc/declared.shale` (library): a parse tree's kids and attributes, a
  name's words and a type's names, for the type printer and
  `rank_hints_that_a_module_does_anothers_work`. `use("cookbook:roc/declared.shale")`
- `roc/find_floats_outside_unit_types.shale`: finds bare `F64`s in declarations, against a
  table of exceptions; the launcher of a Roc script (`roc/find_floats_outside_unit_types/`,
  on `roc_platform/`, held to its goldens by that folder's
  `check_goldens.shale`). `shale cookbook:roc/find_floats_outside_unit_types.shale table=bare_floats.txt`
- `roc/find_uses_after_a_consume.shale`: finds a value read after a write took it, within
  one function, which makes the write copy; the launcher of a Roc script
  (`roc/find_uses_after_a_consume/`, on `roc_platform/`, its plants roc-apps' copy
  shapes). `shale cookbook:roc/find_uses_after_a_consume.shale glob=basic/roc/*.roc`
- `roc/find_calls_nested_three_deep.shale`: finds calls nested three or more deep; the
  launcher of a Roc script (`roc/find_calls_nested_three_deep/`, on `roc_platform/`,
  held to its goldens by that folder's `check_goldens.shale`).
  `shale cookbook:roc/find_calls_nested_three_deep.shale min=4`
- `roc/find_and_remove_extra_parentheses.shale`: finds the parentheses and braceless `if`s a
  code emitter leaves, and with `write=1` rewrites them. `shale cookbook:roc/find_and_remove_extra_parentheses.shale`
- `roc/find_names_and_docs_that_say_too_little.shale`: finds names and module docs that say too
  little; the launcher of a Roc script of several modules (its root
  `roc/find_names_and_docs_that_say_too_little/find_names_and_docs_that_say_too_little.roc`, on `roc_platform/`,
  held to its goldens by that folder's `check_goldens.shale`).
  `shale cookbook:roc/find_names_and_docs_that_say_too_little.shale`
- `roc/find_unused_imports.shale`: finds the imports a file leaves unused;
  the launcher of a Roc script (`roc/find_unused_imports/`, on
  `roc_platform/`, held to its goldens by that folder's
  `check_goldens.shale`). `shale cookbook:roc/find_unused_imports.shale`
- `roc/house.shale` (library): the checks that are our house style alone,
  by recipe. `use("cookbook:roc/house.shale")`
- `roc/find_awkward_names.shale`: hunts for awkward names; the launcher of a Roc
  script of several modules (its root `roc/find_awkward_names/find_awkward_names.roc`, on
  `roc_platform/`, held to its goldens by that folder's
  `check_goldens.shale`). `shale cookbook:roc/find_awkward_names.shale`
- `roc/member_uses.shale` (library): how the type printer resolves names and
  counts each member's uses. `use("cookbook:roc/member_uses.shale")`
- `roc/module_family.shale` (library): how the type printer walks sibling
  modules as one family. `use("cookbook:roc/module_family.shale")`
- `roc/parsed.shale` (library): parses a tree's .roc files and fails on any
  that roc-ast refuses. `use("cookbook:roc/parsed.shale")`
- `roc/pinned.shale` (library): the Roc every recipe's self-test runs.
  `use("cookbook:roc/pinned.shale")`
- `roc/print_module_types.shale`: prints a module's types and every type they
  depend on, signatures only. `shale cookbook:roc/print_module_types.shale module=Pigs`
- `roc/rank_hints_that_a_module_does_anothers_work.shale`: ranks hints that one module does
  another's work. `shale cookbook:roc/rank_hints_that_a_module_does_anothers_work.shale root=safari`
- `roc/rename_identifier_by_whole_word.shale`: renames one identifier across a tree of Roc code.
  `shale cookbook:roc/rename_identifier_by_whole_word.shale from=portion to=share module=Pie`
- `roc/rewrite_module_calls_as_method_calls.shale`: rewrites `List.map(xs, f)` calls as method
  calls, judged by the compiler; the launcher of a Roc script of several
  modules (its root `roc/rewrite_module_calls_as_method_calls/rewrite_module_calls_as_method_calls.roc`, on
  `roc_platform/`, held to its goldens by that folder's
  `check_goldens.shale`). `shale cookbook:roc/rewrite_module_calls_as_method_calls.shale apply=1`
- `roc/types.shale` (library): the roc-types to run for a Roc, and its
  output cut to some modules' records. `use("cookbook:roc/types.shale")`
- `roc/wrap_long_lines_with_trailing_commas.shale`: shortens long Roc lines by letting `roc fmt`
  lay out their groups; the launcher of a Roc script (`roc/wrap_long_lines_with_trailing_commas/`,
  on `roc_platform/`, held to its goldens by that folder's
  `check_goldens.shale`). `shale cookbook:roc/wrap_long_lines_with_trailing_commas.shale dir=roc width=100`

## c/: Shale's own C

These read Shale's own C with a line reader in place of a C parser.
c/check_discipline.shale holds Shale's C to a shape a line reader can trust
(each line at column 0 opens or closes an item, braces end their lines, one
declaration a line, `//` comments), and the build fails when a file breaks
it. So the dialect is the parser's contract: `c/items/` splits a file
into its items on it, and stops, naming the file and line, at a line it
can't account for. It is a Roc package the C finders below share: each is
written in Roc on Shale's platform (`roc_platform/`), its folder's app,
held to its Shale original's goldens by that folder's
`check_goldens.shale`. Its Shale original, a second copy of the splitting
logic, is the fast tier's, `c/build/c_items.shale` beside the cookbook,
since that tier builds no Roc.

- `c/items/` (a Roc package): a file's top-level items, each with its kind,
  name, lines, head, body and the names its body uses, and the masker that
  blanks strings and comments, for the Roc recipes, whose headers name its
  `main.roc` as `items`, then `import items.Split`
- `c/map_file.shale`: prints a file's items, one `first-last kind name` a
  line; `tree=c` splits every file and accounts for each line (in Roc).
  `shale cookbook:c/map_file.shale file=c/script/vm.c`
- `c/find_dead_code.shale`: finds the functions and generators no program
  reaches, and those only tests reach, from every item's names (in Roc).
  `shale cookbook:c/find_dead_code.shale dir=c`
- `c/list_long_functions.shale`: lists the functions past a length in lines of
  code (comments and blank lines counted apart), by default past the tree's
  95th percentile, with the distribution (in Roc). `shale cookbook:c/list_long_functions.shale over=60`
- `c/list_deep_nesting.shale`: ranks the functions by their deepest nesting of
  braced blocks (the body is depth 1; strings and comments masked), those at
  depth 4 or past, deepest first: functions to look at, for a reader (in
  Roc). `shale cookbook:c/list_deep_nesting.shale min=5`
- `c/find_clones.shale`: finds twin functions, of one shape once every name,
  number and string is masked, and runs of six or more lines alike in two
  places, each pair with its spans and a score (in Roc). `shale cookbook:c/find_clones.shale`
- `c/hunt_names.shale`: hunts for names that break c/docs/style.md's "Names"
  or say too little (in Roc).
  `shale cookbook:c/hunt_names.shale`

## go/: Go code ([README](go/README.md))

- `go/find_deep_calls.shale`: finds calls nested three or more deep.
  `shale cookbook:go/find_deep_calls.shale min=4`
- `go/find_smells.shale`: finds long functions, deep blocks, long parameter
  lists and `bool` parameters. `shale cookbook:go/find_smells.shale`
- `go/hunt_names.shale`: hunts for awkward names. `shale cookbook:go/hunt_names.shale tests=0`
- `go/parsed.shale` (library): Go's parse trees and the plants' self-test.
  `use("cookbook:go/parsed.shale")`

## zig/: Zig code ([README](zig/README.md))

- `zig/add_missing_pubs.shale`: builds, and adds the `pub`s the compiler asks
  for after a move. `shale cookbook:zig/add_missing_pubs.shale`
- `zig/check_comments_only.shale`: checks that a change touches comments
  only. `shale cookbook:zig/check_comments_only.shale rev=HEAD~1`
- `zig/check_const_unchanged.shale`: checks that a constant holds the same
  bytes after a refactor. `shale cookbook:zig/check_const_unchanged.shale path=src/x.zig name=page`
- `zig/check_same_tests.shale`: checks that a refactor kept the test suite,
  test by test. `shale cookbook:zig/check_same_tests.shale mode=baseline`
- `zig/check_stacking.shale`: finds groups of three or more items on one
  line, and with `write=1` stacks them. `shale cookbook:zig/check_stacking.shale`
- `zig/count_comments.shale`: counts how much each file's comments say, and
  how loudly; the launcher of a one-file Roc script
  (`zig/count_comments/count_comments.roc`, on `roc_platform/`, held to its
  goldens by that folder's `check_goldens.shale`). `shale cookbook:zig/count_comments.shale top=10`
- `zig/find_bool_flags.shale`: finds `bool` parameters that calls pass as bare
  literals. `shale cookbook:zig/find_bool_flags.shale`
- `zig/find_clones.shale`: finds the same code in two places.
  `shale cookbook:zig/find_clones.shale window=6`
- `zig/find_dead_code.shale`: finds declarations nothing uses.
  `shale cookbook:zig/find_dead_code.shale`
- `zig/find_repeated_params.shale`: finds runs of parameters that function
  after function declares. `shale cookbook:zig/find_repeated_params.shale min=3`
- `zig/find_seams.shale`: finds where a file divides.
  `shale cookbook:zig/find_seams.shale path=src/x.zig`
- `zig/find_smells.shale`: finds the places a tree may want a second look.
  `shale cookbook:zig/find_smells.shale`
- `zig/flip_bools.shale`: flips each bare `true` or `false` a call passes and
  runs the tests. `shale --undo-on-stop cookbook:zig/flip_bools.shale`
- `zig/hunt_alignment_math.shale`: hunts a compiler for copied alignment
  arithmetic. `shale cookbook:zig/hunt_alignment_math.shale`
- `zig/hunt_names.shale`: hunts for awkward names. `shale cookbook:zig/hunt_names.shale`
- `zig/judge_move.shale`: judges that a move of declarations kept the
  behaviour. `shale cookbook:zig/judge_move.shale mode=baseline bin=zig-out/bin/x`
- `zig/list_long_files.shale`: lists the .zig files over a thousand lines in
  the repos; the launcher of a one-file Roc script
  (`zig/list_long_files/list_long_files.roc`, on `roc_platform/`, held to its
  goldens by that folder's `check_goldens.shale`). `cd <repos> && shale cookbook:zig/list_long_files.shale`
- `zig/map_file.shale`: prints a map of a file's declarations and tests.
  `shale cookbook:zig/map_file.shale path=src/x.zig`
- `zig/plan_function_cut.shale`: says what a piece of a long function would
  need as a function of its own. `shale cookbook:zig/plan_function_cut.shale path=src/x.zig fn=run first=40 last=80`
- `zig/rename_member.shale`: renames a declaration with its callers across a
  tree. `shale cookbook:zig/rename_member.shale file=src/x.zig from=old to=new`
- `zig/rewrite_call_args.shale`: regroups every call's arguments by a
  template. `shale --undo-on-stop cookbook:zig/rewrite_call_args.shale --args-stdin` (name, template)
- `zig/split_file.shale`: moves declarations to a new file and makes the old
  one use them. `shale --undo-on-stop cookbook:zig/split_file.shale --args-stdin` (from, to, as, names, doc)
- `zig/tidy_file.shale`: tidies a file's imports and empty banners after a
  move. `shale cookbook:zig/tidy_file.shale path=src/x.zig`

## zig17/: the Zig 0.16 to 0.17 upgrade ([README](zig17/README.md))

- `zig17/count_upgrade_changes.shale`: counts what each repo uses that 0.17
  changed. `cd <repos> && shale cookbook:zig17/count_upgrade_changes.shale`
- `zig17/find_hidden_decls.shale`: finds declarations 0.17's `@hasDecl`
  misses, since it sees only `pub`. `shale cookbook:zig17/find_hidden_decls.shale dir=.`
- `zig17/rewrite_array_mult.shale`: rewrites array multiplication as
  `@splat`. `shale cookbook:zig17/rewrite_array_mult.shale dir=src`
- `zig17/rewrite_renamed_calls.shale`: rewrites the calls 0.17 renamed or
  reshaped. `shale cookbook:zig17/rewrite_renamed_calls.shale dir=src`

`words/` holds the word tables the name hunts read, and each `plants/`
folder holds a recipe's test cases.
