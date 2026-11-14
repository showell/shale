// Quiet for `preposition_split`: a matched pair, opposite prepositions on
// purpose.
fn insertBefore(text: []const u8, i: usize) usize {
    return text.len + i;
}

fn insertAfter(text: []const u8, i: usize) usize {
    return text.len - i;
}
