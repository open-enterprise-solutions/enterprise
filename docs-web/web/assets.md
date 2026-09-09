# Vendored web assets

Everything the browser loads is served from this repository. Nothing reaches a
CDN at run time — verified in Chromium: zero requests leave localhost, on the
client and on the harness.

A cold page load is **17 requests**. It was ~450 while the client ran on a
component library, which is what the keep-alive workaround in
`web/open-issues.md` was written under; the library was removed on 2026-09-07
(ADR-015) and the client draws its own controls.

## Layout

```
webClient/assets/
├── harness.html                   an iframe of the real client, fed a fixture
├── harness-form.json              the fixture it renders
├── oes/oes-tokens.css             the client's palette
├── tools/vendor-tabulator.py      reproduces everything under tabulator/
└── tabulator/6.5.2/               the grid engine (ADR-003)
```

The server mounts the directory at `<prefix>/assets`, resolving it the same
three ways `LoadClient()` resolves the client itself: `<exeDir>/web/assets`,
then a walk up to `webClient/assets` bounded at six levels.

## Two kinds of URL, and a header for each

`tabulator/` carries its identity in the path, so that mount is served
`public, max-age=31536000, immutable` — the bytes at one of those URLs are the
same bytes forever, and a browser is right never to ask again.

Everything else under `/assets` is the client's own and lives at a path that
does not move when the file does. Those are served `no-cache`, which keeps the
copy and requires the browser to revalidate it; cpp-httplib's `ETag` turns the
usual answer into a `304`. And the URLs the page names are **stamped**:
`StampAssetURLs` in the server walks the client HTML for `"./assets/…"`, reads
each one that resolves to a file outside the versioned mount, and appends
`?v=<FNV-1a of its bytes>`. The stamp is computed once, when the client HTML is
read, so everything a page names is pinned to the run that served it.

Before the header, the token stylesheet went out with only `ETag` and
`Last-Modified`, which lets a browser invent a freshness lifetime and reuse the
file without asking. One did: a current `client.html` was drawn against an
`oes-tokens.css` from two commits earlier, which did not yet define the chrome
tokens the page named. Every panel that read one — the sidebar, the output pane,
the status bar — fell through to the bare canvas and rendered white, while
`#main`, whose token had survived, stayed dark. Reported from Firefox as
"криво", 2026-09-07; reproduced by serving the old stylesheet to a current page
and confirmed identical. A header alone would not have freed that browser, since
a copy already held as fresh is never asked about — the stamp changes the URL,
which is the one thing a cache cannot second-guess.

`assets/tabulator/<version>/` keys on the bare version. Its files are copied
verbatim from the package rather than transformed, so the same version has
always meant the same bytes — but that is a habit, not a mechanism, and the day
its vendoring starts rewriting anything it should carry a digest in the
directory name —
[#107](https://github.com/open-enterprise-solutions/enterprise/issues/107). It
is deliberately left out of the stamping above: a URL that promises immutability
should say so in one place, not two.

## The palette

`assets/oes/oes-tokens.css` holds what is still read from a token rather than
written where it is used: the font that stands behind a control's stored face,
and the grid's colours. Token names stay OES-semantic (ADR-006) and now map to
values rather than to a vendor's theme parameters.

The client renders in one light palette. A dark one is not built — it belonged
to the component library and left with it (ADR-015); this file is where it would
go.

## Tabulator 6.5.2

`webClient/assets/tabulator/6.5.2/` — the grid engine (ADR-003), vendored by
`webClient/assets/tools/vendor-tabulator.py`. Two files (`tabulator.js`, the ESM
build, 742 KB; `tabulator.css`, 28 KB), plus `LICENSE.txt` and a `manifest.json`
recording the SHA-256 of each. Re-running the script reproduces the same digests.

Source-map comments are stripped: shipping one without the map means a 404 on
every load. The stylesheet is Tabulator's unstyled base — the look comes from
`--oes-table-*` tokens, not from one of its shipped themes.

Its own palette is a grey slab with a strong blue selection, written for a page
that brings nothing with it, and its rules run three classes deep. The client's
overrides therefore stand one deeper (`.oes-tablebox .tabulator .tabulator-…`),
because the Tabulator stylesheet is appended to the head when the grid loads and
so comes after the client's own — the depth is what makes them readable at all,
not decoration.

Served under `Cache-Control: public, max-age=31536000, immutable`, because the
path carries the exact version. Imported lazily, so a form with no table on it
never loads it.
