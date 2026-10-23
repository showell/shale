// The tutorial's lessons (web/tutorial.html). A lesson is prose and cells;
// each cell is a whole Shale script, run against the project's files. A
// lesson starts from the files the cells of every lesson before it make, as
// written here, so a reader's own edits never break a later lesson.
// web/smoke_test.mjs runs every cell, in order.
//
// Prose: `code`, **bold**; a cell with `stops` is meant to stop.

const R = String.raw;

export const lessons = [
  {
    title: "Hello, Shale",
    cells: [
      {
        say: [
          "Shale is a small language for working on a project's files: reading them, searching them, changing them. It looks like Lua.",
          "Each box below is a whole Shale script. Read it, then press **Run** (or Ctrl+Enter in the box). What it prints appears underneath.",
        ],
        code: R`-- Two dashes start a comment, to the end of the line.
print("Hello, Shale")
print("two and two make " .. 2 + 2)
print("print joins several values with tabs:", 42, true)`,
      },
      {
        say: [
          "`..` joins strings and numbers. Strings take double or single quotes.",
          "A run that ends well says **finished**, and how many steps it took. Every run has a budget of steps, which comes up again in the last lesson.",
        ],
        code: R`print('single quotes work too')
print("a line\nand another")`,
      },
    ],
  },

  {
    title: "Names and numbers",
    cells: [
      {
        say: [
          "`local` names a value. Numbers are whole numbers only: `//` divides and drops the remainder, and `%` is the remainder.",
        ],
        code: R`local posts = 3
local words = 1250
print("words a post:", words // posts)
print("left over:", words % posts)
posts = posts + 1
print("posts now:", posts)`,
      },
      {
        say: [
          "There is no `/` and there are no fractions. This cell stops, on purpose, and says why and where: the line and column of the problem.",
        ],
        code: R`local words = 1250
local posts = 3
print(words / posts)`,
        stops: true,
      },
      {
        say: ["`const` names a value that never changes. A script's constants usually sit at the top."],
        code: R`const BLOG = "Field Notes"
print("This blog is called " .. BLOG .. ", " .. #BLOG .. " letters long.")`,
      },
    ],
  },

  {
    title: "Your first file",
    cells: [
      {
        say: [
          "The project is the files on the right, empty for now. `replace` writes a file whole, making its folder if needed. `read` gives a file back as a string.",
          "Nothing carries over from one script to the next except the files. That is how Shale is meant to be used: the files are the memory.",
        ],
        code: R`replace("posts/hello.md", "Hello, world!\n")
print(read("posts/hello.md"))`,
      },
      {
        say: ["`append` adds to the end of a file. Run this one twice and look at the file on the right."],
        code: R`append("posts/hello.md", "This blog is written with Shale.\n")
print(read("posts/hello.md"))`,
      },
    ],
  },

  {
    title: "Lists and loops",
    cells: [
      {
        say: [
          "A list is written in braces. `#` is its length, and `for ... in` visits each item. Lists count from 1.",
        ],
        code: R`local titles = {"Hello, world", "Tide pools", "A walk in the fog"}
print(#titles .. " titles; the first is " .. titles[1])
for t in titles do
  print("- " .. t)
end`,
      },
      {
        say: ["`push` adds an item. `for i = 1, n` counts. `join` makes one string of a list."],
        code: R`local titles = {"Hello, world", "Tide pools", "A walk in the fog"}
push(titles, "Field notes on crows")
for i = 1, #titles do
  print(i, titles[i])
end
print(join(titles, " / "))`,
      },
    ],
  },

  {
    title: "Functions, and the blog's posts",
    cells: [
      {
        say: [
          "A function is declared with `function` and called after it. This one turns a title into a file name: lower case, with dashes for spaces.",
        ],
        code: R`function slug(title)
  local s = lower(title)
  s = substitute(s, ",", "")
  s = substitute(s, " ", "-")
  return s
end

print(slug("A walk in the fog"))
print(slug("Hello, world"))`,
      },
      {
        say: [
          "Now the blog gets its posts. Each one starts with a few `name: value` lines, then `---`, then the text. Later lessons read those lines back.",
          "A function sees its own parameters and the constants above it, never the script's other locals.",
        ],
        code: R`function slug(title)
  return substitute(substitute(lower(title), ",", ""), " ", "-")
end

function post(title, date, tags, body)
  local text = "title: " .. title .. "\ndate: " .. date .. "\ntags: " .. tags .. "\n---\n" .. body .. "\n"
  replace("posts/" .. slug(title) .. ".md", text)
  print("wrote posts/" .. slug(title) .. ".md")
end

post("Hello, world", "2026-09-01", "meta", "Field notes from short outings, mostly on foot.")
post("Tide pools", "2026-09-14", "coast, walks", "Anemones close when the water leaves. The pools at the north end hold the most, and the gulls know it. Go an hour before low tide and the rocks are still dry enough to stand on.")
post("A walk in the fog", "2026-10-02", "walks, weather", "The fog came in at noon. By two the far side of the bay was gone, and the foghorn started. The sidewalks were wet without a drop of rain.")
post("Field notes on crows", "2026-10-11", "birds", "Seven crows on the wire at dawn, the same seven as yesterday. One of them carries a bottle cap from roof to roof and never drops it. They leave together when the bus comes, and come back one at a time.")`,
      },
    ],
  },

  {
    title: "Folders",
    cells: [
      {
        say: ["`ls` lists a folder's names, sorted. `exists` and `is_dir` ask what is at a path."],
        code: R`for name in ls("posts") do
  print(name)
end
print(exists("posts/hello.md"), exists("posts/goodbye.md"), is_dir("posts"))`,
      },
      {
        say: [
          "`posts/hello.md` was the first draft; `posts/hello-world.md` replaces it. `remove` deletes a file, and `mkdir` makes a folder. `\"\"` is the project's top.",
        ],
        code: R`remove("posts/hello.md")
mkdir("drafts")
replace("drafts/owls.md", "title: Owls at dusk\ndate: 2026-10-20\ntags: birds\n---\nNot done yet.\n")
for name in ls("") do
  print(name)
end`,
      },
    ],
  },

  {
    title: "Deciding",
    cells: [
      {
        say: [
          "`if`, `elseif` and `else` choose. `stat` gives a file's size in bytes. Comparisons are `==`, `~=` (not equal), `<`, `<=`, `>` and `>=`.",
        ],
        code: R`for name in ls("posts") do
  local size = stat("posts/" .. name).size
  if size > 250 then
    print(name, size, "long")
  elseif size > 150 then
    print(name, size, "medium")
  else
    print(name, size, "short")
  end
end`,
      },
      {
        say: ["`and`, `or` and `not` combine conditions. Only `nil` and `false` count as false."],
        code: R`for name in ls("posts") do
  local text = read("posts/" .. name)
  if contains(text, "walks") and not contains(text, "fog") then
    print(name .. " is a walk without fog")
  end
end`,
      },
    ],
  },

  {
    title: "Maps",
    cells: [
      {
        say: [
          "A map holds values by name. `m.title` reads one; `keys` lists them in the order they were set. Reading a name that is not there stops the script, so `has_key` and `get` ask first.",
        ],
        code: R`local post = {title = "Tide pools", date = "2026-09-14"}
print(post.title, post.date)
post.words = 39
for k in keys(post) do
  print(k, post[k])
end
print(has_key(post, "tags"), get(post, "tags", "(no tags)"))`,
      },
      {
        say: ["Maps are good counters. `words` splits text at spaces."],
        code: R`local counts = {}
for w in words("the fog and the crows and the tide") do
  counts[w] = get(counts, w, 0) + 1
end
for w in keys(counts) do
  print(w, counts[w])
end`,
      },
    ],
  },

  {
    title: "Reading a post",
    cells: [
      {
        say: [
          "Here the lessons meet: a function that reads a post's `name: value` lines into a map. `lines` splits text into lines, `find` gives where one string is in another, and `sub` takes a part. `break` leaves the loop at `---`.",
        ],
        code: R`function front(path)
  local meta = {}
  for line in lines(read(path)) do
    if line == "---" then
      break
    end
    local at = find(line, ": ")
    meta[sub(line, 1, at - 1)] = sub(line, at + 2)
  end
  return meta
end

local m = front("posts/tide-pools.md")
print(m.title)
print(m.date)
for tag in split(m.tags, ", ") do
  print("tag: " .. tag)
end`,
      },
    ],
  },

  {
    title: "Walking the project",
    cells: [
      {
        say: [
          "`walk_all` visits everything under a folder, however deep, and gives each entry's `path`, `size` and `kind`. `{kind = \"file\"}` leaves the folders out.",
        ],
        code: R`for e in walk_all("", {kind = "file"}) do
  print(e.path, e.size)
end`,
      },
      {
        say: ["`table` lines up a list of maps in columns, numbers to the right."],
        code: R`local rows = {}
local total = 0
for e in walk_all("", {kind = "file"}) do
  push(rows, {file = e.path, bytes = e.size})
  total = total + e.size
end
print(table(rows, {"file", "bytes"}))
print(#rows .. " files, " .. total .. " bytes")`,
      },
    ],
  },

  {
    title: "Sorting",
    cells: [
      {
        say: [
          "`sort` puts a list in order. A list of maps sorts by one of their names; `\"desc\"` makes it newest first. Dates written `2026-10-02` sort as text in date order.",
        ],
        code: R`function front(path)
  local meta = {}
  for line in lines(read(path)) do
    if line == "---" then break end
    local at = find(line, ": ")
    meta[sub(line, 1, at - 1)] = sub(line, at + 2)
  end
  return meta
end

local posts = {}
for name in ls("posts") do
  local m = front("posts/" .. name)
  push(posts, {date = m.date, title = m.title})
end
sort(posts, "date", "desc")
print(table(posts, {"date", "title"}))
print()
print("newest: " .. posts[1].title)`,
      },
    ],
  },

  {
    title: "Searching",
    cells: [
      {
        say: ["`grep` gives the lines of a file that hold some text, each with its line number."],
        code: R`for name in ls("posts") do
  for hit in grep("posts/" .. name, "fog") do
    print(name .. ":" .. hit.line, hit.text)
  end
end`,
      },
      {
        say: [
          "`count_in` counts how often text appears in a file, without reading it into the script. `re_find` matches a pattern; here, the date line's year and month.",
        ],
        code: R`for name in ls("posts") do
  local p = "posts/" .. name
  local m = re_find(read(p), [=[date: (\d+-\d+)]=])
  print(name, "'the' " .. count_in(p, "the") .. " times", "month " .. m.group)
end`,
      },
    ],
  },

  {
    title: "Editing",
    cells: [
      {
        say: [
          "`edit` changes text inside a file and prints the change as a diff: `-` for the old line, `+` for the new.",
          "Run it a second time and it stops: the old text is gone, and `edit` will not guess. A change you meant to make once fails loudly the second time.",
        ],
        code: R`edit("posts/tide-pools.md", "Anemones close", "Sea anemones close")`,
      },
      {
        say: [
          "A tag renamed across the blog. `all` changes every one in a file, and `word` only whole words: the fog post's `sidewalks` holds `walks` too, and is left alone.",
        ],
        code: R`for name in ls("posts") do
  local p = "posts/" .. name
  if count_in(p, "walks") > 0 then
    edit(p, "walks", "walking", {all = true, word = true})
  end
end`,
      },
    ],
  },

  {
    title: "Building the index",
    cells: [
      {
        say: [
          "Everything so far, in one script: read each post's lines, sort by date, and write the blog's front page, `index.md`, with a link to each post.",
        ],
        code: R`const BLOG = "Field Notes"

function front(path)
  local meta = {}
  for line in lines(read(path)) do
    if line == "---" then break end
    local at = find(line, ": ")
    meta[sub(line, 1, at - 1)] = sub(line, at + 2)
  end
  return meta
end

local posts = {}
for name in ls("posts") do
  local m = front("posts/" .. name)
  m.file = "posts/" .. name
  push(posts, m)
end
sort(posts, "date", "desc")

local page = "# " .. BLOG .. "\n\n"
for p in posts do
  page = page .. "- " .. p.date .. ": [" .. p.title .. "](" .. p.file .. ")\n"
end
replace("index.md", page)
print(page)`,
      },
      {
        say: ["And a page of tags, each with how many posts carry it."],
        code: R`local tags = {}
for name in ls("posts") do
  local m = re_find(read("posts/" .. name), [=[tags: ([^\n]*)]=])
  for t in split(m.group, ", ") do
    tags[t] = get(tags, t, 0) + 1
  end
end

local page = "# Tags\n\n"
for t in sort(keys(tags)) do
  page = page .. "- " .. t .. " (" .. tags[t] .. ")\n"
end
replace("tags.md", page)
print(page)`,
      },
    ],
  },

  {
    title: "When things go wrong",
    cells: [
      {
        say: [
          "A mistake stops the run, with the line, the column, a class (here `store`) and what to do about it. Nothing is guessed: a missing file is never an empty string.",
        ],
        code: R`print(read("posts/missing.md"))`,
        stops: true,
      },
      {
        say: [
          "`attempt` runs a function and catches its stop, so one bad file does not end the run. `fail` is a script's own stop.",
        ],
        code: R`function title_of(path)
  local first = lines(read(path))[1]
  if not starts_with(first, "title: ") then
    fail(path .. " has no title line")
  end
  return sub(first, 8)
end

for p in {"posts/tide-pools.md", "index.md", "drafts/owls.md"} do
  local r = attempt(title_of, p)
  if r.ok then
    print(p, r.value)
  else
    print(p, "skipped: " .. r.error)
  end
end`,
      },
      {
        say: [
          "Every run has budgets: steps, files read, bytes, changes. A loop that never ends is stopped by its step budget, and the report says where. `budget()` says what is left.",
        ],
        code: R`local left = budget()
print("steps left:", left.steps)
print("changes left:", left.changes)

local n = 0
while true do
  n = n + 1
end`,
        stops: true,
      },
    ],
    end: [
      "That is the gist of Shale. The blog on the right is yours to keep changing: edit any cell and run it again.",
      "The full reference, every builtin with its costs and its errors, is `docs/SHALE.md` in Shale's repository.",
    ],
  },
];
