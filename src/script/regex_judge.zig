//! The regex judges' corpus and patterns, for the tests that hold a builtin
//! to the Shale it replaced (`re_find_all` here, the Linux host's `re_lines`):
//! `corpus(n, seed)` joins `n` fragments a fixed generator picks from `seed`,
//! and `regex_patterns` holds one pattern a line, the empty one among them.

pub const corpus =
    \\const regex_patterns = [==[a
    \\ab|cd
    \\\[([^\]]*)\]\(([^)]*)\)
    \\`([^`]+)`
    \\\bsteps?\b
    \\(\w+)=(\d+)
    \\^#+ (.*)$
    \\^
    \\$
    \\\b
    \\x*
    \\
    \\(a)(b)?(c)?
    \\[A-Z][a-z]+
    \\(?:ab)+
    \\\s+
    \\[^ \n]+$
    \\(a|ab)(c|bcd)?
    \\.
    \\\B[a-z]
    \\é
    \\^$|^\t
    \\(x*)*y|\d$]==]
    \\function corpus(n, seed)
    \\  local frags = {"word", "Word", "WORD", " ", " ", "\n", "\r\n", "\n\n", "[link](to.md)", "[a]", "(b)",
    \\    "`code`", "``", "`", "step", "Steps", "misstep", "a", "b", "c", "ab", "abc", "bcd", "123", "x=1",
    \\    "yy=22", "#", "## Head", "(", ")", "[", "]", "\t", "café", "xxy", "y", "STEP"}
    \\  local x = seed
    \\  local parts = {}
    \\  for i = 1, n do
    \\    x = (x * 1103515245 + 12345) % 2147483648
    \\    push(parts, frags[(x // 65536) % #frags + 1])
    \\  end
    \\  return join(parts, "")
    \\end
    \\
;
