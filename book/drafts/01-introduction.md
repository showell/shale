# Draft

## Introduction

Shale is a programming language written by agents, for agents. It looks like Lua on purpose, and it mostly works like any mainstream language: variables, functions, loops, lists and maps, strings and files. Its unique features are built for its niche, which is scripts an agent writes and a human reads.

That niche is the premise of this book. You will read a lot of Shale and write very little. Your agent writes it, every time it searches your code, runs your tests, renames across a tree or ships a release. You read it as it scrolls by, judge it, and steer the agent that wrote it. Here we learn to do that well.

### Who this book is for

This book is for experienced developers: people who have written plenty of code, but not much of it with an agent. You know a build from a test and a test from a gate. You may have tried an agent on a small task and wondered what it would take to trust one with a real project. You're curious about programming languages, and open to letting an agent do real work.

You won't need to install anything to follow along. Every script in the book really ran, and the book shows what it printed.

### The project

The book follows one side project from start to finish. We build a new essay surface: a small web app where Steve writes notes in markdown, reads them in a browser, and replies to any paragraph. A Go server sits behind a plain-JavaScript front end. By the end of the week Steve was using it for real, and he wrote his replies to the drafts of this book on it.

We tell it as a week, a day per chapter, because that's how the work fell into stages. It's a side project, not five calendar days: the real build took an afternoon of agent time. An essay surface isn't an agent harness, though the two have something in common. In both, a human reads what was written, replies, and steers.

### We, and the prompts

The book says "we" for Steve and his agent. Each day opens with the prompt we gave the agent, quoted whole. Then it follows the work through the agent's own Shale, quoted from the journal the agent kept as it went. The agents were real, the prompts were real, and so were the mistakes. When something went wrong, the book says so, along with what caught it and what habit it taught.

### What Shale adds

Most of what you'll read is ordinary code. A few features are Shale's own:
- **Every run has budgets:** steps, reads and output. A runaway script stops and says which budget it hit.
- **A script works inside its own folder** and the named folders it mounts, so you can see everything it can reach.
- **A risky program runs when the script gives its reason.** A shell, an interpreter or a text tool needs a `want = "tag: why"`, and the reason is logged.
- **A stop is one line** that says where and why.
- **A prelude does the daily work:** git, jobs, HTTP, JSON, and refactors that read code the way its compiler does.

Shale builds nothing itself. `go build` makes the server. Shale runs the build, keeps what it said, and judges what came out.

### Gates

The thread through the whole week is gates. A gate is a script that holds work to a standard. It runs the checks, prints a verdict on its last line, and fails when the work falls short. Gates are how we write quality software with agents, because they turn a rule into something that runs on every change.

In this project, nobody planned the gates up front. They arrived when the work asked for them: a check before a commit on Monday, a fast check and a full one by Wednesday, faults planted on purpose on Thursday, a release with a rollback on Friday.

### A language that changes

Shale is meant to have dialects. A dialect costs a team of humans a great deal, because everyone has to learn it. It costs an agent very little, because an agent reads the whole script, and the docs for its own dialect. So you can keep the dialect that works for your agent, and build your own library of scripts on top of the cookbook or instead of it.

The week shows this happening. A nuisance came up four times, so on Saturday the language changed to fix it. For now, a reader who wants that freedom forks their own copy of Shale.

Monday starts with an empty repository and a prompt.
