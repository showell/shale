// align_hunt plant: arguments sorted by alignment and laid out, one more copy of the rule.
fn byAlignment(_: void, lhs: Arg, rhs: Arg) bool {
    return lhs.alignment > rhs.alignment;
}
fn entrypointParamSlotSize(layout_idx: u32) u32 {
    return layout_idx;
}
