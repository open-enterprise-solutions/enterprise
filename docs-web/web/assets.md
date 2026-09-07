# Vendored web assets

Everything the browser loads is served from this repository. Nothing reaches a
CDN at run time — verified in Chromium: zero requests leave localhost in any
theme/density combination, on the client and on the harness.

## Layout

```
webClient/assets/
├── smoke.html                     one ui5-button, to prove the importmap resolves
├── harness.html                   two iframes of the real client, side by side
├── harness-form.json              the fixture both iframes render
├── oes/oes-tokens.css             Layer 2/3 semantic tokens
├── tools/vendor-ui5.py            reproduces everything under ui5/
└── ui5/2.26.0-<digest>/
    ├── importmap.json
    ├── THIRD_PARTY_LICENSES.txt
    ├── @sap-theming/theming-base-content/    24 font files
    ├── @ui5/webcomponents/                   198 files
    ├── @ui5/webcomponents-base/              151 files
    ├── @ui5/webcomponents-fiori/              58 files
    ├── @ui5/webcomponents-icons/              90 files
    ├── @ui5/webcomponents-localization/        6 files
    └── @ui5/webcomponents-theming/            13 files
```

540 files, 6.5 MB. The server mounts the directory at `<prefix>/assets`,
resolving it the same three ways `LoadClient()` resolves the client itself:
`<exeDir>/web/assets`, then a walk up to `webClient/assets` bounded at six levels.
The path is served `immutable` with a year's cache.

## The directory is named after its contents

`ui5/<version>-<digest>/`, where the digest is a SHA-256 over every staged
file's path and bytes. `immutable` is a promise that the bytes at a URL never
change, and the tree under a bare `2.26.0/` is not upstream's — it is what this
script *makes*, and it changed when the script did.

It cost a day. An early run staged the theme parameters as
`parameters-bundle.css.json`; a later one converted them to `.css.js` and
rewrote the loader's import to match. A browser holding the old
`json-imports/Themes.js` — for the year the header had promised — kept asking
for a `.json` that no longer existed, got a 404 with no content type, and
reported `blocked because of a disallowed MIME type ("")`. The theme never
loaded, every `--sap*` stayed undefined, and the page rendered with black
borders. No fresh browser could reproduce it, which is the shape of every cache
bug. Reported from Firefox, 2026-09-07.

The digest is computed over the staged tree before the import map is written
into it, so it is stable: two runs produce the same id. Both pages that carry an
import map — `assets/smoke.html` and `webClient/client.html` — are repointed by
prefix substitution, which leaves the client's own entries (tabulator) alone,
and any older `ui5/*` directory is removed so one tree is served.

`assets/tabulator/<version>/` still keys on the bare version. Its files are
copied verbatim from the package rather than transformed, so the same version
has always meant the same bytes — but that is a habit, not a mechanism, and the
day its vendoring starts rewriting anything it should grow the same digest.

## Updating

```
python3 webClient/assets/tools/vendor-ui5.py
```

It downloads into a scratch directory under `/private/tmp`, never into the
repository, and copies out only what a browser loads. Two runs produce an
identical tree — the aggregate hash is checked.

## What is pruned, and why the script and not a hand

The first vendoring copied whole packages: 91 MB and 13 867 files, of which
21 MB were `.d.ts`, 10 MB were source maps and 19 MB were icon sets nothing
imports. A source repository does not carry that.

The rule is: **vendor what a browser loads, and nothing else.** The script
implements the rule, so re-running reproduces the pruned tree rather than a full
copy someone then trims by hand.

- Dropped outright: `.d.ts`, `.ts`, `.map`, per-package READMEs and licences (the
  obligation is kept, consolidated, in `THIRD_PARTY_LICENSES.txt`), tests,
  fixtures, and the `package.json` files used only to resolve exports.
- Icon sets: `webcomponents-icons-tnt` and `-business-suite` are dropped entirely.
  From `webcomponents-icons`, only the icons the import graph reaches — twenty-eight
  of them, in both the v4 and v5 shapes, because each wrapper picks by theme family.
  The script prints the list it ended up with, so a new entrypoint that quietly
  pulls a hundred icons is visible in the run rather than in a diff.

`@ui5/webcomponents-fiori` arrived with the chrome (2026-09-07): `ui5-shellbar`
and `ui5-side-navigation` live there and nowhere else. It is a SIBLING of the
root package, not a dependency, so the graph walk could never find it on disk —
`COMPANION_PACKAGES` at the top of the script is what npm installs alongside, and
its version is checked against `VERSION` the same way the root's is. Adding
another package is one entry there plus its entrypoints. The whole chrome cost
1.1 MB and 164 files.

**A package's `Assets.js` is an entrypoint of its own.** It is what registers
that package's theme parameters, and nothing imports it — a component asks for
its bundle by name at run time, which the graph walk cannot see. The fiori
parameters were therefore absent on the first pass, and its components rendered
with the base package's. Every package with components on the page needs its
`Assets.js` listed.
- Locales: `en`, `ru`, `uk`, matching what the build stages as `*.hlk`. The list is
  a named constant at the top of the script; adding a locale is one line.

The script **fails** rather than warns if a document-relative URL or a bare JSON
import survives it. Both were real defects (below), and neither may return quietly
on a version bump.

## Two things UI5 v2 assumes, that a bundler-free tree has to undo

**Theme parameters are JSON modules.** UI5's generated loader does

```js
return (await import(/* webpackChunkName: "..." */ "../assets/themes/sap_horizon_dark/parameters-bundle.css.json")).default;
```

— a bare `import()` of a `.json` with no `with { type: "json" }`, and a
`webpackChunkName` comment saying out loud what it expects. Without a bundler the
browser applies strict MIME checking and refuses every theme bundle; the elements
upgrade, get their shadow roots, and render as transparent nothing with every
`--sap*` variable empty. The script converts each bundle into `export default
<json>` beside its loader and rewrites the specifier. 29 of them: 20 theme
bundles, 6 message bundles, 3 CLDR.

**Rewritten URLs must resolve against the module, not the document.** The font
URLs were first rewritten as `./assets/ui5/2.26.0/…`, which resolves only for a
page sitting at the prefix root. The client is such a page; the harness is one
level deeper, and there all 24 faces aborted with a doubled `assets/assets/`. They
are built with `new URL('…', import.meta.url).href` now, so page depth stops
mattering. An importmap is a different case — it *is* resolved against the
document, so each page carries a prefix correct for its own depth.

## Tabulator 6.5.2

`webClient/assets/tabulator/6.5.2/` — the grid engine (ADR-003), vendored by
`webClient/assets/tools/vendor-tabulator.py`. Two files (`tabulator.js`, the ESM
build, 742 KB; `tabulator.css`, 28 KB), plus `LICENSE.txt` and a `manifest.json`
recording the SHA-256 of each. Re-running the script reproduces the same digests.

Source-map comments are stripped: shipping one without the map means a 404 on
every load. The stylesheet is Tabulator's unstyled base — the look comes from
`--oes-table-*` tokens, not from one of its shipped themes.

Served under `Cache-Control: public, max-age=31536000, immutable`, like the UI5
tree, because the path carries the exact version. Imported lazily, so a form with
no table on it never loads it.
