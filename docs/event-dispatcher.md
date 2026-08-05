# Event dispatcher — lambdas and named handlers behind one door

Any event (a control's `OnChange`, a form's `beforeClose`, a **command's Action**) can be handled two ways, chosen
per-binding and dispatched by plain polymorphism:

1. **Undefined** — nothing bound; firing is a no-op (the event proceeds).
2. **Named** — the classic handler: a form-module procedure called the old-fashioned way, bound by name. **Serialised.**
3. **Lambda** — an anonymous function attached at runtime (`ThisForm.OnChange = Function(...) EndFunction`).
   **Transient** — a runtime value, not serialised.

The caller never branches on the kind. It fires the event; the value behind it knows how to run.

---

## The core: a facet on the runtime values, not a wrapper

`ibEventDispatcher` (`backend/eventDispatcher.h`) is a **pure interface** — one method:

```cpp
class BACKEND_API ibEventDispatcher {
    virtual bool Dispatch(ibProcUnit* runtime, ibValue** args, long argc, ibValue& outCancel) = 0;
    virtual bool IsEmpty() const { return false; }
};
```

It is **not** a class hierarchy of its own — the existing runtime values *implement* it, so each is simultaneously the
value **and** its own dispatcher (no reinvented wheel):

| Runtime value | File | `Dispatch` does |
|---|---|---|
| `ibValueEvent` (a named-event value) | `system/value/valueEvent.{h,cpp}` | `runtime->CallAsProc(m_eventName, args…, cancel)` |
| `ibValueFunction` (a lambda value) | `compiler/procUnitValues.h` + `compiler/procUnitLinq.cpp` | `CallLambdaWithArgs(*this, args…, cancel)` (the session's lambda runtime) |

Both are `class X : public ibValue, public ibEventDispatcher`. The upcast `X* → ibEventDispatcher*` is **implicit**, so
resolving an event's value to its dispatcher needs **no cast**.

### Why `Dispatch` is non-const
Dispatch is an **action** (it runs code), not a query. A named event doesn't mutate itself; a lambda invoke *may*
(heap-promoting its captured frames). The interface is non-const so the mutating case is legal without `const_cast`
(which is a model error here) — the non-mutating case simply doesn't mutate.

### The trailing cancel contract
`outCancel` is the **last** parameter, passed **by reference** — the handler may set it to stop the default action,
exactly as the old `CallAsProc(name, args…, cancel)` did. `Dispatch` returns that flag.

---

## The property: `ibEventControl` holds the dispatcher value

The dispatcher lives on the **concrete** control event, not the abstract base:

- `ibEvent` (base, `propertyManager/propertyObject.h`) declares it **pure**:
  `virtual ibEventDispatcher* GetDispatcher() const = 0;` — every event decides how it dispatches; the base carries no
  storage. (`ibEventAction` returns `nullptr` — it runs via its own functor `Invoke`, never through `CallAsEvent`.)
- `ibEventControl` (`propertyManager/property/eventControl.{h,cpp}`) implements it and **holds the value**:
  `mutable ibValue m_dispatcherValue` — an `ibValueFunction` (a lambda assigned from script) or an `ibValueEvent`
  materialised lazily from the stored name.

```cpp
ibEventDispatcher* ibEventControl::GetDispatcher() const {
    if (ibValueFunction* fn = AsFunction(&m_dispatcherValue))   // a lambda -> its own dispatcher
        return fn;
    ibValueEvent* ev = m_dispatcherValue.ConvertToType<ibValueEvent>();
    if (ev == nullptr) {                                        // materialise the named event, kept alive here
        m_dispatcherValue = ibValue::CreateObjectValue<ibValueEvent>(m_propValue.GetString());
        ev = m_dispatcherValue.ConvertToType<ibValueEvent>();
    }
    return ev;
}
```

`SetDataValue` / `GetDataValue` are **symmetric**:
- **set** a function → hold the lambda (`m_dispatcherValue = fn`); set a named event → store the name (`m_propValue`)
  and drop the cached dispatcher.
- **get** → the **lambda** if one is assigned (so the runtime reads a real callable it can re-invoke), else the named
  event value from the name.

`DoSetValue` (a name edit) drops `m_dispatcherValue` so `GetDispatcher` rebuilds the named dispatcher from the new name.
Serialisation writes **only the name** (`ReadNodeValue` / `WriteNodeValue`) — a runtime lambda is never persisted.

---

## The fire site: one door

`ibValueFrame::CallAsEvent` (`frontend/visualView/ctrl/frame.h`) is the single point every control (and a command's
Action, via `ExecuteValueByPath → CallAsEvent`) fires through. It no longer inspects the value — it asks and dispatches:

```cpp
template <typename ...Types>
bool CallAsEvent(const ibEvent* event, Types&&... args) const {
    if (event == nullptr) return false;
    ibEventDispatcher* dispatcher = event->GetDispatcher();
    if (dispatcher == nullptr || dispatcher->IsEmpty()) return true;   // undefined -> no-op
    ibValue* argPtrs[] = { (&args)..., nullptr };                      // trailing null keeps a 0-arg array valid
    ibValue eventCancel = false;
    try { return dispatcher->Dispatch(GetFormProcUnit().get(), argPtrs, (long)sizeof...(args), eventCancel); }
    catch (...) { return false; }
}
```

A button carries only a **command** now (its own user event is retired); the command's **Action IS an `ibEventControl`**
(`ctrl/formCommand.h`), so a button-through-a-command reaches the very same door — a command's Action can be a lambda too.

---

## Lifetime

A lambda bound to an event lives as long as **the event holds it** (`m_dispatcherValue` keeps the function value alive)
— and the event lives as long as **its owner** (the control / command). Destroy the owner → destroy the event → drop
the lambda. It is additionally valid only while its **runtime context** (the module bytecode it references,
`ibValueFunction::m_parentBc`) is alive — i.e. within the run. This is exactly the "lives for the duration of execution,
as long as the owning object lives" model.

---

## Using it (script)

```
// classic — a named form procedure, the old way (serialised)
ThisForm.OnChange = New Event("MyOnChangeProc");

// lambda — attached at runtime, transient
ThisForm.OnChange = Function(Value, Cancel)
    // ... react ...
    Cancel = True;   // trailing cancel, by ref — stops the default action
EndFunction;

// reading back sees WHICH kind is bound
Handler = ThisForm.OnChange;   // the function value (a lambda) or the named-event value
```

The lambda is the **easy subset** of the language's anonymous functions: it compiles as a top-level callable with the
event's parameter signature plus `ThisForm` context, so it needs **no closure capture** — though closure capture is
itself available since 2026-05-12 (see [lambda.md](lambda.md), [closure-capture.md](closure-capture.md)); the event
lambda simply does not depend on it.

---

## Files

| Concern | File |
|---|---|
| The interface | `backend/eventDispatcher.{h,cpp}` |
| Named value = dispatcher | `backend/system/value/valueEvent.{h,cpp}` |
| Lambda value = dispatcher | `backend/compiler/procUnitValues.h`, `backend/compiler/procUnitLinq.cpp` (`ibValueFunction::Dispatch`) |
| Base declares it (pure) | `backend/propertyManager/propertyObject.{h,cpp}` |
| Property holds it | `backend/propertyManager/property/eventControl.{h,cpp}` |
| Action event opts out | `backend/propertyManager/property/eventAction.h` (`GetDispatcher` → `nullptr`) |
| Fire site | `frontend/visualView/ctrl/frame.h` (`CallAsEvent`) |
