# Porting Shale's host

Shale's host runs on Linux alone today. This page says which modules are
bound to Linux and why, what a port to another kernel (a Mac's, say) would
replace, and what already runs anywhere. It describes the code as it is;
it is not a design for any port.

## What is already portable

**The language is portable by construction.** The `shale` module
(`src/shale.zig`) is the lexer, parser, compiler, VM, builtins, budgets,
the signed script route and the installed suite. Every effect it makes goes
through webbox's `store`, and its `Io` comes from webbox's `host_io`
module, so the same sources run on Linux, on metal (zig-metal-unikernel)
and in a browser (`src/shale_wasm.zig`, over `src/wasm_io.zig`).

**Several of the Linux host's own files are portable too**, since they make
no system call:

- `src/shale_keep.zig`: what `run` keeps of a program's output, from bytes.
- `src/shale_job_record.zig`: a job's record, `<log>.job`, as plain text.
- `src/shale_host_json.zig`, `src/shale_host_regex.zig`,
  `src/shale_host_tokens.zig` and `src/shale_host_flat_tree.zig`: JSON,
  `re_lines`, `shale_tokens` and the reading of roc-ast's and go-ast's
  trees.
- `src/shale_host_zig.zig` and `src/shale_zig_wire.zig`: the Zig tools'
  half in the host. They check a call's arguments, read its files, ask
  zig-ast and act on its answer; they hold no Zig grammar. What reads Zig is
  zig-ast (`tools/zig_ast`), a program over `std.zig` that the host starts
  once a run, its own process (`std.process.spawn`, with pipes); a port
  starts it its own way, or builds the program for its kernel.
- `src/shale_host_setup.zig`: the mounts, the preludes and the environment
  a run's programs start from.

`src/shale_host_linux.zig` holds the host's seam (`call`, `resolve`) and
the mount-aware paths. It makes no raw system call, going through `std.Io`
instead, but it hands `run`, the jobs and the procs to the Linux-bound
files below. No port has tried it yet.

## What is bound to Linux

The calls to `std.os.linux` live in four files, gathered there so that
the files above name none:

- `src/shale_spawn.zig` runs one program under `run`. It forks by hand
  because `std.process.spawn` has no hook before exec. The child sets
  `prctl`'s death signal and takes a process group of its own; the parent
  polls the output, a pidfd for the exit (`pidfd_open`) and a signalfd for a
  TERM or HUP sent to `shale` itself, and ends what the program left
  running in its group before `run` returns.
- `src/shale_jobs.zig` runs jobs. It double-forks a reaper out of the
  caller's tree (`setsid`, `fork`, `execve` of `shale --reap`), signals a
  job's process group, and names a process by its start time from
  `/proc/<pid>/stat`, so that a reused pid is never signalled.
- `src/shale_os_linux.zig` holds the rest. Its parts are the monotonic
  clock and sleep, this process's pid and working folder, its own path
  (`/proc/self/exe`, for the reaper), the process table read from `/proc`
  (for `procs`), a pidfd's open, signal and wait (for `proc_stop`), and
  the private file `run`'s `stdout_secret` writes, renamed into place with
  `renameat2`'s `NOREPLACE`.
- `tools/shale_gate.zig`, the Claude Code hook, writes its log of escapes
  with raw `open`, `write` and `mkdir`.

`src/shale_host_run.zig`, `src/shale_host_jobs.zig`,
`src/shale_host_procs.zig` and `src/script_runner.zig` reach the kernel
only through these files. The tests of the process layer call Linux
directly too (`src/shale_fixture.zig`, the program they spawn, and
`src/shale_test_host.zig`, `src/shale_jobs_test.zig`,
`src/shale_procs_test.zig`, `src/shale_prelude_linux_test.zig`).

## What a port would replace

A port replaces the process layer, which is spawn, jobs, procs and run.
These are the Linux pieces each one leans on:

- **pidfd** is how a program's exit is polled beside its output, and how
  `proc_stop` signals a process without racing a reused pid.
- **`/proc`** gives the process table, a process's start time and parent,
  its command line and uid, and the program's own path.
- **signalfd** turns a TERM or HUP to `shale` into a poll event, so that
  `run` can end the program's group before it exits.
- **renameat2** with `NOREPLACE` stores a secret without replacing a file
  made while the program ran.
- **prctl**'s `PR_SET_PDEATHSIG` ends a child whose `shale` died.

On a Mac, kqueue is where an exit, a signal and a pipe's readiness are
waited on together. Each of the other pieces needs its own answer, found
when a port is done. The portable files above stay as they are, and the
host's callers (`src/shale_host_run.zig`, `src/shale_host_jobs.zig`,
`src/shale_host_procs.zig`) keep their shape if a port's files offer the
same functions that `src/shale_os_linux.zig`, `src/shale_spawn.zig` and
`src/shale_jobs.zig` do.
