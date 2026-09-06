# Web UI program — documents

These are the documents the web UI program produces. They belong in the
`enterprise-docs` submodule at `docs/`, and they are here instead because that
repository does not resolve: `git submodule update --init docs` answers

```
fatal: repository 'https://github.com/open-enterprise-solutions/enterprise-docs.git/' not found
```

and `gh repo view` cannot see it either. So `docs/` is an empty mount point on
this machine, files written there are invisible to the parent repository's git,
and nothing put in them would reach a commit or a review.

The mapping is mechanical, so the move is a rename when the submodule returns:

| The program says | It lives at |
|---|---|
| `docs/DECISIONS.md` | `docs-web/DECISIONS.md` |
| `docs/web/<name>.md` | `docs-web/web/<name>.md` |
| `docs/design/<name>` | `docs-web/design/<name>` |

Everything the program's §0 tells a reader to read first — `architecture.md`,
`conventions.md`, `ui-shell.md`, `paging-design.md` — is in that unreachable
submodule. What is written here was therefore established from the source, and
where a rule came from the program's own text rather than from a document I
could read, it says so.
