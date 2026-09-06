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
└── ui5/2.26.0/
    ├── importmap.json
    ├── THIRD_PARTY_LICENSES.txt
    ├── @sap-theming/theming-base-content/    24 font files
    ├── @ui5/webcomponents/                   135 files
    ├── @ui5/webcomponents-base/              143 files
    ├── @ui5/webcomponents-icons/              54 files
    ├── @ui5/webcomponents-localization/        6 files
    └── @ui5/webcomponents-theming/            13 files
```

376 files, 5.1 MB. The server mounts the directory at `<prefix>/assets`,
resolving it the same three ways `LoadClient()` resolves the client itself:
`<exeDir>/web/assets`, then a walk up to `webClient/assets` bounded at six levels.
The versioned path is served `immutable` with a year's cache.

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
  From `webcomponents-icons`, only the icons the import graph reaches — sixteen of
  them, in both the v4 and v5 shapes, because each wrapper picks by theme family.
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
