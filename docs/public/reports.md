# Reports and the data composer

**What.** A report is two halves that meet only at the end. The **data** comes from a query, or from a
**composition**: a declared schema - sources, fields, filters, sorts, groupings, resources and named
variants - that the **data composer** renders into ordinary query text. The **presentation** is a
spreadsheet document: cells, areas, outline groups. A report declares its compositions as templates of the
composer kind, and its form shows one with its commands: **Compose**, **Settings**, **Variants**.

**Why.** A schema spoken in the reader's words - *filter*, not `WHERE`; *grouping*, not `TOTALS BY` - is
something a person can change without writing a query, and something an assistant can build. Rendering it
to text, rather than giving it a door of its own, keeps one engine under every report: a composition reads
through the same door as any query, rights and row restrictions included, and any composed report is
reproduced by pasting its text into `New Query`.

## What it guarantees

- **The text is the only seam down.** The composer writes a query and reads its result through the
  ordinary query machinery; it has no execution of its own. What the text cannot carry (parameters, the
  page asked for) travels beside it.
- **Composing is a read, and it runs like one.** It is rented a background run with a connection of its
  own: the window stays live and says it is busy, and a form that closes cancels it, so a report nobody
  waits for stops holding a connection.
- **The document comes whole.** The report composes into a document of its own and replaces the shown one
  in one step, with every cell's value and its drill-down parameters - a report is never seen half-built.
- **Every figure can be asked where it came from.** A cell keeps the value it was composed from: it opens
  that value, or asks the report's `OnDetailProcessing` for a detail.
- **The reader's settings are a copy.** The settings the configuration holds are the default of the
  reader's; running a report never writes into the configuration. A variant is a named setting: choosing
  one sets the reader's setting, and a hand edit after it simply is no longer that variant.
- **A refusal is seen where Compose was pressed**, in the engine's words, not in a log nobody opened.
- **A list becomes a report for nothing.** Every list, tabular section and value table can print what is
  on screen, in its order, filter and grouping, onto a spreadsheet document.

## Where it stops

- A list has no variants: its setting is whatever the reader narrowed it to.
- The composer adds no capability to the query language. What a query cannot express, a composition
  cannot ask for ([query-language.md](query-language.md)).
