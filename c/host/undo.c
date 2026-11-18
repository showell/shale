// undo.c: `--undo-on-stop`'s putting back. paths.c journals each path
// before its first change (`paths_note_before`); this puts back a stopped
// run's changes, newest first: a file's bytes and mode restored, a path
// that was not there deleted (a folder with what is in it). A folder the
// run removed whole and a program's changes cannot be put back; each such
// path is reported, and the answer says not all was.
//
// Include paths.c first.

// Puts back what `h`'s journal holds, writing a line to `w` for each path it
// could not put back and one for the whole, into `*all` whether everything
// was. STOP only when `w`'s arena refuses a line.
// DRAGON: the undo journal. This runs after a stop, deleting and writing
// absolute paths outside any check, so an entry made from anything else
// would delete or overwrite whatever it names.
// REMEDY: only paths_note_before pushes onto the journal, after may_change
// has confined the path; test_paths plants a file, a folder and nothing
// there.
static STATUS_MUST_USE Status undo_changes(Paths *h, Buf *w, bool *all) {
    Arena *a = h->arena;
    *all = true;
    for (size_t k = h->journal.len; k > 0; k--) {
        const Prior *p = &at(h->journal, k - 1);
        // A journaled path is a CheckedPath's, made absolute.
        OsStr abs = os_str_own(p->abs);
        if (p->was == PRIOR_ABSENT) {
            FileSystemErr e = file_system_delete_file(abs);
            if (e == FILE_SYSTEM_IS_DIR) {
                FileSystemErr de = file_system_delete_tree(a, abs);
                if (de != FILE_SYSTEM_OK) {
                    TRY(message_format(w, S("── not put back: {s}: the folder the run made ({t})\n"), MSG_ARGS(MSG_FROM_HOST(p->abs), MSG_FROM_HOST(file_system_err_name(de)))));
                    *all = false;
                }
            } else if (e != FILE_SYSTEM_OK && e != FILE_SYSTEM_FILE_NOT_FOUND) {
                TRY(message_format(w, S("── not put back: {s}: what the run made ({t})\n"), MSG_ARGS(MSG_FROM_HOST(p->abs), MSG_FROM_HOST(file_system_err_name(e)))));
                *all = false;
            }
        } else if (p->was == PRIOR_FILE) {
            FileSystemErr e = file_system_write_file(abs, p->bytes, p->mode);
            if (e != FILE_SYSTEM_OK) {
                TRY(message_format(w, S("── not put back: {s} ({t})\n"), MSG_ARGS(MSG_FROM_HOST(p->abs), MSG_FROM_HOST(file_system_err_name(e)))));
                *all = false;
            }
        } else {
            struct stat st;
            FileSystemErr e = file_system_stat_exact(abs, false, &st);
            if (e == FILE_SYSTEM_FILE_NOT_FOUND) {
                TRY(message_format(w, S("── not put back: {s}: a folder removed whole, and its files with it\n"), MSG_ARGS(MSG_FROM_HOST(p->abs))));
                *all = false;
            } else if (e != FILE_SYSTEM_OK) {
                TRY(message_format(w, S("── not put back: {s}: the folder cannot be looked at ({t})\n"), MSG_ARGS(MSG_FROM_HOST(p->abs), MSG_FROM_HOST(file_system_err_name(e)))));
                *all = false;
            }
        }
    }
    MsgNum n;
    return message_format(w, S("── undone: {d} path(s) the run changed put back{s}\n"), MSG_ARGS(MSG_FROM_HOST(message_num(&n, h->journal.len)), MSG_FROM_HOST(*all ? S("") : S(", but not all (above)"))));
}
