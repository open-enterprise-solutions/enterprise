# Event handlers

**What.** An `EventHandler` is a configuration object that handles one event of the objects it names,
in a module of its own. It sits under *Common → Event handlers*.

**Why.** A rule that holds for many objects — check every document before it is written, stamp every
catalog item when it is copied, narrow every list form — would otherwise be a call pasted into each
object's module. The next object added forgets it, and finding out who reacts to an event takes a search
of the code. A handler is declared once. The objects are not edited, and who handles what is read off the
handlers.

## Declaring one

| Property | Holds |
|---|---|
| `Source` | What raises the event: one type (`CatalogObject.Goods`), every type of one kind (`DocumentObject` is every document's object, `CatalogManager` every catalog's manager, `AccumulationRegisterRecordSet` every such register's record set), or several types |
| `Event` | One of the events the source raises. The list follows the source |
| `HandlerModule` | The handler's own module |

The module is prepared with one procedure. It is named as the event, and it takes `Source` (the value that
raised the event) before the event's own arguments:

```
// Source: DocumentObject   Event: BeforeWrite
Procedure BeforeWrite(Source, Cancel, WriteMode, PostingMode)
{
    If (Source.DeletionMark) {
        Cancel = True;          // the document is not written
    }
}
```

Trailing arguments the procedure does not read may be left off: `BeforeWrite(Source, Cancel)` works too.

A record set (a register's, a sequence's) raises `BeforeWrite` and `OnWrite` with `(Cancel, Replacing)`, and
`BeforeDelete` and `OnDelete` with `(Cancel)`. `Replacing` says whether the write replaces what the set's filter
holds.

From an assistant over MCP: `metadata_create` with `kind: "EventHandler"`, `Source` through
`metadata_set_type` (one type, or several through `description`), `Event` through `metadata_set`, then
`module_write` into the module named by `HandlerModule`.

## When it runs

- **Wherever the event is raised**: a write from a form, from code, from a job. Object events go through
  one door. The source's own module runs first, then every handler whose `Source` admits the value's type
  and whose `Event` matches.
- **With the same arguments.** `Cancel = True` in a handler cancels the write, as it does in the object's
  own module. `StandardProcessing = False` refuses in the same way.
- **In cascade.** A handler that writes another object raises that object's events, and their handlers
  run in turn.

## Several types in one source

Arguments are passed by position. So when `Source` names several types, `Event` offers only the events
that every type raises under the same name and with the same number of arguments. A catalog's
`BeforeWrite(Cancel)` and a document's `BeforeWrite(Cancel, WriteMode, PostingMode)` differ, so a handler
of both cannot take `BeforeWrite`. It can take `OnWrite`, `BeforeDelete`, `OnDelete`, `Filling` and
`OnCopy`.

## Manager events

A manager raises events of its own. A handler takes them with a manager type as its source
(`CatalogManager.Goods`, or `CatalogManager` for every catalog). `Source` is then the manager.

| Event | Raised | The answer |
|---|---|---|
| `FormGetProcessing(Form, Cancel)` | when a form of the object has been made: rights checked, form module initialised, nothing shown yet | `Cancel = True`: the form is not handed out |
| `ChoiceDataGetProcessing(ChoiceData, Parameters, StandardProcessing)` | on a quick choice or on text typed into a reference field (catalogs, documents, charts, enumerations, scheduled jobs). `Parameters.SearchString` is the text typed | `StandardProcessing = False`: the list offered is the `ChoiceData` array the procedure filled, and no search runs |
| `JobProcessing(Job)` | on every run of a scheduled job (`ScheduledJobManager`) | — |

`FormGetProcessing` is raised for every metatype that has a manager module. `ChoiceDataGetProcessing` is
raised only for a user who may see the object's data.

## What it guarantees

- **No second call to forget.** An object event reaches its handlers through the same door as its own
  module's procedure.
- **A family reaches objects added later.** `DocumentObject` admits a document created after the handler.
- **The choice of event survives a change of source.** `Event` keeps a number made from the event's name,
  not its position in the list.
- **Only what raises an event is offered as a source.** A report's or a data processor's object declares
  none and is not offered.

## Where it stops

- Several types are matched by the name and the number of arguments. Argument names are not compared.
- Handlers of one event run in the configuration's order. There is no priority.
- Nothing guards against a handler raising its own event again, the same as in an object's module.
- A manager event needs the object's manager module. An object without one, such as a constant, raises
  no manager events.
- A form's and a control's own events stay in the form's module. Handlers do not see them.
