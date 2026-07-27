# Debugger — transport, roles, and why the server is the debuggee

> **Scope:** how the Designer debugs a running process — the TCP transport, the
> client/server inversion, the per-connection threading and the connection manager, **how
> the interpreter decides to stop** (§4.1), the protocol and its two hard problems
> (evaluated values §5.1, COM across apartments §5.2), editing while stopped (§5.3), what
> "verified" actually guarantees (§7), and what the debugger **cannot** do (§8).
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

### 4.1 How the runtime decides to stop

Everything above is transport. This is the mechanism — the part that turns a running
interpreter into a stopped one.

There is exactly **one** hook, and it sits in the interpreter's hot loop, before the opcode
switch (`compiler/procUnit.cpp`):

```cpp
//enter in debugger
if (debugServer != nullptr && !evalMode)
    debugServer->EnterDebugger(pContext, curCode, lPrevLine);
```

`!evalMode` is load-bearing: a Watch expression is itself executed by the interpreter, and
without this gate evaluating a watch would re-enter the debugger from inside the debugger.

`EnterDebugger` (`debugServer.cpp`) then decides, per instruction, whether to park. Four
gates, cheapest first:

| # | Gate | Why it is first |
|---|---|---|
| 1 | `if (!m_bUseDebug) return;` | Debugging off = one atomic read per instruction. This is what keeps the hook affordable in production builds. |
| 2 | `IsSteppableOpcode(m_numOper)` | Housekeeping opcodes (`OPER_CTX_BEGIN` / `OPER_CTX_END` / …) are not user-visible steps. Stopping on them would step "into" nothing. |
| 3 | `byteCode.m_numLine != numPrevLine` | **Stop on a line CHANGE, not per instruction.** One source line compiles to many opcodes; without this the debugger would stop several times on the same line. `numPrevLine` is carried by the interpreter frame, not the server — it is per-execution state. |
| 4 | a stop *reason* below | Only now does anything cost real work. |

The stop reasons, in priority order:

```cpp
//step into
if (m_bDebugStopLine && byteCode.m_numLine >= 0) { m_bDebugStopLine = false; m_bDoLoop = true; }

// step through  (= step over)
else if (auto* st = ibSession::GetPUState();
         st && m_numCurrentNumberStopContext
            && m_numCurrentNumberStopContext >= st->GetCountRunContext()
            && byteCode.m_numLine >= 0)
{ m_numCurrentNumberStopContext = st->GetCountRunContext(); m_bDoLoop = true; }

else { /* arbitrary breakpoint: look the line up in this module's list */ }
```

- **StepInto** is a one-shot flag: *stop at the next steppable line, whatever frame it is
  in.*
- **StepOver** is a **call-depth comparison**, not a line calculation:
  `m_numCurrentNumberStopContext >= st->GetCountRunContext()` — resume, and stop again as
  soon as the frame stack is **no deeper** than it was when the user pressed the key. A
  called function runs to completion because while inside it the depth is greater. This is
  the right way to build step-over, and it is worth knowing that **the depth machinery for
  StepOut is therefore already present** — see §8.
- **Breakpoint** is a lookup in `std::map<wxString, std::vector<unsigned int>>` keyed by
  module doc-path, then a linear `std::find` over that module's line vector. The vector is
  fine at human breakpoint counts; it is a linear scan **per line change**, so it is the
  thing to change first if breakpoints ever become numerous (a sorted vector +
  `binary_search`, or a `set`).

When a reason fires, `DoDebugLoop(fileName, docPath, line + 1, runContext)` parks the
script thread on the per-session condition variable — see
[debugger-per-session.md](debugger-per-session.md) for the wake/drain half.

> **Read the shape as:** the interpreter offers every instruction to the debugger; the
> debugger rejects almost all of them in two atomic reads. The expensive question — "is
> there a breakpoint here?" — is asked only when the source line actually changed.

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

### 5.3 Editing while stopped — the line-shift protocol

A breakpoint is stored as a **line number**, and the user edits the module while the
process is parked on it. Every insert or delete above a breakpoint invalidates it silently
unless something shifts it. `ibDebuggerClient::PatchModule` (`debugClient.cpp`) is that
something:

```cpp
void ibDebuggerClient::PatchModule(const wxString& strModuleName, unsigned int line,
                                   int line_offset, bool atLineStart)
{
    // Two maps shift the same way for this edit, differing only in collision handling:
    // breakpoints collapse onto one line; the dense committed<->editor offset map keeps all lines.
    ShiftLineMap(*breakpoints, line, line_offset, atLineStart, /*collapseCollisions*/ true);
    ShiftLineMap(*offsets,     line, line_offset, atLineStart, /*collapseCollisions*/ false);

    // …then tell the debuggee, so ITS line numbers move too
    commandChannel.w_u16(line_offset > 0 ? CommandId_PatchInsertLine : CommandId_PatchDeleteLine);
}
```

Three things worth keeping:

- **Two maps, one edit, different collision rules.** `m_listBreakpoint` collapses — two
  breakpoints pushed onto the same line become one, because a line either has a breakpoint
  or does not. The dense `m_listOffsetBreakpoint` (committed ↔ editor line correspondence)
  keeps every line, because it is a mapping, not a set.
- **The shift is replicated to the debuggee.** The Designer moving its own markers is not
  enough — the running process holds its own breakpoint list, so the edit goes over the
  wire as `PatchInsertLine` / `PatchDeleteLine`.
- **The editor drives it, through a virtual.** `ibCodeEditor::OnPatchModule` is an empty
  hook in the base editor (`frontend/…/codeEditor.h`); `ibCodeEditorDesigner` overrides it
  and calls `debugClient->PatchModule`. codeRunner embeds the same editor with no debugger
  and pays nothing.

`InitializeModule(module, line_count)` seeds the dense map (every line → offset 0) when a
module is first opened; `SaveModule` re-bases the breakpoint list on commit.

### 5.4 Socket options

Set on every client connection once it comes up (`debugClient.cpp`):

```cpp
m_socketClient->SetOption(IPPROTO_TCP, TCP_NODELAY,  &flag, sizeof(flag));
m_socketClient->SetOption(SOL_SOCKET,  SO_KEEPALIVE, &flag, sizeof(flag));
```

`TCP_NODELAY` because the debug protocol is **bursty with tiny packets** (step, eval,
locals) — Nagle would batch them and add visible latency to every keypress. `SO_KEEPALIVE`
so a crashed or killed debuggee is detected in seconds rather than hours.

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

## 7. Verification is NOT authentication

`CommandId_VerifyConnection` reads like a security handshake. It is not one. What actually
happens (`debugServer.cpp` / `debugClient.cpp`):

```cpp
// debuggee answers a probe with four strings:
w_stringZ(activeMetaData->GetConfigGuid());     // configuration identity
w_stringZ(activeMetaData->GetConfigMD5());      // configuration content hash
w_stringZ(appData->GetUserName());              // informational
w_stringZ(appData->GetComputerName());          // informational

// Designer's verdict — GUID only:
m_verifiedConnection = activeMetaData->GetConfigGuid() == ibGuid(m_confGuid);
```

So *verified* means **"this process runs the same configuration I have open"** — a
correctness check that stops you from stepping through line 40 of a module the debuggee
never compiled. It is not a credential check: no secret is exchanged, and nothing proves
the client is entitled to attach.

Two consequences to hold on to:

- **The MD5 is sent, stored in `m_md5Hash`, and never used in the verdict.** GUID equality
  passes even when the two sides hold *different versions of the same configuration* — the
  exact case where line numbers drift and breakpoints land on the wrong statements. The
  hash needed to catch it is already on the wire; only the comparison is missing. Whoever
  tightens this should decide deliberately between hard-fail and a warning (a Designer
  edited since the debuggee started is a routine state, not necessarily an error).
- **What actually contains the exposure today is the bind address, not the protocol.**
  Every `CreateServer` call site passes `defaultHost` (`= "localhost"`,
  `debugDefs.h`), so the listener is loopback-only. §1 lists cross-machine debugging as
  something the TCP choice *buys*, and that remains true architecturally — but the moment
  the host is changed to a routable address, **any client on the network can attach to a
  production runtime and evaluate arbitrary expressions in its context** (Watch is a full
  `ibProcUnit::Evaluate`, §5.1). Remote debugging therefore needs a real handshake — a
  shared secret at minimum — before the bind address moves. Treat "it is localhost" as the
  current control, and note that nothing in the protocol enforces it.

---

## 8. Boundaries — what this debugger does not do

Recorded so a reader does not infer capability from the shape of the protocol. All three
are absences in `CommandId` (`debugDefs.h`), verified against the enum:

| Missing | State | Cost to add |
|---|---|---|
| **StepOut** ("run to caller") | No command, no server handling. The stepping vocabulary is Continue / StepOver / StepInto / Pause. | **Small — the mechanism already exists.** StepOver is a call-depth comparison (`m_numCurrentNumberStopContext >= GetCountRunContext()`, §4.1); StepOut is the same test with a strict `>`. One command id, one branch beside the step-over branch, one toolbar action. |
| **Conditional breakpoints** | Not representable: a breakpoint is a bare line number in `std::map<wxString, std::vector<unsigned int>>` (`debugServer.h`) — there is nowhere to put a condition. | Medium. The evaluation half is free (`EvalInParkedSession` already runs arbitrary expressions at the stopped frame); what it needs is a payload change (line → line + expression), the wire format, and the Designer UI. |
| **Break on exception** | No command; the debuggee reports errors after the fact through `CommandId_MessageFromServer` / `SendErrorToClient`, which sends a message, it does not park. | Medium. The natural seam is the procUnit catch path, which would have to enter `DoDebugLoop` before unwinding — i.e. park while the frame is still alive. |

Ordering note: StepOut is the one that costs a day and is felt every hour; the other two
are real features. Do not let the low price of the first imply the other two are near.

---

## 9. Honest remainder

- Web/debug transport unification is an open arc ([ROADMAP.md § 2](ROADMAP.md)); the TCP
  path described here is the live one.
- One open heisenbug (freed-memory at startup) — documented, with pinning instructions, in
  [debugger-per-session.md](debugger-per-session.md). Do not re-derive it.
- `m_number_connection_attempts` starts at `-1` (unlimited) — the retry policy for a
  scanner is worth verifying before relying on it.
- The breakpoint lookup is a linear scan per line change (§4.1) — correct, and the first
  thing to change if breakpoint counts ever grow.
