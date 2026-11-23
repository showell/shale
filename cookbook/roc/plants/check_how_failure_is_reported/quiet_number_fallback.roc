# Quiet for errors' `number_fallback`: a lookup that isn't by index, and a
# fallback that is no number.
total = |counts, key| Dict.get(counts, key) ?? 0

head = |numbers| List.first(numbers) ?? 0

held = |numbers, at, usual| List.get(numbers, at) ?? usual
