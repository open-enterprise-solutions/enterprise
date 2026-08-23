# Notice

## Who holds the copyright

OES Enterprise is the work of two authors, and both hold copyright in what they wrote:

- Maxim Kornienko
- Yurii Bulakh

Both agreed to the change of terms recorded below.

## Contributors

People who improved this platform and let it be licensed as it is. A contribution accepted
here goes into something owned and sold by the authors above and is not paid for — see
[CONTRIBUTING.md](CONTRIBUTING.md), which says so plainly. This list is the credit that was
promised in return, and it is kept accurate on purpose.

- **fishca** (`sergrud@mail.ru`) — PR #39, February 2026: a fix in the spreadsheet document
  view and the external data-processor window.

## The change of terms, 2026-08-23

Until 2026-08-23 this software was published under the GNU LGPL 2.1. From that date the
terms are the PolyForm Noncommercial License 1.0.0 — see [LICENSE.md](LICENSE.md).

The change applies **going forward only**. Anything already released under the LGPL 2.1
stays available under it to whoever obtained it; a licence already granted cannot be
withdrawn.

## Third-party components

These keep their own licenses. Nothing in LICENSE.md changes or overrides them.

| Component | Where | License |
|---|---|---|
| wxWidgets | `src/3rdparty/wxWidgets` (git submodule) | wxWindows Library Licence |
| cpp-httplib | `src/3rdparty/cpp-httplib` | MIT |
| nlohmann/json | `src/3rdparty/nlohmann` | MIT |

## wxWidgets-derived sources inside the engine

Some of the widget layer is a **fork** of wxWidgets code rather than a use of it: the
data-view control, the grid, the tree control and the document/view layer were taken from
wxWidgets and changed in place. Those files carry the original notices and remain under
the **wxWindows Library Licence**, whatever the rest of this repository is licensed under.
A derivative of a library cannot be relicensed by the party deriving it, and this notice
is here so that no one has to reconstruct that from file headers.

They live in:

| Directory | Files |
|---|---|
| `src/engine/frontend/win/ctrls/grid` | 9 |
| `src/engine/frontend/win/ctrls/dataview` | 8 |
| `src/engine/frontend/docView` | 2 |
| `src/engine/frontend/uikit/ctrl` | 2 |

The per-file notices are authoritative; the table is a map, not a substitute. See
`docs/wx-fork.md` (private) for what was changed and why.

## Documentation

The design documentation moved to a private repository on 2026-08-23 and is attached here
as the `docs` submodule. It resolves for members of the Open Enterprise Solutions
organisation and is simply absent for everyone else — the build does not need it, and CI
initialises only the wxWidgets submodule for that reason.
