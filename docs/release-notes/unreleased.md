# Unreleased

Since [v2.0.0-preview.1](v2.0.0-preview.1.md).

## Breaking changes

- **In a date pattern `MM` is the month and `mm` the minutes**, as everywhere else they are written;
  the seconds are `ss`. It was the other way round. A stored format such as `DF=dd.mm.yyyy` now
  prints the minutes where the month was, and nothing reports it: change `mm` to `MM` (and `MM` to
  `mm`, `SS` to `ss`) in the formats of attributes, form items and report fields. The built-in
  presets are already written the new way.

## Script language

- **XML: `XMLReader`, `XMLWriter`, `XMLNamespaceContext`, `XMLNodeType`.** A script reads a document
  node by node and writes one, to a file or to a string, in UTF-8, UTF-16, windows-1251 or cp866.
  The parser is expat, built into the engine; an external entity is refused, not read. A file the
  writer could not write is raised at `Close`.
- **`Format` spells a date out and pads a number.** A date pattern takes `d`/`dd`, `ddd`/`dddd`
  (the weekday), `M`/`MM`, `MMM`/`MMMM` (the month's name, after a day in the genitive in ru and
  uk), `h`/`hh` and `tt` (a 12-hour clock) and text in single quotes. The names come from the base's
  language; `L=ru` says them in the language it names. New codes: `DLF` (a preset — `D`, `DD`, `T`,
  `DT`, `DDT` — used when there is no `DF`), `L` and `NLZ` (the whole part padded with zeros to
  `ND − NFD` digits).

## Fixes

- **A list filtered by «contains» finds the rows that contain the value.** On a list read from the
  base the condition reached the database as `LIKE 'value'`, an exact match, so "Comment contains
  Оплат" showed nothing and only "%Оплат%" worked. The value is now wrapped as `%value%`, as the
  same condition already was in memory; a `%` or `_` typed into the value is still a wildcard. The
  query language's own `LIKE` is unchanged and takes its pattern as written.
- **A server whose base and `backend.conf` name no language speaks US English**, not the language of
  the machine it runs on, so the same server answers the same words everywhere. A language named
  and not installed on the machine falls back to US English too.
- **A table read anew with no row to read around is placed by the server** — a client's first page, a
  list read again from its top: from the top when the table stands on no row, else around the row it
  stands on, with as many rows ahead of that row as the page, the page saying whether it is the top (`Begin`).
  The browser client took such a page for the top: a table standing on its last row came back to a
  resumed session as that one row, with no way to the rows above it. The thin client puts the row it
  was read around mid-view and reads the rows above the ones that came when there are more.
- **The browser client's table reads nothing while it has no height** (a notebook page behind another),
  where every page fell short of the window and the whole list was read; and when the rows held fall
  short of the window with nothing after them, it reads back, not on.
- **An Excel workbook with a part that does not parse is refused, the part named**, where it was read
  with that part left out: shared strings that did not parse left every string cell of the book empty,
  and nobody was told. The workbook's XML is read by the engine's own parser, no longer by wx's.
