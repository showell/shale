// A comment holds .{ 1, 2, 3 } and f(a, b, c): no items.
const s = "(a, b, c) [x, y, z]";
const c = ',';
const m =
    \\ a, b, c, d
;
fn sum(xs: []const u8, ys: []const u8) usize {
    var t: usize = 0;
    for (xs, ys, 0..) |x, y, i| t += x + y + i;
    return t;
}
