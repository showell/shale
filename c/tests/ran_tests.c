// ran_tests.c: the test files that run sources as the C `shale` runs them
// (ran.c), as one binary. ran.c includes every module of c/script and
// c/host, so a file built alone would compile the whole interpreter; as
// parts of one binary (`TEST_PART`, test.h) they share one compile a build.
// The binary prints each part's count in a line of its own, as the file
// alone would, and c/build/ran.shale holds each line to the part's `TEST(`
// lines. A part's own block (c/build/vm.shale, builtins.shale,
// host_builtins.shale, host_procs.shale, host_text.shale) plants its faults
// and names the part as what catches them.

#define _GNU_SOURCE

#include "test.h"

#include "ran.c"

#include "test_vm.c"
#include "store/test_builtins.c"
#include "host_builtins/test_host_run.c"
#include "host_builtins/test_host_procs.c"
#include "host_builtins/test_run_guards.c"
#include "text/test_host_text.c"
#include "text/test_host_grep_tree.c"

TEST_PARTS_MAIN(TEST_PART_OF(vm), TEST_PART_OF(builtins), TEST_PART_OF(host_run), TEST_PART_OF(host_procs), TEST_PART_OF(run_guards), TEST_PART_OF(host_text), TEST_PART_OF(host_grep_tree))
