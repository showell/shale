// host_call.c: the Linux host's `host(name, ...)` calls, each sent to its
// file: `run` and `sleep` (host_run.c, on host.c, host_refusals.c,
// host_write_gate.c and host_run_secrets.c), the jobs (host_jobs.c), `real` and
// `copy_mode` (here), the Zig tools (host_zig.c), `re_lines` and
// `shale_tokens` (host_text.c),
// `grep_tree` (host_grep_tree.c), `roc_tree` and `go_tree` (flat_tree.c),
// `json_read` and `json_write` (host_json.c), and `may_change` and
// `edits_write`, which `edits_all` calls, and `procs` and `proc_stop`
// (host_procs.c).
//
// Include paths.c, builtins_change.c, host_run.c (and the four before it),
// host_jobs.c, host_procs.c, host_json.c, host_text.c, host_grep_tree.c,
// flat_tree.c and host_zig.c first.

// `copy_mode(from, to)`, which the prelude's `copy` calls after each file it
// writes: `to` given `from`'s permission bits (0777's, never setuid's), so an
// executable copied stays one. `from` is a read; `to` is asked of the host
// as every change is (a read-only mount refuses it, `--undo-on-stop`
// journals it), and the write it follows was the change counted. Gives true.
static STATUS_MUST_USE Status host_copy_mode(Host *lh, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 2) return run_failf(r, pos, CLASS_ARGUMENT, "`copy_mode` takes two paths, given {d}", S("`copy_mode` takes two paths, given "), run_unum(r, args.len));
    PathRun *pr = &builtin_files_of(r)->path;
    CheckedPath from;
    CheckedPath to;
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &from));
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 1), false, &to));
    TRY(change_may(r, pos, &to));
    Str from_abs = S("");
    Str to_abs = S("");
    TRY(paths_absolute(&lh->paths, pr, pos, mounts_trim_slash(from.full.s), &from_abs));
    TRY(paths_absolute(&lh->paths, pr, pos, mounts_trim_slash(to.full.s), &to_abs));
    TRY(builtin_count_read(r, pos));
    struct stat st;
    // Both CheckedPaths', made absolute.
    FileSystemErr e = file_system_stat_exact(os_str_own(from_abs), false, &st);
    FileSystemCPath cp;
    const char *c = NULL;
    if (e == FILE_SYSTEM_OK) e = file_system_cstr(&cp, os_str_own(to_abs), &c);
    if (e == FILE_SYSTEM_OK && chmod(c, st.st_mode & 0777) != 0) e = file_system_err_of(errno);
    if (e != FILE_SYSTEM_OK) {
        return run_failf(r, pos, CLASS_FILE_SYSTEM, "`copy`: {s}'s mode could not be given to {s}: {s}", S("`copy`: "), from.rel, S("'s mode could not be given to "), to.rel, S(": "), file_system_err_name(e));
    }
    value_bool(out, true);
    return OK;
}

// The `RunHost.call` of the Linux host, `ctx` its Host.
static STATUS_MUST_USE Status host_call(void *ctx, Run *r, Pos pos, Str name, ConstValues args, Value *out) {
    Host *lh = ctx;
    Paths *h = &lh->paths;
    if (str_eq(name, S("run"))) return host_run_program(lh, r, pos, args, out);
    if (str_eq(name, S("sleep"))) return host_sleep(lh, r, pos, args, out);
    if (str_starts_with(name, S("job_"))) return host_jobs_call(lh, r, pos, name, args, out);
    if (str_eq(name, S("real"))) {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`real` takes one path, given {d}", S("`real` takes one path, given "), run_unum(r, args.len));
        Str given = {0};
        TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &at(args, 0), S("its path"), &given));
        Str os = {0};
        TRY(paths_os_path(h, &builtin_files_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, true, &os));
        value_string(out, os);
        return OK;
    }
    if (str_eq(name, S("copy_mode"))) return host_copy_mode(lh, r, pos, args, out);
    if (str_eq(name, S("re_lines"))) return host_re_lines(r, pos, args, out);
    if (str_eq(name, S("shale_tokens"))) return host_shale_tokens(r, pos, args, out);
    if (str_eq(name, S("grep_tree"))) return host_grep_tree(r, pos, args, out);
    if (str_eq(name, S("roc_tree"))) return host_flat_tree(r, pos, args, FLAT_ROC, out);
    if (str_eq(name, S("go_tree"))) return host_flat_tree(r, pos, args, FLAT_GO, out);
    if (str_eq(name, S("json_read"))) return host_json_read(r, pos, args, out);
    if (str_eq(name, S("json_write"))) return host_json_write(r, pos, args, out);
    if (str_eq(name, S("edits_write"))) {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`edits_write` takes one list, given {d}", S("`edits_write` takes one list, given "), run_unum(r, args.len));
        return change_edits_write(r, pos, &at(args, 0), out);
    }
    if (str_eq(name, S("may_change"))) {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`may_change` takes one path, given {d}", S("`may_change` takes one path, given "), run_unum(r, args.len));
        CheckedPath p;
        TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
        TRY(change_may(r, pos, &p));
        value_bool(out, true);
        return OK;
    }
    bool procs = false;
    Status ps = host_procs_call(lh, r, pos, name, args, out, &procs);
    if (procs) return ps;
    // zig-ast runs confined as every program does: its ruleset is made
    // before the call that may start it.
    if (host_zig_is_call(name) && !lh->zig->started) TRY(host_confine_open(lh, r, pos, name, &lh->zig->confine_fd));
    bool zig = false;
    Status zs = host_zig_call(h, lh->zig, r, pos, name, args, out, &zig);
    host_confine_close(&lh->zig->confine_fd);
    if (zig) return zs;
    return run_failf(r, pos, CLASS_HOST, "`{s}`: not on this host", S("`"), name, S("`: not on this host"));
}
