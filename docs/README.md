# Documentation

Start with **[development.md](development.md)**, which describes how the platform is built:
branches and pull requests, code rules, the design rules changes are reviewed against, tests and
CI.

| Document | What it holds |
|---|---|
| [development.md](development.md) | How we work: pull requests, commits, code, design rules, tests, speed, documentation |
| [BUILD.md](BUILD.md) | Building on Windows, macOS and Linux, database drivers, CI |
| [portability.md](portability.md) | The rules for code that must compile on MSVC, Clang and GCC, and what breaking them cost |
| [ARCHITECTURE.md](ARCHITECTURE.md) | The system, directory by directory |
| [ai-context.md](ai-context.md) | Context for an AI assistant that generates metadata or scripts |
| [public/](public/) | Design documents anyone may read — how a subsystem works, and why it is shaped that way |
| [release-notes/](release-notes/) | What each release changed |

Outside this folder: [README.md](../README.md) (what OES is, quick start),
[CONTRIBUTING.md](../CONTRIBUTING.md) (what sending a pull request means) and
[CLAUDE.md](../CLAUDE.md) (layout, naming, key decisions; also the context file for assistants).

## `public/` and `private/`

Design documents come in two kinds. The ones anyone may read — how a subsystem works, why an
interface has its shape, what a mechanism guarantees — live in [public/](public/), in this
repository. Its README says what belongs there.

Arcs in progress, plans and anything that stays inside the organisation live in a private
submodule at `docs/private/`. It opens for members of the Open Enterprise Solutions organisation:

```
git submodule update --init docs/private
```

For everyone else the directory stays empty, and links into it from code and from the documents
above will not open. Nothing in the build or the tests depends on it.
