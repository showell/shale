// host_call.c: the Linux host's `host(name, ...)` calls, the part of Zig
// Shale's src/shale_host_linux.zig `call` the C runs: `real` (a Shale path's
// OS path), `may_change` (the hook asked of a path before `edits_all` writes
// any) and `edits_write` (`edits_all`'s write). Every other call is refused
// as not ported yet; `run`, the jobs and the rest are their own lanes.
//
// Include paths.c and builtins_change.c first.

// The `RunHost.call` of the Linux host, `ctx` its Paths.
static STATUS_MUST_USE Status host_call(void *ctx, Run *r, Pos pos, Str name, ConstValues args, Value *out) {
    Paths *h = ctx;
    // `run`'s ways to a secret, which its lane calls: named so the unity
    // build has a use of each until then.
    (void)paths_secret_path;
    (void)paths_secret_target;
    (void)paths_refuse_secret_args;
    if (str_eq(name, S("real"))) {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`real` takes one path, given {d}", S("`real` takes one path, given "), run_unum(r, args.len));
        Str given = {0};
        TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &at(args, 0), S("its path"), &given));
        Str os = {0};
        TRY(paths_os_path(h, &builtin_store_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, true, &os));
        value_string(out, os);
        return OK;
    }
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
    Msg m;
    msg_init(&m, r->ctx.values);
    msg_str(&m, S("host(\""));
    msg_str(&m, name);
    msg_str(&m, S("\")"));
    return builtin_not_ported(r, pos, m.s == OK ? buf_view(&m.b) : S("host"));
}
