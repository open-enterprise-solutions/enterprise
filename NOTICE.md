# Notice

## Who holds the copyright

OES Enterprise is owned by **Open Enterprise Solutions**
(<https://github.com/open-enterprise-solutions>), which holds the copyright and grants the
licenses: the noncommercial one in [LICENSE.md](LICENSE.md) and the commercial ones.

Open Enterprise Solutions is not yet registered as a company. Until it is, the copyright is
held, and licenses are granted, by Maxim Kornienko, who wrote most of the platform, acting as
Open Enterprise Solutions. Everyone else's work is contributed under the grant in
[CONTRIBUTING.md](CONTRIBUTING.md), in return for the platform free for noncommercial use and
for evaluation.

The copyright holders agreed to the change of terms recorded below.

## Contributors

People who improved this platform and let it be licensed as it is. A contribution accepted
here goes into something owned and sold by Open Enterprise Solutions and is not paid for — see
[CONTRIBUTING.md](CONTRIBUTING.md), which says so plainly.

Their credit is the history itself. A pull request is merged as it is, not squashed, so every
contributor stays the author of their own commits, and the repository's contributor list on
GitHub names them. It is not copied here by hand, because a copy kept by hand drifts from the
record it copies.

## The change of terms, 2026-08-23

Until 2026-08-23 this software was published under the GNU LGPL 2.1. From that date the
terms are the PolyForm Noncommercial License 1.0.0 — see [LICENSE.md](LICENSE.md).

The change applies **going forward only**. Anything already released under the LGPL 2.1
stays available under it to whoever obtained it; a license already granted cannot be
withdrawn.

## Third-party components

These keep their own licenses. Nothing in LICENSE.md changes or overrides them.

| Component | Where | License |
|---|---|---|
| wxWidgets | `src/3rdparty/wxWidgets` (git submodule) | wxWindows Library Licence |
| cpp-httplib | `src/3rdparty/cpp-httplib` (submodule) | MIT |
| Mbed TLS | `src/3rdparty/mbedtls` (submodule) | Apache-2.0 |
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
`docs/private/wx-fork.md` (private) for what was changed and why.

## Icons

Two icons in the designer come from **[icons8](https://icons8.com)** and are used under its
free tier, which requires attribution:

| Icon | Where it is used | Source |
|---|---|---|
| *switch host* | the start page — tab, menu item, workspace editor | icons8 |
| *ai* | the assistant — tab and menu item | icons8 |

Both are downscaled to 16px and embedded as base64 in
`src/engine/backend/picturePredefined.cpp`, where each carries a comment pointing here.

⚠ This section was missing until 2026-08-31 even though the code had said for months that the
credit "is kept in the docs" — the note existed, the attribution did not. It is here rather
than in the private `docs/private` submodule for the reason that matters: an obligation to a third
party has to survive somebody not having access to the documentation.

## Documentation

The design documentation moved to a private repository on 2026-08-23 and is attached here
as the `docs/private` submodule. It resolves for members of the Open Enterprise Solutions
organisation and is simply absent for everyone else — the build does not need it, and CI
initialises only the wxWidgets submodule for that reason. What a contributor needs — how we
work, the build, portability, the architecture — is public, in `docs/`.
