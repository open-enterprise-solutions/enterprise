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
| [release-notes/](release-notes/) | What each release changed |

Outside this folder: [README.md](../README.md) (what OES is, quick start),
[CONTRIBUTING.md](../CONTRIBUTING.md) (what sending a pull request means) and
[CLAUDE.md](../CLAUDE.md) (layout, naming, key decisions; also the context file for assistants).

## `private/`

Design documents, arcs and plans live in a private submodule at `docs/private/`. It opens for
members of the Open Enterprise Solutions organisation:

```
git submodule update --init --checkout docs/private
```

For everyone else the directory stays empty, and links into it from code and from the documents
above will not open. Nothing in the build or the tests depends on it.
