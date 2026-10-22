// The tree the page starts with: a small Python project, and Shale scripts
// that work on it. web/smoke_test.mjs runs every script here.

export const sample = {
  "README.md": `# weather

A command-line forecast: \`weather Boston\` prints the next three days.

    pip install -e .
    weather Boston --days 3

The forecast comes from the Open-Meteo API; no key is needed.
`,

  "src/weather/__init__.py": `from .fetch import fetch_forecast
from .format import format_day

__all__ = ["fetch_forecast", "format_day"]
`,

  "src/weather/fetch.py": `import json
import urllib.request

API = "https://api.open-meteo.com/v1/forecast"


def fetch_forecast(lat, lon, days=3):
    # TODO: retry once on a timeout before giving up
    url = f"{API}?latitude={lat}&longitude={lon}&daily=temperature_2m_max,temperature_2m_min&forecast_days={days}"
    with urllib.request.urlopen(url, timeout=10) as reply:
        return json.load(reply)["daily"]
`,

  "src/weather/format.py": `def format_day(date, high, low):
    # TODO: Fahrenheit, behind a --units flag
    return f"{date}  high {high:.0f}°C  low {low:.0f}°C"


def format_forecast(daily):
    return "\\n".join(
        format_day(d, hi, lo)
        for d, hi, lo in zip(daily["time"], daily["temperature_2m_max"], daily["temperature_2m_min"])
    )
`,

  "src/weather/cli.py": `import argparse

from .fetch import fetch_forecast
from .format import format_forecast

CITIES = {"Boston": (42.36, -71.06), "Denver": (39.74, -104.99), "Seattle": (47.61, -122.33)}


def main():
    parser = argparse.ArgumentParser(prog="weather")
    parser.add_argument("city", choices=sorted(CITIES))
    parser.add_argument("--days", type=int, default=3)
    args = parser.parse_args()
    lat, lon = CITIES[args.city]
    # TODO: say which cities there are when the name is unknown
    print(format_forecast(fetch_forecast(lat, lon, args.days)))
`,

  "tests/test_format.py": `from weather.format import format_day, format_forecast


def test_format_day():
    assert format_day("2026-10-22", 14.4, 6.6) == "2026-10-22  high 14°C  low 7°C"


def test_format_forecast():
    daily = {"time": ["a", "b"], "temperature_2m_max": [1, 2], "temperature_2m_min": [0, 1]}
    assert format_forecast(daily).count("\\n") == 1
`,

  "scripts/todos.shale": `-- Every TODO in the project, with its file and line, printed
-- and kept in notes/todos.txt.

local report = {}
for e in walk_all("", {kind = "file"}) do
  if not starts_with(e.path, "scripts/") and not starts_with(e.path, "notes/") then
    for hit in grep(e.path, "TODO") do
      push(report, e.path .. ":" .. hit.line .. "  " .. trim(hit.text))
    end
  end
end

for line in report do print(line) end
replace("notes/todos.txt", join(report, "\\n") .. "\\n")
print(#report .. " TODOs, kept in notes/todos.txt")
`,

  "scripts/rename.shale": `-- Renames a name across the project, whole words only: \`fetch\`
-- inside \`fetch_forecast\` is left alone. Each change prints its diff.
-- Give other names in Args, e.g.  from=format_day to=render_day

const FROM = arg("from", "fetch_forecast")
const TO = arg("to", "load_forecast")

local changed = 0
for e in walk_all("", {kind = "file"}) do
  if ends_with(e.path, ".py") and count_in(e.path, FROM) > 0 then
    changed = changed + edit(e.path, FROM, TO, {all = true, word = true})
  end
end
print(FROM .. " -> " .. TO .. ": " .. changed .. " replaced")
`,

  "scripts/sizes.shale": `-- The project's files, largest first, and the total.

local rows = {}
local total = 0
for e in walk_all("", {kind = "file"}) do
  push(rows, {size = e.size, file = e.path})
  total = total + e.size
end
sort(rows, {{key = "size", desc = true}, "file"})
print(table(rows, {"size", "file"}))
print()
print(#rows .. " files, " .. human(total))
`,

  "scripts/runaway.shale": `-- A loop that never ends. Every run has budgets (steps, reads,
-- bytes, changes), so Shale stops it and says where.

local n = 0
while true do
  n = n + 1
end
`,
};

// Which script the page opens first.
export const first = "scripts/todos.shale";
