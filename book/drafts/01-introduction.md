# Draft

## Introduction

Shale is a programming language written by agents, for agents. It looks like Lua on purpose, and it mostly works like any mainstream language: variables, functions, loops, lists and maps, strings and files. Its unique features are built for its niche, which is scripts an agent writes and a human reads.

That niche is the premise of this book. You will read a lot of Shale and write very little. Your agent writes it, every time it searches your code, runs your tests, renames across a tree or ships a release. You read it as it scrolls by, judge it, and steer the agent that wrote it. This book teaches you to do that well.

### Who this book is for

This book is for experienced developers: people who have written plenty of code, but not much of it with an agent. You know a build from a test and a test from a gate. You may have tried an agent on a small task and wondered what it would take to trust one with a real project. You're curious about programming languages, and open to letting an agent do real work.

### Agents in late 2026

This book was written with late-2026 agents, and agents change fast. If your picture of agents is a year old, they're already better than you think. And some of the workflow here may be obsolete by 2027. So the book leans on principles that should last. The first is that an agent can't read your mind, so getting your requirements across is always part of the bottleneck. That's why this book's project is an essay surface, where you reply to the exact paragraph you mean.

You won't find skills files here, the instruction files some setups use to teach an agent a task. A modern agent learned how to do things in training. What it needs from you is a mission and a standard, so this project has mission documents, style guides and Shale gates instead.

### The project

The book follows one side project. We build a new essay surface: a small web app where an agent's notes, written in markdown, become pages, and Steve, who runs this project, reads them in a browser and replies to any paragraph. (The first prompt says "where I write notes". In practice the agent writes them, and Steve reads and replies.) A Go server sits behind a plain-JavaScript front end. Steve soon used it to reply to the drafts of this book.

We tell it in sessions, a chapter each. A session is one stretch of work that starts with one prompt. The sessions aren't days: the real build took an afternoon of agent time, and the feature in Session 7 came the next morning. Each session's agent kept a journal under the name first given it, so the captions under Session 1's quotes say `monday.md`. An essay surface isn't an agent harness, the program an agent runs inside, but in both a human reads what was written, replies, and steers.

### We, and the prompts

The book says "we" for Steve and his agent. Each session opens with the prompt we gave the agent, quoted whole, which sets the session's mission. Later prompts mostly enforce standards, and a few improve the workflow itself. Then it follows the agent's own Shale, quoted from the journal it kept. The agents were real, the prompts were real, and so were the mistakes. When something went wrong, the book says so, along with what caught it and what habit it taught.

### Gates

The thread through every session is gates. A gate is a script that holds work to a standard. It runs the checks, prints a verdict on its last line, and fails when the work falls short. Gates are how we write quality software with agents, because they turn a rule into something that runs on every change.

Nobody plans the gates up front. They arrive when the work asks for them, from a check before a commit in Session 1 to a release with a rollback in Session 5.

Gates need a language to write them in. Next, a short tour of Shale.
