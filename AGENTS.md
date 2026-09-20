# Agent rules — OES Enterprise

Rules for any AI assistant or coding agent working in this repository
(Claude Code, Cursor, Codex, Copilot, Grok Bot, and the same class of tools).
Human contributors follow [CONTRIBUTING.md](CONTRIBUTING.md); this file is the
agent-facing counterpart for habits that only show up when a tool writes the
commit or the pull request.

Architecture and code conventions remain in [CLAUDE.md](CLAUDE.md).

## Commits and pull requests — authorship

**The human who asked for the change is the only author.** Do not put the
agent (or the product that runs it) in the git history or on the pull request
as a co-author.

In particular, do **not**:

- add a `Co-Authored-By:` / `Co-authored-by:` trailer naming Claude, Cursor,
  Codex, Copilot, GPT, Gemini, Grok, or any other assistant;
- append “Generated with …”, “Made with …”, “🤖 …”, or similar footers to
  commit messages or pull-request bodies;
- set `GIT_AUTHOR_*` / `GIT_COMMITTER_*` (or the hosting UI’s co-author field)
  to an agent identity.

Commit and open the PR as the human: their name and email from the local git
config / signed-in account. If a tool would inject a co-author trailer by
default, strip it before `git commit` and before the PR is opened.

A pull request that already shows an agent as co-author should be fixed by
rewriting the commit messages on that branch (and updating the PR body), not
by leaving the trailer in place.
