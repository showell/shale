// Quiet for `preposition_split`: the house's recursion worker, `*From`,
// resuming at an index beside the function it serves.
fn hasAt(text: []const u8, i: usize, word: []const u8) bool {
    return hasFrom(text, i, word, 0);
}

fn hasFrom(text: []const u8, i: usize, word: []const u8, k: usize) bool {
    _ = k;
    return text.len > i and word.len > 0;
}
