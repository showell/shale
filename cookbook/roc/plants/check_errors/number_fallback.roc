# A plant for errors' `number_fallback`: an index past the end is a bug, as
# `spawn_drops` in rocco-engine's entity game shows.
delay_of = |delays, slot| List.get(delays, slot) ?? 0.0
