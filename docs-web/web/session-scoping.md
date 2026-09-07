# One process, many sessions — what was shared that should not have been

*2026-09-07.*

The web server holds many sessions in one process. The desktop holds one per
window, and the frontend was written for that: state that is "the whole
program's" there is one session's here, and where the two were confused the
second session took the first one's.

## The open-document registry

`ibFormVisualDocument` registers itself in `s_createdDocFormArray`, a static set
in `visualHostClientDocView.cpp`, and five lookups walk it:
`FindFormByUniqueKey` (by form key, by control guid, by source guid),
`FindDocByUniqueKey`, `UpdateFormUniqueKey`.

A list form's key is stable — it comes from the metaobject, not from the
session. So the second session to open Goods reached
`ibValueForm::CreateDocForm`, hit

```cpp
ibFormVisualDocument* founded = ibFormVisualDocument::FindDocByUniqueKey(m_formKey);
if (founded != nullptr) { founded->Activate(); return true; }
```

found the **first session's** document, activated a tab in somebody else's
window, and returned success. The session that had asked ended up with no tab at
all, so `GetActiveHost()` saw zero tabs and `POST /open-meta/<id>` answered `{}`
— while the log still printed `[tabs] CreateNewForm`, because the form object
had been made. That is what made it look like a form-creation failure for two
days: everything up to the last step really did happen.

The fix is one question asked in all five places: a document remembers the
session that made it (`ibSession::Current()` at construction) and a lookup skips
documents belonging to another. A document made outside any session answers to
everyone — a null owner means "belongs to nobody", not "private to nobody" — so
the desktop, where one window owns one session, is unchanged.

Verified with five sessions opening the same list over HTTP and with three
concurrent browsers: each gets its own tab, its own toolbar and its own rows,
and one opening an object leaves the others alone.

## What else to look at when something is shared

`grep` for `static` collections in `visualView/` before assuming a symptom is
per-session. The two that are known-good and deliberately process-wide are the
metadata image and the type-ctor registry — those genuinely are the program's.
`ibVisualHost`'s children, the document registry and anything holding an
`ibValueForm*` are not.

Related: `CreateChildFrame` used to return `nullptr` silently when the current
session had no web frame, which is the same class of failure — a form that opens
into nobody's window. It now says so on stderr.
