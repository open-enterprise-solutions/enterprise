# Agent rules — OES Enterprise

For any AI assistant or coding agent working in this repository (Claude Code, Codex, Cursor, Copilot
and the like). This file only points the way; the rules live where the people read them too.

- **Context:** [CLAUDE.md](CLAUDE.md) — layout, naming, key decisions, how to run it and reach the
  MCP server. [docs/ai-context.md](docs/ai-context.md) when you generate metadata or scripts.
- **The rules a change is reviewed against:** [docs/development.md](docs/development.md).
- **What a script can check is checked:** run `bash .github/lint.sh origin/develop` before you push;
  CI runs the same script before any build.
- **Authorship:** the person who asked for the change is its only author. No `Co-Authored-By:`
  trailer and no "Generated with …" footer, in commits or in the pull request
  ([development.md §1](docs/development.md); the lint refuses them).
