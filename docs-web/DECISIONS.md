# Decisions

One entry per decision, in ADR form. Nobody edits a past entry: a decision that
stops holding is superseded by a new one that says so.

---

## ADR-001 — The web runtime is the reference UI implementation
*2026-09-05*

**Context.** Enterprise renders forms twice: through wxWidgets on the desktop and
through the web client. Two rendering paths mean two answers to every question of
appearance, and wx cannot express a token system — its colours are compiled in.

**Decision.** The web runtime is the reference implementation for Enterprise.
Native wx remains as a shell (windowing, OS dialogs) and as the Designer's UI.

**Consequences.** Design work lands once, on the web side. The desktop client
keeps rendering forms until the shell work of Iteration 5 replaces its content
area. The Designer is out of scope entirely.

---

## ADR-002 — UI5 Web Components v2.x, self-hosted, no bundler
**Superseded by ADR-015.**
*2026-09-05*

**Context.** The standard controls need a component library with an enterprise
density story and a theme system. Paid tiers and CDN dependencies are both out.

**Decision.** UI5 Web Components v2.x (Apache 2.0), pinned to an exact version,
served from the repository as raw ES modules through an importmap. No bundler, no
Node at build or run time: the C++ build is the only build.

**Consequences.** Free for commercial use; Theme and Density built in. The cost
is real and was paid immediately: UI5 v2 loads its theme parameters as JSON
modules that assume a bundler will convert them, so the vendor script converts
them itself (see `web/assets.md`). Every future version bump re-runs that script.

---

## ADR-003 — Tabulator is the single grid engine
*2026-09-05*

**Context.** TableBox and GridBox need hierarchy, grouping, totals, a cell-editing
state machine, a cursor independent of selection, and an array data feed.

**Decision.** Tabulator (MIT) is the grid engine. `ui5-table` is not used anywhere.

**Consequences.** One grid to learn and to theme. `ui5-table` lacks every item on
that list and its virtualizer is experimental, so mixing the two would have meant
two grids with different keyboard contracts.

---

## ADR-004 — webSizer owns form layout
*2026-09-05*

**Context.** Layout comes from the metadata as wxSizer semantics — proportion,
flag, border — serialized by `webSizer.cpp` and mapped to flexbox on the client.
`ui5-form` is a layout container of its own with different rules.

**Decision.** `webSizer` owns layout. `ui5-form` is not used. UI5 elements are
leaves inside sizer divs.

**Consequences.** Metadata parity with the Designer and the desktop; no layout
lock-in to a component library. A future mobile layout mode lives inside
`webSizer` rather than in a second library. The practical cost is small and known:
custom elements are `display: inline` until told otherwise, so each renderer sets
its own display to remain a flex child.

---

## ADR-005 — Theme and Density are independent axes
**Superseded by ADR-015.**
*2026-09-05*

**Context.** A combined mode set (Light-Compact, Dark-Comfortable, …) multiplies
with every future axis value.

**Decision.** Theme ∈ {Light, Dark} — UI5 Morning Horizon / Evening Horizon, High
Contrast reserved. Density ∈ {Compact (default), Comfortable} — the
`ui5-content-density-compact` class on the root. Never a combined mode.

**Consequences.** Two independent switches. Both reach the document and both
reach an iframe of the client (verified in Chromium across three combinations).

---

## ADR-006 — Semantic token names are OES-owned
*2026-09-05*

**Context.** A vendor's property name in metadata is a dependency on that vendor.

**Decision.** Tokens carry OES semantic names — `color.text.primary`,
`table.row.current.background`. UI5, Tabulator, CSS and C++ names are Layer-4
mappings. Metadata never stores a vendor property name: `validationState`, not
`valueState`.

**Consequences.** The library can be replaced without touching stored metadata.
The mapping table is `web/ui5-integration.md`.

---

## ADR-007 — The form JSON contract is stable
*2026-09-05*

**Context.** Renderers change; the document they render should not.

**Decision.** Renderer migrations happen in JavaScript. `ToJSON` output changes
only when a new OES-semantic property is added to the core, and then for both the
wx and the web side.

**Consequences.** Desktop parity is structural rather than maintained by hand, and
an agent can generate forms against a contract that does not move under it.
Iteration 1 changed no JSON.

---

## ADR-008 — Cursor changes go to their own endpoint
*2026-09-05*

**Context.** `/change/<id>` means "a control's value changed". A grid cursor moves
on every arrow key.

**Decision.** `POST /cursor/<controlId>`, separate from `/change`.

**Consequences.** The two are distinguishable in the log and in the handler.

---

## ADR-009 — Sort and filter are server-side
*2026-09-05*

**Context.** Grid data is paged; the client holds a window, never the set.

**Decision.** The client never sorts or filters grid data locally.

**Consequences.** Ordering matches the desktop for the same list, because it is
the same query. A sort re-fetches from the first page.

---

## ADR-010 — The client owns the fetch queue; the server is stateless per fetch
*2026-09-05*

**Decision.** The client keeps a bounded in-memory window and asks for more at the
edges; each fetch carries its own anchor and the server holds nothing between them.

**Consequences.** A large catalog cannot exhaust the client, and a server restart
does not strand a scroll position.

---

## ADR-011 — Feature flag `ui=legacy|ui5`
**Superseded by ADR-015.**
*2026-09-05*

**Context.** The renderer sets must be comparable side by side, and a bad
migration must be one query parameter away from being undone.

**Decision.** `ui=legacy|ui5` as a query parameter over a `--ui=` server default.
The UI5 renderers are a second map merged over the legacy map; the legacy set
stays intact and reachable. The default stays `legacy` until parity is verified.

**Consequences.** Both sets ship. The flip is a decision of its own, not a side
effect of the last control landing.

---

## ADR-012 — Desktop hosting uses wxWebView
*2026-09-05*

**Decision.** When the desktop client hosts the web UI, it does so through
`wxWebView` (WebView2 on Windows, WebKit elsewhere) with the server in-process.
CEF, Electron and Tauri are not used.

**Consequences.** wx is already a dependency, so the footprint is the smallest
available. A machine without WebView2 needs a clear error and the native fallback.

---

## ADR-013 — Vendored assets live in the repository, not in a submodule
**Superseded by ADR-015.**
*2026-09-05*

**Context.** UI5 has to be served from beside the binary. The obvious instinct is
a submodule, and the repository already uses them for wxWidgets.

**Decision.** The pruned, pinned asset tree lives in `webClient/assets/`, in this
repository, reproduced by `webClient/assets/tools/vendor-ui5.py`.

**Consequences.** Three reasons, in order of weight. `SAP/ui5-webcomponents` is a
TypeScript monorepo that ships source, not the built `dist`, so a submodule of it
would reintroduce the Node build ADR-002 forbids. The loadable modules exist only
in the npm packages, so a submodule would have to be a second repository holding
the same vendored files — the same copy, one indirection further away. And this
repository's `docs` submodule is, as of today, a 404 for the person working in it;
putting the client's ability to render behind that same door would turn a missing
document into a blank screen. The cost is 5.1 MB and 376 files in the tree, and a
version bump that shows up as a large diff. Revisit if a second version has to
coexist.

---

## ADR-014 — Program documents live in `docs-web/` while `docs/` is unreachable
*2026-09-05*

**Context.** Every document this program produces is addressed at `docs/…`, which
is the `enterprise-docs` submodule. That repository does not resolve.

**Decision.** The documents live in `docs-web/`, mapped mechanically:
`docs/web/X` → `docs-web/web/X`, `docs/design/X` → `docs-web/design/X`.

**Consequences.** They reach commits and reviews. When the submodule returns the
directory moves wholesale, which is a rename. See `README.md` beside this file.


---

## ADR-015 — UI5 Web Components are removed; the client draws its own controls
*2026-09-07*

**Context.** ADR-002 took a component library for its theme system, its density
story and its enterprise controls. Ten days of building on it is enough to say
what it actually cost.

What the library gave was a palette and a set of drawn controls. What it took
was a second surface with its own opinions, sitting between the metadata and the
screen, and every defect of the last week was on that seam rather than inside
either side of it. A text control drew a frame, and the renderer drew one too, so
a field sat inside a box inside a box. A group box is a `fieldset` and not a
component, so it had to be painted by hand anyway. A side navigation could not
show a subsystem's own picture, which the metadata carries, so the sidebar lost
the icons it had. The theme's font arrived on some rows and the browser's on
others. None of these were the library being wrong; they were the cost of having
two answers to the same question in one window.

And the platform already guarantees what a component library is usually bought
for. A form is described once, in metadata, and the same description drives the
desktop and the web; the controls, their layout, their commands and their
enabling all come from there. Consistency between two screens of the same
application does not come from the widgets being someone else's — it comes from
both screens being generated from one description. That was true before the
library and stays true without it.

**Decision.** UI5 Web Components are removed: the vendored tree, the import map
entries, the renderer overlay, the theme loader, the `ui=` flag with its
`--ui=` server default, and the `theme` / `density` axes that existed only to
steer them. The client's own renderers — the ones that had been kept intact and
reachable throughout, per ADR-011 — are now the only ones. Tabulator stays; it
was never part of this (ADR-003).

**Consequences.** One renderer, and the seam is gone with the second one. A cold
page load is **17 requests**, measured, where the module graph made it ~450: the
keep-alive workaround that churn forced (see `web/open-issues.md`) is no longer
under that pressure. 6.8 MB and some 520 files leave the repository, and the
vendor script with them.

The palette is now the client's own, in `webClient/assets/oes/oes-tokens.css`,
holding what is still read: the font that stands behind a control's stored face,
and the grid's colours — Tabulator ships a grey slab and a strong blue selection
otherwise. Token names stay OES-semantic (ADR-006); they now map to values
rather than to a vendor's parameters, which is what ADR-006 said should be
possible.

**A dark theme is not built.** It was the library's, and it leaves with it. The
client renders in one light palette. Building a second one is a decision of its
own — the tokens are the place it would go, and nothing else would have to
change.

ADR-006's mapping table lived in `web/ui5-integration.md`, which is deleted:
there is no vendor left to map to.
