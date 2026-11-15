# The menu

*2026-11-15. A riff on cookbooks, recipes and the missing word: if the agents do the cooking, what does the reader of the book need?*

## Who's in the kitchen

The words we already use split the kitchen cleanly. The cookbook is the folder of recipes, and a recipe is a Shale script that does one job well. The cooks are agents: they read the cookbook, follow a recipe or improvise one, and taste as they go. That leaves the human with a different job, and you've named it: preparing the meal without cooking it. In a restaurant that's the chef de cuisine, or the owner who decides what the kitchen serves tonight. They rarely touch a pan, and every plate that leaves the kitchen is still theirs.

So a cookbook is the wrong book for our reader, at least on its own. A cookbook teaches you to cook. Our reader needs to order well, judge what arrives, and send a plate back when it's wrong. That's a menu, and the skills that go with it.

## What a menu is

A menu speaks the diner's language, not the cook's. It says "roast chicken with lemon," never "brine for six hours, truss, 425 degrees." Look at yesterday's prompts in that light: "survey the repos," "nuke Cobblestone," "refuse grep now." Each was an order off a menu nobody had written down, and the kitchen turned each one into a dozen recipes you never had to read. You knew what to order because you'd built the restaurant.

A menu is also a promise. Every dish on it can be made tonight, to a standard, in about the time it says. That's what the gates are for: "check before commit" is on the menu because it always takes a few seconds and always means the same thing. A dish the kitchen can't promise doesn't belong on the menu yet.

And a menu says what isn't served. "No substitutions" is a menu's way of being strict. Shale's refusals are that line: no `sed`, no `grep` through `run`, no closures, no aliases for guessed names. The diner never sees them, but they're why every plate comes out the same.

## The menus we already have

We have menus already; we just haven't called them that. essay-surface's `scripts/README.md` is one: check before commit, check before release, release, each named for what you get and how long it takes. The cookbook's README is closer to a pantry list. The census is the restaurant's sales report, telling you which dishes people order, which ones the kitchen keeps improvising, and which belong on the menu permanently. Promoting a recipe to a prelude function is the kitchen making a popular dish faster to plate. Putting it on a menu would be the step after that: saying it out loud, in the diner's words, so anyone can order it.

## A book that teaches ordering and tasting

That suggests a shape for the book, or for part of it. Each chapter is a dish:

- **The order:** what you ask for, in your own words, and what makes the order a good one.

- **The plate:** what comes back, the report and the verdict, and how to read it in a few seconds.

- **The kitchen, briefly:** which recipes did the work, shown once, so you trust the plate without learning to cook it.

- **How to taste it:** the one or two things that tell you it's right, and what to say when it isn't.

A good cookbook teaches you to cook along the way. This book would teach you to taste along the way, which is the skill the human actually needs: reading Shale at a skim, recognizing a green that's only a claim, and spotting the plate that looks right but isn't.

## The curveball

There are two audiences, and maybe two books. The cookbook really is for cooks, and our cooks are agents. Every recipe's header, every stop that names its fix and every row of SHALE-AGENTS.md is cooking instruction, written for a reader who will cook. The menu is for the human. A restaurant needs both, and they're written in different voices for different readers. Maybe the book is the menu, with the cookbook on the shelf behind it, and a note at the front saying that the kitchen reads one and you read the other.

And one question for later: should a project carry a menu of its own, a `MENU.md` listing what you can order there in your words, with what each dish takes and how to taste it? essay-surface's scripts README is already most of one.
