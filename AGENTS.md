# Agent rules — OES Enterprise

For any AI assistant or coding agent working in this repository (Claude Code, Codex, Cursor, Copilot
and the like). This file only points the way; the rules live where the people read them too.

- **Context:** [CLAUDE.md](CLAUDE.md) — layout, naming, key decisions, how to run it and reach the
  MCP server. [docs/ai-context.md](docs/ai-context.md) when you generate metadata or scripts.
- **The rules a change is reviewed against:** [docs/development.md](docs/development.md).
- **How the code itself is written** — names, class layout, holders, refusals, the diff:
  [docs/writing-code.md](docs/writing-code.md). Read it before your first edit.
- **What a script can check is checked:** run `bash .github/lint.sh origin/develop` before you push;
  CI runs the same script before any build.
- **Authorship:** the person who asked for the change is its author. No `Co-Authored-By:`
  trailer for a tool and no "Generated with …" footer, in commits or in the pull request
  ([development.md §1](docs/development.md); the lint refuses them). A PERSON whose code is inside
  the commit is named as a co-author — that trailer stays.
- **Language:** English. No Cyrillic in code, comments, test names, commits, pull requests,
  issues, review replies, or docs. The exception is the name of a 1C configuration metadata
  object that already has a Cyrillic name.
