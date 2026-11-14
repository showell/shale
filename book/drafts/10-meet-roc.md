# Draft

## Meet Roc

Part Two makes the notes richer: a note that holds a diagram, drawn from a description in Graphviz's dot language, and later a small live widget. That takes four languages. Go keeps doing the plumbing: it serves pages, stores replies and runs programs. Roc expresses the features, starting with a note format of our own. JavaScript does in the page what JavaScript always does. Shale is the glue, the language the agent builds, checks, refactors and ships all of it with.

Four languages is a lot to ask of a team of humans. It's far less to ask of an agent, for the same reason a dialect of Shale is cheap: the agent reads each language's docs every session. The workflow stays the same, and so does the theme: building your own workflow, now in app territory.

As with Shale, you'll read Roc and rarely write it. Most of it reads like any typed language you know, so this interlude explains only what you wouldn't guess. Here's a real type from the project, the diagram a note can hold:

{{lines repos:essay-surface/format/Diagram.roc from "Diagram :: Str.{" to "== Err(NotDot)"}}

What's worth knowing:
- **`::` makes an opaque type.** A `Diagram` is a string inside, but only the code in the braces after `.{` can see that. `Diagram.(source)` wraps a string into a diagram, and the parameter `|Diagram.(dot)|` unwraps one. Everyone else goes through `from_source`, so every diagram in the program has passed its check.
- **A signature sits on its own line** above its definition. `from_source` takes a string and returns a `Try`: `Ok` with a diagram, or `Err` with the reason.
- **A tag names itself.** `NotDot` needs no declaration: square brackets list the tags a type allows, and a tag can carry a value, as `Ok(...)` does. Think of an enum whose cases you write where you use them.
- **`|word| ...` is a function,** and `opening.starts_with(word)` calls a function on a value, as a method call would.
- **`##` is a doc comment, and `expect` is a test,** kept beside the code and run by `roc test`.

### Nominal and opaque

A type made with `:=` is nominal: it has a name of its own, and any code can build it and read its fields. A note is one:

{{lines repos:essay-surface/format/Note.roc from "Note := {" to "Subtitle : [NoSubtitle, Subtitle(Text)]"}}

A note built from valid parts is a valid note, so there's nothing for a maker to check. The rules live in its parts: `Phrase` and `Text` are opaque, like `Diagram`. Where another language would make the subtitle a nullable field, this type names both cases as tags, and code that reads a subtitle must handle both.

### Pure, with a few marks

Here's the parser's entry, which reads a note's source into a `Note`, or says the first line it can't read:

{{lines repos:essay-surface/format/Parse.roc from "note : Str -> Try(Note, Parse.Problem)" to "Ok({ title, subtitle: opening.subtitle, blocks: $blocks })"}}

Three marks carry most of it:
- **`var $blocks`** declares a variable that can change, and the `$` stays on its name at every use, so you can see each one that does.
- **`?` after a call** returns the call's `Err` from the function at once, and otherwise gives the `Ok` value.
- **`match`** is a switch over tags, and it must cover every tag the type allows.

`{ title, subtitle: ... }` is a record, and a bare `title` means the field `title` takes the variable `title`.

This function is pure: the same source always gives the same note, and it can't read a file or print. A function that acts on the world has a name ending in `!`, such as `echo!`, which prints. Which of those exist depends on the platform, the host program a Roc app runs inside. Session 9 meets one, and the choice of platform is where Roc runs.

### Read the types first

When you review code you rarely write, start with its types. In Roc they say what can exist: a diagram that isn't dot, or a note with two titles, can't be built at all. The functions only say how.

That's enough Roc to start. Next, a session that writes types before any code.
