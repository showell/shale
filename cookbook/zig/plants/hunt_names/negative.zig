// A plant for hunt_names's `negative`: `no_cache = false` reads twice.
pub fn fetchPage(no_cache: bool) u8 {
    return if (no_cache) 0 else 1;
}
