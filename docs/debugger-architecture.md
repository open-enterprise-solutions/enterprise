# Debugger — transport, roles, and why the server is the debuggee

> **Scope:** how the Designer debugs a running process — the TCP transport, the
> client/server inversion, the per-connection threading and the connection manager.
> Companion: [debugger-per-session.md](debugger-per-session.md) (per-session sessions, the
> wake/drain protocol, one open heisenbug).
> This is foundation code — it predates most arcs in this folder.

---

## 1. Lineage — why TCP at all

**The Designer was written from scratch** (unlike the form editor, which descends from
wxFormBuilder — [form-editor.md § 1](form-editor.md)). The debugger was designed with it.

At the time this was first built, the available way to drive another process's debugger on
Windows was **DDE** — hook the target's window events and post commands into it. That was
rejected and replaced with a plain **TCP/IP client that ships bytes**: commands that drive
the runtime.

What the transport choice bought:

- **No window, no coupling.** A headless runtime (`daemon.exe`, `codeRunner.exe`,
  `wenterprise-server.exe`) has no message loop to hook — DDE could never have debugged
  them. TCP does.
- **Across the network.** The debuggee need not be on the same machine.
- **Multi-threading.** Each debug session is its own connection on its own thread, so
  sessions do not block each other (§4).

The port is fixed: `#define defaultDebuggerPort 1650` (`backend/debugger/debugDefs.h`).

---

## 2. The inversion — the debuggee is the SERVER

This is the counter-intuitive part, and it was a real fork in the road.

**The intuitive design is Designer-as-server:** the Designer is the tool, the authority,
the thing you sit in — so let it listen, and let runtimes report to it.

It does not work. If the Designer is the server, **a running process has no way to know
where to connect** — and, symmetrically, the Designer cannot reach *into* an existing
session, because being the server means waiting to be told. Debugging is initiated from the
Designer against a process that is *already running*; a server cannot go find its clients.

So the roles are inverted:

| Role | Who | Class |
|---|---|---|
| **Server** | the **debuggee** — every runtime under debug | `ibDebuggerServer` (`debugServer.{h,cpp}`, ~1600 lines) |
| **Client** | the **Designer** | `ibDebuggerClient` (`debugClient.{h,cpp}`, ~1400 lines) |

**Every process that can be debugged is a server**, listening on 1650. The Designer is a
client that goes out and connects to them. Read it as: *the debuggee offers itself; the
tool reaches out.*

Both classes live in `backend.dll` — the debugger is not a Designer feature, it is a
platform capability the Designer consumes.

---

## 3. Connection roles — `ConnectionType`

A client connection is not always a debug session. `debugDefs.h`:

```cpp
enum ConnectionType {
    ConnectionType_Scanner,
    ConnectionType_Waiter,
    ConnectionType_Debugger,

    ConnectionType_Unknown = 100
};
```

- **Scanner** — the discovery role, and the answer to "where do I connect?". A new
  connection starts as `ConnectionType_Scanner` (see the constructor in §4): the client
  probes for debuggees rather than being told about them.
- **Waiter** — parked, waiting for a debuggee to appear / become ready.
- **Debugger** — an attached, live debug session.

The `Unknown = 100` gap is the usual id spacing.

---

## 4. One thread per connection, and a manager

```cpp
class BACKEND_API ibDebuggerClientConnection : public wxThread {
    ibDebuggerClientConnection(ibDebuggerClient* client, const wxString& hostName, unsigned short port)
        : wxThread(wxTHREAD_DETACHED),
          m_verifiedConnection(false), m_hostName(hostName), m_port(port),
          m_socketClient(nullptr), m_number_connection_attempts(-1),
          m_connectionType(ConnectionType::ConnectionType_Scanner)
    {
        if (debugClient != nullptr) debugClient->AppendConnection(this);   // self-register
        wxThread::SetPriority(wxPRIORITY_MIN);
    }

    ~ibDebuggerClientConnection() {
        if (debugClient != nullptr) debugClient->DeleteConnection(this);   // self-deregister
        if (m_socketClient != nullptr) m_socketClient->Destroy();
    }
};
```

Four decisions worth keeping:

- **Each debug session is a separate connection on a separate thread.** They are
  independent by construction — one session sitting on a breakpoint cannot stall another.
- **Detached threads** (`wxTHREAD_DETACHED`): a connection cleans itself up; nobody joins
  it.
- **Minimum priority.** Debug plumbing must never compete with the UI or the runtime it is
  observing.
- **Connections register themselves** with the manager in the constructor and remove
  themselves in the destructor. Ownership and the registry stay in sync without a
  bookkeeping step a caller could forget.

`ibDebuggerClient` is that manager — the "debug session manager":

```cpp
std::vector<ibDebuggerClientConnection*> m_listConnection;   // every connection, whatever its role
const std::vector<ibDebuggerClientConnection*>& GetListConnection();
// AppendConnection / DeleteConnection / AttachConnection / DetachConnection(kill)
```

Attach/detach look a connection up in the list and forward — so "attach to this session"
and "kill that session" are manager verbs, not socket verbs.

Health is checked defensively, not optimistically:

```cpp
bool IsConnected() const {
    if (m_socketClient == nullptr)          return false;
    if (!m_socketClient->IsConnected())     return false;
    if (!m_socketClient->IsOk())            return false;
    wxSocketError error = m_socketClient->LastError();
    return error == wxSOCKET_NOERROR || error == wxSOCKET_WOULDBLOCK;
}
bool IsVerifiedConnection() const { return m_verifiedConnection && IsConnected(); }
```

`wxSOCKET_WOULDBLOCK` counts as **healthy** — it means "nothing to read right now", which
is the normal state of an idle debug link. Treating it as an error would drop every quiet
session. And *verified* is stronger than *connected*: a socket that is up but has not
completed the handshake is not yet a debuggee.

The server side mirrors the shape: `ibDebuggerServer` owns
`ibDebuggerServerConnection : wxThread`.

---

## 5. The protocol surface

Commands arrive as events on `ibDebuggerClientAdapter : wxEvtHandler` — the client's
inbound vocabulary, i.e. what a debuggee can tell the Designer:

| Command | Means |
|---|---|
| `OnSessionStart` / `OnSessionEnd` | a debuggee session appeared / went away |
| `OnEnterLoop` / `OnLeaveLoop` | **stopped** at a line / resumed — the breakpoint pause |
| `OnSetStack` | the call stack for the stopped frame |
| `OnSetLocalVariable` | the Locals window |
| `OnSetVariable` / `OnSetExpanded` | the Watch window — value, and expanding a node |
| `OnSetToolTip` | hover-a-variable evaluation |
| `OnAutoComplete` | autocomplete resolved **in the live runtime** |
| `OnMessageFromServer` | a message from the debuggee |

Payloads are typed structs (`debugDefs.h`): `ibDebugLineData`, `ibStackData`,
`ibLocalWindowData`, `ibWatchWindowData`, `ibDebugExpressionData`,
`ibDebugAutoCompleteData`.

`OnEnterLoop` / `OnLeaveLoop` are the heart of it: "enter loop" is the debuggee stopping
and pumping the debug loop instead of running ([debugger-per-session.md](debugger-per-session.md)
for the wake/drain protocol and its `notify_all` details).

`ibDebuggerClientBridge` (`debugClientBridge.{h,cpp}`) is the seam between the adapter and
whatever UI is attached — the adapter owns the bridge (`SetBridge` deletes the previous
one, the destructor deletes the current).

### 5.1 Evaluated values — the hard part, and why the bridge exists

**The difficult problem in this debugger was the *evaluated value*** — Watch. You type an
expression and it must be computed **in the live runtime**, at the frame where it is
stopped, and come back. Everything else here (threads, events, the bridge) is the residue
of solving that.

It arrived in three steps, each fixing the previous one's damage:

1. **Synchronous → it hung.** Ask, wait for the answer inline, and there is no good moment
   to catch the reply arriving; the UI stalls.
2. **Threads → the GUI corrupted.** Moving each session onto its own thread (§4) fixed the
   hang and created a worse bug: threads wrote into the GUI **in parallel**, and the UI
   state got trampled. This is the standard wx rule — only the main thread may touch
   widgets — but it is exactly what a debug reply wants to do.
3. **An event system + a bridge → correct.** The worker never touches the UI. It raises an
   event; the answer is delivered **to the form**, and the form applies it itself.

Three pieces of the code are that decision:

```cpp
class wxDebugEvent : public wxEvent {
    EventId m_eventId;
    virtual wxEvent* Clone() const override { return new wxDebugEvent(*this); }   // ← required to cross threads
};
```

`Clone()` is not decoration: wx copies an event to hand it from a worker to the main
thread's queue. An event that cannot clone cannot leave its thread.

```cpp
class BACKEND_API ibDebuggerClientBridge {
    virtual void OnSetVariable(const ibWatchWindowData& watchData) = 0;   // every method pure
    virtual void OnSetExpanded(const ibWatchWindowData& watchData) = 0;
    virtual void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr) = 0;
    …
};
```

The bridge is **entirely abstract** and lives in `backend.dll` — the debugger declares
*what it can tell you* and knows nothing about the window that will render it. The adapter
just forwards (`debugClientAsync.cpp` — every method is `if (m_debugBridge) m_debugBridge->…`).

And the reply carries its own destination:

```cpp
struct ibWatchWindowData {
    wxTreeItemId m_item;                 // ← the tree node this answer belongs to
    struct ibWatchWindowItem {
        wxTreeItemId m_item;
        wxString m_name;
        wxString m_value;
    };
};
```

**That `wxTreeItemId` is the whole trick.** The answer is addressed to the node that asked,
so the form can apply it on its own, in its own thread, whenever it gets it — no shared
cursor, no "which request was this for?", no worker reaching into a control. `OnSetVariable`
sets a value; `OnSetExpanded` fills children when a node is expanded — the Watch tree
expands **lazily against the live runtime**.

The event vocabulary that carries it (`debugDefs.h`):

```cpp
enum EventId {
    EventId_SessionStart = 1, EventId_SessionEnd = 2,
    EventId_EnterLoop = 3,    EventId_LeaveLoop = 4,     // stopped / resumed
    EventId_SetToolTip = 5,                              // hover-evaluate
    EventId_StartAutocomplete = 6, EventId_ShowAutocomplete = 7,
    EventId_SetData = 8,                                 // the evaluated answer
    EventId_MessageFromEnterprise = 9,
};
```

Autocomplete is two events, not one (`Start` / `Show`), for the same reason: the request
leaves, the answer returns later — nobody blocks.

> **The rule this encodes:** a debug reply is data addressed to a widget, delivered as an
> event, applied by the widget. If you add a command, give its payload the identity of what
> asked for it; do not reach back into the UI from the socket thread.

### 5.2 COM objects across the thread — marshaling

Evaluating in a worker thread has a second, sharper consequence: **a script variable may
hold a COM object** (`ComObject` / `ibValueOLE` —
[script-value-types.md § 2.8](script-value-types.md)). A COM `IDispatch` is bound to its
apartment; **a raw pointer cannot legally cross a thread**. Evaluate a Watch on it from the
debug thread and you are calling an interface from the wrong apartment.

The answer is real COM **inter-thread marshaling**, and it is fully implemented
(`valueOLE.{h,cpp}`, `__WXMSW__` only). The API is split by apartment, and says so:

```cpp
//STA
static void CreateStreamForDispatch();
static void ReleaseStreamForDispatch();
static void ReleaseComObjects();

//MTA
static void GetInterfaceAndReleaseStream();

friend class ibDebuggerServer;      // ← the debugger is the reason this exists
```

Every live OLE value is tracked in a registry, so marshaling is a **sweep, not a
one-off**:

```cpp
static std::map<IDispatch*, ibValueOLE*> gs_valueOLE;
```

**STA side** — package every live dispatch into a stream before the other thread needs it:

```cpp
void ibValueOLE::CreateStreamForDispatch() {
    for (auto dispOle : gs_valueOLE) {
        ibValueOLE* oleValue = dispOle.second;
        if (oleValue->m_streamDispatch) continue;
        HRESULT hr = ::CoMarshalInterThreadInterfaceInStream(IID_IDispatch, oleValue->m_dispatch,
                                                             &oleValue->m_streamDispatch);
        if (FAILED(hr)) oleValue->m_streamDispatch = nullptr;
    }
}
```

**MTA side** — unpack it in the thread that will actually call:

```cpp
void ibValueOLE::GetInterfaceAndReleaseStream() {
    for (auto dispOle : gs_valueOLE) {
        ibValueOLE* oleValue = dispOle.second;
        if (!oleValue->m_streamDispatch) continue;
        HRESULT hr = ::CoGetInterfaceAndReleaseStream(oleValue->m_streamDispatch, IID_IDispatch,
                                                      (void**)&oleValue->m_currentDispatch);
        if (SUCCEEDED(hr)) oleValue->m_streamDispatch = nullptr;
    }
}
```

Hence the three members: **`m_dispatch`** (the original, owned by its apartment),
**`m_streamDispatch`** (the marshaled packet in flight), **`m_currentDispatch`** (the proxy
usable in *this* thread).

The debug server drives all of it (`debugServer.cpp`):

```
:329   ibValueOLE::CreateStreamForDispatch();        // STA — before handing off
:355   ibValueOLE::ReleaseStreamForDispatch();       // STA — tear down
:945   ibValueOLE::GetInterfaceAndReleaseStream();   // MTA — inside the debug thread
```

**And the debug thread declares its apartment — this is what makes the whole thing legal.**
The very first act of a debug connection's thread body is to enter COM as
**multi-threaded**:

```cpp
wxThread::ExitCode ibDebuggerServer::ibDebuggerServerConnection::Entry()
{
#ifdef __WXMSW__
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);      // ← this thread is MTA
    if (FAILED(hr)) {
        wxLogSysError(hr, _("Failed to create an instance in thread!"));
    }
#endif
    // Mark this OS thread as a debug worker so ibSession::Current() …
```

with `::CoUninitialize()` in the same function's epilogue (`:815`; a later site at `:1377`
notes it is *already* done there — do not add a second).

That is why the `ibValueOLE` API is split `//STA` / `//MTA` rather than "before/after": the
**main thread is STA** (it owns the objects and the UI) and the **debug thread is MTA**.
Marshaling is the bridge *between two declared apartments* — without the
`COINIT_MULTITHREADED` on this side, the unmarshaled proxy would have nowhere valid to
live.

> This was hard-won. The sequence — evaluate on a worker → COM refuses to cross → register
> every live dispatch → marshal on STA, unmarshal on MTA, with the worker declaring itself
> MTA at `Entry()` — is what makes evaluating a COM value in a Watch window possible at all.
> Treat the three pieces (registry, apartment declaration, marshal/unmarshal pair) as one
> mechanism; removing any one of them breaks it silently, at runtime, on a user's machine.

So: **a COM value in a Watch window is a marshaled proxy, not the object.** The seam is
Windows-only (`#ifdef __WXMSW__`) because COM is; on other platforms `ComObject` does not
arise.

> This is the part where "evaluate a value" stops being a debugger feature and becomes a
> threading contract. If evaluation ever moves threads again (a compute-server tier —
> [compute-server-tiering.md](compute-server-tiering.md)), this sweep is what has to move
> with it.

---

## 6. Lifecycle

Both singletons follow the same pattern, stated in the header:

```
// Lifecycle: owned by ibMetaDataConfigurationStorage as a unique_ptr field
// (private ctor + friend). Same cache-pointer pattern as ibDebuggerServer —
// designer / codeEditor hot paths read the static slot directly through the macro.
#define debugClient (ibDebuggerClient::Get())
```

The macro is a hot-path read, not a service locator: the code editor asks about breakpoints
constantly, so the static slot is read directly rather than resolved.

---

## 7. Honest remainder

- Web/debug transport unification is an open arc ([ROADMAP.md § 2](ROADMAP.md)); the TCP
  path described here is the live one.
- One open heisenbug (freed-memory at startup) — documented, with pinning instructions, in
  [debugger-per-session.md](debugger-per-session.md). Do not re-derive it.
- `m_number_connection_attempts` starts at `-1` (unlimited) — the retry policy for a
  scanner is worth verifying before relying on it.
