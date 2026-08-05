# Session parameters — values that exist once per session

> **Status:** built and exercised by hand 2026-08-05 — declared, set from the session
> module, read from a filter, and refused outside it. Metatype, session module, storage
> and the global object are in place. No automated test of its own yet; the existing
> suite (1069) stays green.
> Companions: [access-policy-rls.md](access-policy-rls.md) (the reason they exist),
> [session-ownership.md](session-ownership.md), [metadata-lifecycle.md](metadata-lifecycle.md).

---

## 1. What they are

A **session parameter** is a named, typed value that exists once per session: set
when the session starts, read everywhere, unchanged afterwards.

```c
// module of the session — the only place a write is allowed
Procedure SetSessionParameters()
{
    SessionParameters.Organisation = FindOrganisationFor(...);
    SessionParameters.Period       = BegOfMonth(CurrentDate());
}

// anywhere else — read
Source = Source.Where(Function(x) { Return x.Organisation = SessionParameters.Organisation; });

// anywhere else — write
SessionParameters.Organisation = other;   // raises
```

---

## 2. Why the platform needs them at all

Not for substitution. Row access here is a **decorator**: a handler folds a `Where`
into the source, and the lambda **captures** the value it compares against — the
captured value reaches the query as a parameter by itself. Nothing has to be
declared for that to work.

What was missing is narrower: **somewhere for the value to live for the length of a
session**, set before the first read and unchanged after it. "The current
organisation" is not an attribute of the user — it is a choice made at login, and
there was no place to keep it.

The second half is protection. Row access is filtered **by these values**, so an
ordinary variable would be a way around the policy in one line:

```c
CurrentOrganisation = otherOrganisation;   // and the filter now lets the other rows through
```

Hence the write window rather than a right: writable **only while the session
module runs**, closed before and after, for everybody.

---

## 3. A parameter IS an attribute

`ibValueMetaObjectSessionParameter` derives `ibValueMetaObjectAttribute`. That is
the whole design: an attribute is already "a declared name with a type, its
qualifiers and an editor", which is exactly what is being declared. Type
description, designer property page, serialisation and **AdjustValue** arrive
finished; a metatype of its own would have to be taught all four again.

What differs is the **owner**, not the shape. A catalog's attribute is a column —
it belongs to a table, is stored per row, and is asked of an object. A session
parameter belongs to the session: no table, no column, no row. No restructuring
pass reaches it, and not because it says so — a table is built from the attributes
of an owner that has one, and this one's owner is the configuration.

In the tree it sits under **Common**, beside the jobs, for the same reason those
do: it belongs to the configuration as a whole and to no business object.

---

## 4. The manager and the unit

The same shape scheduled jobs have — a manager in the global context, units inside.

| | |
|---|---|
| `ibValueSessionParameters` | the **manager**: reached as `SessionParameters`, names read from the metadata every time |
| `ibValueSessionParameter` | one **unit**: holds its declaration, manages that parameter's value on the session |

The manager holds **nothing** — no values, no cached list of declarations. A cached
list would go stale the moment a parameter is added in the designer, and the editor
would complete a name the runtime refuses.

The unit is what talks to the session:

* **read** — the stored value, or, when never set, the declared type's own empty.
  An unset reference parameter is an empty reference *of that kind*, so a
  comparison against it narrows the rows to none — the safe direction.
* **write** — through the declaration's `AdjustValue` (scale, date truncation, a
  reference of the wrong kind), then to the session, which refuses it outside the
  module by raising.

Values live on `ibSession`, keyed by name. Two users signed in at the same moment
hold different ones, and that isolation is structural rather than a rule anybody
keeps.

**Both are registered types**, and the unit's registration is not paperwork: any value
a script can hold gets asked for its type sooner or later, and that question resolves
through the ctor registry (`GetClassName` → `GetTypeIDByRef` → assert). The debugger
asks it of every value it displays, which is where a missing registration surfaces —
long after the value itself works fine. The two names differ on purpose: the metatype
`SessionParameter` describes the **declaration**, `SessionParameterValue` is what a
script holds.

---

## 5. When the module runs

```
authentication
   ├─ metadata loaded              (OnFirstConnect)
   ├─ CreateRoot                   (EnsureRoot)
   └─ CompileRoot
        ├─ modules compiled
        ├─ AttachRuntime
        ├─ SetSessionParameters    ← trusted window, write window open
        ├─ RLS policy built
        └─ modules run
   ─────── interactive client only ───────
   beforeStart(Cancel) · onStart
   home page
```

Three properties of that position, each deliberate:

* **After authentication** — before it there is no user and no roles to resolve.
* **Before the policy** — the policy filters by what the module sets.
* **In `CompileRoot`, not beside `beforeStart`** — every kind of session passes
  through here, including background and scheduled jobs, which never fire the
  interactive events at all.

The module runs inside `ibAccessTrustScope`: it reads data itself (find the
organisation for this user, the period), and at that moment there is nothing to
filter those reads by. The same door the role modules use, for the same reason.

**A failure is not fatal.** A broken `SetSessionParameters` leaves the parameters
empty and logs why; a policy written against an empty parameter narrows to nothing,
which is the safe direction. Locking everybody out of the base would not be.

---

## 6. Where it lives

| File | What |
|---|---|
| `metaCollection/metaSessionParameterObject.{h,cpp}` | the metatype, the manager and the unit |
| `metaCollection/metaSessionParameterObject_res.cpp` | the icon — the attribute's, in amber |
| `metaCollection/metaObjectMetadata.{h,cpp}` | the **session module**, a second module property on the root |
| `session/sessionParameters.cpp` | the store, the write window, and the call |
| `session/session.h` | the map + the window flag, declared beside the rest of the session |
| `moduleManager/globalContextManager.cpp` | `SessionParameters` in the global context |

The session module is a **manager module** (`ibValueMetaObjectManagerModule`), not a
plain one — a plain module never registers itself with the module storage, so it is
never compiled into a session and its procedure can never be called. It would exist
in the tree, open in the editor, and quietly do nothing.

It registers **after** the configuration module has put the main module into the
compile cache (`ibValueMetaObjectConfiguration::OnBeforeRunMetaObject`). Being an
ordinary common module whose only peculiarity is starting earlier than the
before-start events, it is the one module that could otherwise land in a manager
that has no main module yet.
