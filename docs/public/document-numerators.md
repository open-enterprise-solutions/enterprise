# Document numerators

**What.** A `DocumentNumerator` is one number series that several kinds of document take their numbers
from: an invoice and a credit note numbered as one sequence, a receipt and a payment in one journal. It
sits under *Documents → Document numerators*.

**Why.** Each kind of document is a series of its own. When the law or the book-keeping asks for one
sequence across several kinds, the alternative is a counter kept by hand in a module, which every kind
must remember to use and which knows nothing of periods or of the numbers already written.

## What it is

| Property | Holds |
|---|---|
| `Number` | What a number of the series is: a string or a number, and its length. String(11) by default |
| `Number periodicity` | When the series starts again: nonperiodical (the default), or each year, quarter, month or day of the document's date |
| `Check unique` | Whether a write is refused when another document of the series already has the number in the period. On by default |

**Membership is kept on the member.** A document names its numerator in its own `Numerator` property; the
numerator keeps no list. While it is set:

- the document's `Number` has the numerator's type and length, and its own *Number periodicity* and
  *Number* type are not shown — they come back as they were when the property is cleared;
- the next number is the next one of the shared series, whichever kind took the last;
- the first number of a series (or of a new period) is read from what every member's table already holds.

A document that names no numerator numbers as before, on its own counter.

## What it guarantees

- **One counter.** The series is one row of `sys_sequence`, keyed by the numerator, by the period and by
  the prefix — the same table and the same increment a lone document uses, inside the write's transaction.
- **A taken number is refused.** With *Check unique* on, a write whose number another document of the
  series already has in the same period raises before anything is saved, and the message names that
  document and the numerator. A number typed by hand is checked under the series row, as a generated one
  is. The check is not filtered by the writer's rights: a number is taken whoever may see its document.
- **A numerator in use is not deleted.** Removing a numerator a document names is refused, naming the
  first such document; clear the document's `Numerator` first.

## Where it stops

- The prefix still splits the series: two documents with different prefixes number apart.
- On Firebird, two sessions taking the next number of one series at the same moment can meet an update
  conflict on the counter row, as two writers of one document kind already can; the write is refused and
  may be repeated.
- A document cannot be moved to another numerator while keeping its old numbers checked against the old
  series: the check reads the numerator named now.
