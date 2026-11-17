// host_call.c: the Linux host's `host(name, ...)` calls, Zig Shale's
// src/shale_host_linux.zig `call`: `run` and `sleep` (host_run.c), the jobs
// (host_jobs.c), `real`, the Zig tools (host_zig.c), `re_lines` and
// `shale_tokens` (host_text.c), `json_read` and `json_write` (host_json.c),
// and `may_change` and `edits_write`, which `edits_all` calls. `procs`,
// `proc_stop`, `roc_tree` and `go_tree` are refused as not ported yet.
//
// Include paths.c, builtins_change.c, host_run.c, host_jobs.c, host_json.c,
// host_text.c and host_zig.c first.

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
        TRY(paths_os_path(h, &builtin_store_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, true, &os));
        value_string(out, os);
        return OK;
    }
    if (str_eq(name, S("re_lines"))) return host_re_lines(r, pos, args, out);
    if (str_eq(name, S("shale_tokens"))) return host_shale_tokens(r, pos, args, out);
    if (str_eq(name, S("json_read"))) return host_json_read(r, pos, args, out);
    if (str_eq(name, S("json_write"))) return host_json_write(r, pos, args, out);
    if (str_eq(name, S("edits_write"))) {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`edits_write` takes one list, given {d}", S("`edits_write` takes one list, given "), run_unum(r, args.len));
        return change_edits_write(r, pos, &at(args, 0), out);
    }
    if (str_eq(name, S("may_change"))) {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`may_change` takes one path, given {d}", S("`may_change` takes one path, given "), run_unum(r, args.len));
        StorePath p;
        TRY(builtin_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
        TRY(change_may(r, pos, &p));
        value_bool(out, true);
        return OK;
    }
    bool zig = false;
    Status zs = host_zig_call(h, r, pos, name, args, out, &zig);
    if (zig) return zs;
    // Zig's host has these and the C does not run them yet; any other name
    // is none of Zig's host's either.
    bool zigs = str_eq(name, S("procs")) || str_eq(name, S("proc_stop")) || str_eq(name, S("roc_tree")) || str_eq(name, S("go_tree"));
    if (!zigs) return run_failf(r, pos, CLASS_HOST, "`{s}`: not on this host", S("`"), name, S("`: not on this host"));
    Msg m;
    msg_init(&m, r->ctx.values);
    msg_str(&m, S("host(\""));
    msg_str(&m, name);
    msg_str(&m, S("\")"));
    return builtin_not_ported(r, pos, m.s == OK ? buf_view(&m.b) : S("host"));
}
