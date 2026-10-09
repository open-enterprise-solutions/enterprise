# Session failover

**What.** How a thin client's session outlives a dropped connection, how the same person comes back on
another server of the same base without typing the password again, how the open tabs are reopened, how
unsaved edits of a modified form are kept, and how the dead session's record locks move to the new one.

**Why.** The server holds the session and the live form. The client draws frames and sends events. A
failure today throws away the person's work: a dropped socket logs the client out while the server is
still up, and a call retried after that drop runs again; a crashed server makes the person log in by
hand, and the open forms and unsaved edits are gone; the dead session's locks stay until the sweep, so
on a standby server the person is told the record is locked by their own dead session.

**Where it will be written.** The names and numbers below are proposed. They land in
`src/engine/protocol/protocol.h` when each stage is built, and the wire description in
[client-protocol.md](client-protocol.md) grows with them. This document is the plan. Nothing here is
implemented yet.

The form contract in [The form, after a move](#the-form-after-a-move) is the text that will be added to
[thin-client.md](thin-client.md), under Forms. It is not added in this step.

## What exists

The chain is core → backend → frmserver ⇄ protocol ⇄ frmclient → enterprise-thin. `fileserver` is the
same protocol in the client's process, for a file base. `protocol.h` depends on nothing. A number there
is never renumbered; a new one takes the next. A field a side does not know is skipped; an absent field
is its default. The protocol version stays 2: a new field does not raise it
(`ibProtocolVersion` in `protocol.h`).

### The client and the socket

`ibClientHost` (`src/engine/frmserver/client/clientHost.h`) is the protocol's door for one base. `login`
makes a `Client`, mints its id with `ibGuid::newGuid()`, and stores the connection pointer it came on
(`clientHost.cpp`, the `Login` branch). That id is the protocol's `Client` and the session's id.

`ibClientInstance::Login` (`clientInstance.cpp`) creates an `ibClientSession` of kind `ThinClient` or
`ThinDesigner` under that same id, so the client, the registry and the `sys_session` row are one
identifier. A wrong password drops the holder, and the row goes with it. `Start` builds the frame and
runs the application's start.

A WebSocket is one connection per client (`src/engine/appserver/clientListener.cpp`). When the read
ends, the listener calls `ibClientHost::Disconnect`. `Disconnect` erases every client logged in through
that pointer and calls `OnExit` on each. `OnExit` cancels the session, closes its documents, and
releases the holder, which closes the session and deletes the row. The protocol states the consequence
([client-protocol.md](client-protocol.md), "Where it stops"): a call is answered once, and a call
repeated after a dropped connection is done again, because the dropped socket ends its clients.

The client's socket (`src/engine/protocol/connectionServer.cpp`) treats a closed socket as "log in
anew" (`ibProtocolRefusal::NoSession`). The address it parses is one host: `oes://server[:port]/<base>`.

An HTTP `POST` passes a null connection, so it is not tied to a socket. Those clients live until
`logout`, until they ask to close, or until `kIdleLimit` (30 minutes) in `ibClientHost::RoundBody`.
A lost HTTP response is still done again: nothing remembers the call.

One call of a client at a time. The host keeps the last frame it answered (`Client::frame`,
`Client::sent`) and, from protocol 2, the view last sent for each tab. A call that names `Since` as
that frame number is answered with `Patch`.

The `*` on a tab title is built on the server. `ibClientChildFrame::GetTitle`
(`clientChildFrame.cpp`) appends it while the document `IsModified()`. The document's flag follows the
form (`ibFormVisualDocument::Modify` in `visualHostDocView.cpp`). The form's flag follows the object:
`ibValueRecordDataObjectRef::Modify` (`commonObject.cpp`) finds the form opened on that object and
calls `Modify` on it. The form's own state also carries `Modified` (`ibValueForm::OnUpdate` in
`form.cpp`). That flag, once an event has finished, is the moment a draft is written. It is not a timer.

### `sys_session`

Created in `ibApplicationInstance::CreateTableSession` (`appDataQuery.cpp`). The primary key is
`session` (36 characters). Beside it: `userName`, `application`, `started`, `lastActive`, `computer`,
and, by later migration, `pid`, `address`, `currentActivity`, `kind`, `signal`, `exclusive`. There is
no token column and no resume deadline.

The owning process updates `lastActive` every second (`JobHeartbeatOwn`). `JobSweepStale` runs about
every 3 seconds. A row this process still holds is alive. Any other row whose `lastActive` is older
than the silence — 10 heartbeats, 10 seconds (`kSilentBeats`, `kSilentSeconds` in `sessionRegistry.cpp`)
— is deleted. The same pass hands the lock manager and the temporary-file storage the list of who is
still alive, and they delete the rest (`SweepOrphans`).

A row therefore outlives a killed process until that cutoff. Until then a lock whose `sessionGuid` is
the dead session still conflicts, and the message names that row's `userName` — the person who just
came back.

### `sys_lock`

Created in `CreateTableLock` (`appDataQuery.cpp`). One row per long-held lock: `lockGuid`,
`sessionGuid` (the owner), `namespace`, `keyHash`, `keyData`, `lockMode`, `acquiredAt`, `userName`,
`computer`. `ibLockManager` (`lockManager.h`) inserts on acquire and deletes on release. Same-owner
re-entry on `(namespace, keyHash)` is not a conflict. `OnSessionEnd` deletes every row of that
`sessionGuid`; `ProcessRemove` calls it before deleting the session row (`sessionRegistry.cpp`).
`SweepOrphans` deletes locks whose owner is not in the live list, which is how a lock survives a crash
that never reached `OnSessionEnd` — until the sweep.

`sys_file` is the same shape of ownership: every part carries `sessionGuid`, and the session's end
deletes them. A file the client uploaded is named by an id in that storage
([client-protocol.md](client-protocol.md), Files).

### The frame, the form, the object

A tab's id is assigned when it opens (`NodeId`). Its `Kind` is 2 for a text and 3 for a spreadsheet;
absent means a form. Its `File` is the temporary-storage id, once the document has one. The active
view's `Key` is the runtime form key (`ibFormVisualEditView::OnDraw`), the key `event` names in `Form`.
That key is not the metadata form's guid. `CreateFormUniqueKey` (`visualHostDocView.cpp`) is, in order,
the key the caller set, the owner control's guid, the source object's guid, or a new guid.

`execute` opens an object by metadata id and command type (`ibInterfaceCommandType` in
`interfaceHelper.h`: 100 default, 150 create, 151 list, 152 select). `open` opens a temporary file by
the template its name matches, or a new document of an extension.

A form's object lives in `m_listObjectValue` (`ibValueDataObject` in `valueInfo.h`), attributes and
tabular sections keyed by meta id. A reference object loads both in `ReadData`
(`commonObjectRefQuery.cpp`): attribute columns from the row, then one tabular section per table,
loaded by the object's guid.

### What serialization can do

`ibDataNode` (`src/engine/core/serialize/dataBuilder.h`) is the one in-memory tree. `ibBinaryProvider`
writes it to a blob, fields by name, so a new field does not break an older blob. `ibValue::Serialize`
writes the class id and then `DoSerialize` (`valueSerialization.cpp`). It refuses a value that is not
transferable.

What `DoSerialize` covers:

- empty, boolean, number (`ibNumber`), string, date (`ibDateTime`);
- an array, each element a child that serializes itself (`valueArray.cpp`);
- a map, a font, a colour, a composition field;
- a reference, as identity only: meta id `m` and guid `g` (`ibValueReferenceDataObject::DoSerialize`
  in `reference.cpp`). The comment there is the rule: the object is not written, because it belongs
  to a session. The far side re-reads it.

What it does not cover, and says so with `IsTransferable() == false`:

- a record object (`ibValueRecordDataObject::IsTransferable` in `commonObject.h`) — its attributes
  and tabular sections are the session's buffer, and it may hold a lock;
- a tabular section (`ibValueTabularSectionDataObjectBase` in `tabularSection.h`) — it is part of
  that object. `Unload()` copies the rows into a value table for script. That copy is a runtime
  value, not a node. A value table's `WriteProperty` (`valueTable.cpp`) writes its columns, not its
  rows, and it has no `DoSerialize` for the rows;
- a form, a manager, a record set, a lambda, an OLE value.

So a live object with tabular sections has no node form today. The draft extends `DoSerialize` on the
record and on the tabular section. It does not add a second format, and it does not flip
`IsTransferable`: jobs and settings keep refusing a live object. The draft writer calls `DoSerialize`
on the node it owns. `Serialize()` stays the door that checks `IsTransferable`.

Form attributes are a different store. `ibValueForm::WriteAttributes` (`formAttribute.cpp`) writes the
attribute's definition (name, type, the main flag) into the configuration. The runtime value is
`ibFormAttributeValue::m_value`. The draft takes that value, through the same `Serialize`, when the
value is transferable. The main attribute is the object, written by the record's `DoSerialize`.

### A file base

`fileserver` opens the base in the client process and passes each JSON-RPC text to
`ibClientHost::Call` (`fileBase.h`). There is no socket to drop until the process closes. A crash
takes the window and the engine together. Stages 2–5 still apply when a new process opens the same
file inside the window: the row, the token, the locks and the draft are in the base. The code does
not branch on file-versus-server. A file base's address list has one entry, the file.

## What a failure loses

1. **The socket drops, the server stays up.** `Disconnect` → `OnExit` ends the session at once. The
   next call is a new login. A call whose answer was lost, retried, runs a second time. The JSON-RPC
   id is already monotonic per client (`ibCommunicator::m_lastId` in `communicator.cpp`); the server
   does not remember it.
2. **The server process dies.** The person types the password again. Forms, cursors and unsaved edits
   are in that process and are gone. `Var` in the form module, a script that was running, and a
   question the server was waiting on die with it.
3. **The locks.** They stay until a peer's sweep decides the row is 10 seconds stale, then
   `SweepOrphans` deletes them. In that gap the person, logged in on a standby, is the other owner.

## The form, after a move

This paragraph will be added to [thin-client.md](thin-client.md) as the form contract. It describes a
move to another server (stages 2–4). A return to the same process (stage 1) keeps the live form, so
the script and the pending question are still there.

After a move to another server, a form's state is its object and its attributes. Variables of the
form module (`Var`) are not restored: one may hold a COM object, a connection or a lambda, and the
form opens again and starts them from scratch. Always lost: a script that was mid-execution, and a
question that was waiting for the person.

The client's copy of the frame is not the source. The person's events are not replayed on the new
server. Questions stay synchronous; the script API does not grow an asynchronous question.

## Stages

Each stage is a later change of its own. The protocol only grows, by the fields and the one method
below. `ibWorkerPoolHeadless::Await` / `Wake` and `session/workerPool*` stay as they are: a script
parked on a question keeps waiting on the pool it uses now (`ibClientInstance::OnExit` already
cancels that wait when the session actually ends).

### 1. The session survives a dropped connection

`Disconnect` stops meaning "log out". It clears the connection pointer and the notifier, and arms a
deadline of `Resume` seconds (see [The setting](#the-setting)). The `Client`, the session, the forms
and the frame cache stay. `RoundBody` does not treat that client as idle: the idle limit applies
while a connection is attached (and, as today, to an HTTP client, which has none). When the deadline
passes with no return, the host calls `OnExit`, and the session closes as it does today.

`login` accepts `Token` and no `User`. The server finds the in-memory client for that token, binds
the new connection, and answers with the same `Client`. The call may name `Since`. The answer is the
patch from the frame the client holds, the same rule as any other call. A `login` that still carries
`User` and `Password` is a new session, as today.

The server keeps one slot on the client: the JSON-RPC `id` of the last call it answered, and the
response text it wrote. A request that arrives with that id is answered with the saved text and is
not run. The slot is filled when the call finishes, including a refusal. A request that is still
running (the client's `unsettled` future) is waited for, not started again. The client keeps the
unacked request and sends it again with the same id after it has logged in by token, and only then
sends anything new. The slot lives in the process, with the session. It is not written to the base.
After a crash there is nothing to return, and the client does not retry that call on the new server:
it restores the workplace instead. A write the crashed call had already committed stays committed.

A pending question survives this stage, because the session object does. The resumed frame still
carries `Request`. The person answers it. The script continues where it waited.

`Features` on the login answer lists `resume` when `Resume` is not 0, so an older client never
depends on it. The protocol version stays 2.

### 2. A login on another server

The address may name several servers. The client's parser (`connectionServer.cpp`, `ParseAddress`)
accepts a comma-separated list of authorities in front of the base:

`oes://host1[:port],host2[:port]/<base>`

One host, as today, still parses. The client tries the server it last spoke to, then the others,
until a socket connects. It logs in by itself. No password when it still holds a token.

The token is a column on `sys_session`, so any process that opens the base can see it. When this
process does not hold the in-memory client, and the row's `resumeUntil` is still ahead, `login`
`{Token}` is accepted. It creates a new session for the same `userName`, without
`AuthenticationAndSetUser`. The answer's `Client` is the new id. The frame is the new session's
start, the whole frame, and the client then calls `restore` (stage 3).

When `resumeUntil` has passed, or the row is gone, `login` `{Token}` is refused with 401
(`LoginRefused`). The client asks for the password.

The token is stolen by the server that accepts it: the old row is deleted as part of the adopt
(stage 5 moves the locks and the files first). A process that still holds the in-memory session
notices on its next heartbeat that the row is gone, and closes that session. The client tries its
last server first, so a live session is resumed in place (stage 1) and is not stolen.

### 3. The workplace comes back

The server tells the client how to reopen each tab, on the tab, as a child `Address`. The client
keeps it and sends it back. It is not inferred from the client's copy of the controls.

| Field on `Address` | Holds |
|---|---|
| `Object` | the source, when it is a stored reference: the value node `{t, m, g}` |
| `Form` | the metadata form's guid (`ibValueMetaObject::GetGuid`) |
| `Command` | the metadata id `execute` was given, when the tab was opened that way |
| `Type` | the command type: 100, 150, 151, 152 |
| `Draft` | an id minted for a new object that has no reference yet |

A text or a spreadsheet keeps the tab's existing `Kind` and `File`. `File` is the temporary-storage
id. Those rows move with the locks (stage 5), so the id still opens on the new session. A tab with
no address (the start page, `Locked`) is not sent back; the new session builds its own start page.

`restore` is the new method (number 16, the next after `presentation`). Parameters: `Client`, and
`Tabs`. Each entry is the `Address` the server wrote, plus where the person was: `Current` (the
row's reference value, or the key fields of a row that is not a reference) and `Focus` (`Control`,
the control id, and `Column`). `Active` names which entry is the active tab. The answer is the frame,
whole.

The row the client holds during a session is a handle (`ibValueModelTableBox::HandRow` counts
`m_nextRowHandle`). That handle belongs to the process that handed it out. `restore` sends the
reference, which `FindRowValue` can resolve on the new form. The control id is the id stored on the
control (`EnsureControlID`); a control that already has one keeps it across an open of the same form.

The server opens each address the way `execute` or `open` opens it today. `OnCreate` runs. Unsaved
edits are not in this stage: the object is the one in the base. The draft (stage 4) is applied after
the form exists.

### 4. A draft of unsaved state

The moment is: a call's work has settled (no question pending), and the form is modified. The host
writes the draft at the end of that work, on the session's worker, before the frame is answered. An
idle tick does not write one. A call that only moves the cursor does not write one either: the draft
is written when the object's values or a form attribute's value differ from the draft already stored.
The comparison is the node's (`ibDataValue` equality), so a second event that leaves the same values
does not write again.

The draft is the form's object — attributes and tabular sections — and the form attributes whose
values are transferable. `Var` is not in it. A non-transferable form attribute is omitted, except a
dynamic list's settings, which already have a node (`WriteProperty` of the composition). The rows of
that list are read again.

Writing or saving the object deletes the draft, in the same success path as `SaveData`. Closing the
form without saving leaves the draft until the token's window ends. The sweep that drops an expired
token drops its drafts.

On the reopen in stage 3 the form is created first, so `OnCreate` sees the object from the base (or
a new object). Then, if a draft exists for that address, `DoDeserialize` applies it, and the form is
set modified so the `*` comes back. The draft wins over a field `OnCreate` filled.

### 5. The locks move

The new session takes the dead session's locks before any form tries to acquire them, and before the
old row is deleted. One update: `sys_lock.sessionGuid` from the old id to the new one, and the same
for `sys_file.sessionGuid`. Same-owner re-entry then sees the new session as the holder, so the
reopened form does not conflict with the lock the person already held.

`SweepOrphans` must not delete those rows in the gap. A session row with `resumeUntil` still ahead
counts as alive for the sweep, even when `lastActive` is older than 10 seconds. `OnSessionEnd` of
the old id runs only for a session that ended without being adopted. An adopted id has already given
its rows away, so the delete matches nothing.

## Protocol

Names go in `ibProtocolName`. Numbers already assigned stay. `ibProtocolMethodFromName` walks from
`Login` through `Presentation`; a new enumerator is added after `Presentation` and the walk's end
moves with it.

| Addition | Kind | Number / name | On the wire |
|---|---|---|---|
| `Token` | field | name `Token` | `login` parameter. Present without `User`: resume. Answer of a password `login`, and of a resume: the token to keep |
| `Address` | field | name `Address` | child of a tab. `Object`, `Form`, `Command`, `Type`, `Draft`, as [stage 3](#3-the-workplace-comes-back) |
| `Current` | field | name `Current` | `restore`: the row's reference (or its key), not the handle |
| `Focus` | field | name `Focus` | `restore`: `Control`, `Column` |
| `Active` | field | name `Active` | `restore`: which tab entry is active |
| `resume` | feature | string in `Features` | the server offers stages 1–5. Absent: an older server, the client behaves as today |
| `restore` | method | 16, name `restore` | `{Client, Tabs}` → the frame. `Tabs`: `Address`, `Current`, `Focus` each. `Active` beside them |

No new refusal code. An unknown or expired token on `login` is 401, the code `login` already uses
for a refused login. A `restore` with no session is the existing 401 `NoSession`.

`Since` is the existing field. A resume of a live session honours it. A new session answers with the
frame whole, and the client starts its store again, as a whole frame already requires.

## Where the token is stored

A new nullable column `token` on `sys_session`, `ibTypeString(64)`, indexed for the login lookup —
the width `sys_lock.keyHash` already uses. It is not the session guid. The session guid is
`Client`, and Active users already shows it (`schema` 2, the `Session` column). The token is 32
random bytes, written as 64 hex characters, returned at `login` and stored by the client. It is
not put on the frame and not put in the Active users schema.

A new nullable column `resumeUntil` (`ibTypeDate`). For a `ThinClient` row, the heartbeat writes
`now + Resume`. Other kinds leave it null, and the 10-second silence is unchanged for them. A thin
designer holds the base's exclusive flag; giving it the resume window would keep a dead designer in
the way. A thin designer still ends on `Disconnect`.

Both columns are added by `MigrateTableSession`, the additive path that table already uses. The
table is not dropped. Existing rows stay valid: null `token` and null `resumeUntil` mean today's
sweep.

The in-memory client, while this process holds it, maps the token to the `Client` record. The column
is what another process reads.

## Draft format

A new table `sys_draft`, created beside `sys_settings` (the same startup path as `sys_lock`). The
primary key is one column, as that table's is: a hash, because the renderer spells `PRIMARY KEY` per
column.

| Column | Holds |
|---|---|
| `draftKey` | SHA-256 hex of `token` plus the address. Primary key |
| `token` | the session token. The drafts survive a new session id; the index serves "every draft of this token" |
| `address` | the readable address: object guid + form guid, or the `Draft` id of a new object |
| `changed` | when it was written |
| `dataSize` | the blob's length |
| `binaryData` | the node, through `ibBinaryProvider` |

The node:

- `Object` — the record's `DoSerialize`. Child `a`: one child per attribute meta id, each an
  `ibValue::Serialize` of the attribute (a reference is `m` and `g`). Child `t`: one child per
  tabular-section meta id; the section's rows in order, each row a child of cells keyed by column
  meta id, each cell serialized the same way. The line number is not stored; order in the node is
  the order, and the platform numbers the lines again on load.
- `Attributes` — one child per form attribute that is not the main one, by name, the value
  serialized when `IsTransferable()`. A dynamic list contributes its settings node, not its fetched
  rows.

`DoDeserialize` on the record writes the attributes back with `SetValueByMetaID`, clears each
tabular section, and appends the rows. The draft writer is the one caller that invokes `DoSerialize`
on a non-transferable record. `ibValue::Serialize` continues to refuse it, so a job still cannot be
handed the object.

## The setting

`backend.conf`, key `Resume`, seconds. Read where `Connections` is read
(`ibApplicationHost::ReadBackendConf`). Absent, or left 0 by the usual "0 means default" rule of
that reader: **120**. An explicit off is a follow-up only if a deployment must keep today's
immediate logout; the plan does not add a second key. 120 is the starting value the measurement
below is meant to confirm or replace.

The same number is the host's deadline after `Disconnect` and the `resumeUntil` offset on the
heartbeat. One window, both roads: the process is alive but the socket is gone, or the process is
gone and a peer is sweeping.

## A file base, again

No branch. `Disconnect` on a file base runs when the in-process listener is closed, which is the
process going away, and the host's deadline then does not matter. A new process that opens the file
inside `resumeUntil` takes the token, the locks, the files and the drafts by the same adopt path a
standby server uses. There is no second server to list.

## Open forks

1. **Token versus reusing `Client`.** `Client` is already in `sys_session.session` and already shown
   as Active users' `Session`. Using it as the secret would let anyone who can see that schema resume
   the session. **Recommendation:** a separate `token` column, never displayed. `Client` stays the
   public session id.

2. **Where idempotency lives.** The saved reply can sit in the process, or in the base so another
   server can return it. **Recommendation:** one slot in the process, keyed by the JSON-RPC `id` the
   client already sends. Cluster-wide exactly-once is a different problem: a call that crashed
   mid-write may have committed, and replaying it is what this plan refuses. On a new server the
   client does not resend the in-flight call.

3. **`login` `{Token}` versus a `resume` method.** **Recommendation:** a field on `login`. The
   session either is the one this process holds, or is a new one that adopted the row. The answer's
   `Client` says which: the same id means the live form (stage 1, honour `Since`); a new id means
   call `restore`. A second method would duplicate login's answer (the frame, the version, the
   features).

4. **How long a dead row lives.** Stretching the 10-second silence for every session would hold a
   dead designer and a dead exclusive flag for the whole window. **Recommendation:** `resumeUntil`
   on `ThinClient` rows only. `JobSweepStale` treats " `resumeUntil` still ahead" as alive, and
   otherwise keeps the 10-second rule. `SettleSilentPeers` stays on `lastActive`, and a thin designer
   never sets `resumeUntil`.

5. **Who wins when two servers accept one token.** **Recommendation:** the adopt deletes the old
   row after moving locks and files. The process that still holds the memory closes the session when
   its heartbeat finds the row gone. The client tries the last server first, so this is the crash
   path, not the ordinary blip.

6. **What a draft includes.** The whole form, including `Var` and the control tree, would restore a
   COM object, a connection or a lambda, and would be a second copy of the frame. **Recommendation:**
   object attributes, tabular sections, and transferable form attributes, through `DoSerialize`.
   `Var` is started again by `OnCreate`. The frame on the client is not applied back.

7. **When the draft is written.** Every settled call on a modified form, including `Input` (the
   object is marked modified before the text is committed) and including `Focus` / `Row`.
   **Recommendation:** write when the serialized node differs from the stored draft, after a settled
   call with no question pending. `Input` that has not changed a committed value does not change the
   node, so it does not write. Not a timer, and not a write per keystroke of an identical value.

8. **Spreadsheets and texts.** Stage 4 is specified for a form's object and its attributes. A
   sheet's unsaved edit is the sheet node the server already stores on the document.
   **Recommendation:** object forms in the first cut of stage 4. A sheet can use the same
   `sys_draft` blob later, the spreadsheet's existing node, without a new table. Until then a
   spreadsheet reopened from its temporary file comes back as last saved to that file.

9. **The row sent to `restore`.** The handle is process-local. **Recommendation:** the reference
   value (or the key fields). The control id is stable for a control the form definition numbered.

10. **Redacting the token in the debug journal.** The protocol journal logs the request text, and
    today that text contains `Password` (`communicator.cpp`). The macro is empty in a release build.
    **Recommendation:** when stage 1 touches that line, redact `Password` and `Token` in the debug
    line. Not a new mechanism; the release build already keeps the wire out of the journal.

11. **`Resume=0`.** The conf reader treats 0 as "use the default". **Recommendation:** absent means
    120, via that reader. Do not overload 0 as "off" unless a later change adds an explicit key. The
    window is the behaviour.

12. **Startup on the adopted session.** The new session runs the application's start, including the
    start page, and then `restore` opens the person's tabs. **Recommendation:** do not skip the
    start. It is a login. The start page is the locked tab; the restored tabs follow it.

## Measurement

Taken on a debug build and a release build, one base, before the stages are tuned.

**Draft write, per settled event on a modified form.** Around the walk, `ibBinaryProvider::Write`,
and the `INSERT`: wall time, and `dataSize`. Cases: a catalog item with a handful of attributes and
an empty tabular section; a document with 1, 100 and 1 000 tabular rows, a few reference columns
among them; a form with several form attributes beside the object. The journal already times the
call (`client` / `call`). The draft's time is reported beside that, so a `Change` shows how much of
the call the draft became. The target is a small fraction of that call. A case that writes the same
node as the stored draft is included, and must cost the compare and no `INSERT`.

**Memory held across the reconnect window.** The session already holds its forms; the window delays
the free. Record, for one client with a heavy modified document (the 1 000-row case) and for ten
such clients: the process's resident size just before `Disconnect` and at the end of `Resume`, the
`Weight()` and the byte size of the last frame node (`Client::sent`), and the byte size of the saved
reply. The incremental cost of the window is that set, times the seconds the clients stay detached.
`Resume` moves to whatever this measurement says the process can hold.

## Tests

They land next to `tests/test_clientFrameApply.cpp` (the frame's patch is what a same-server return
is answered with). The two-process cases use one base and two server instances. A file base runs the
cases that need only one process; it is not a separate code path.

- **Drop and return by token.** Close the socket. The `sys_session` row and the in-memory client
  remain. `login` `{Token, Since}` returns the same `Client` and a patch that applies
  (`ClientFrameApply`'s rule). An open modified form is still the same object.
- **A lost call is not executed twice.** A call that increments a counter finishes on the server; the
  response is discarded. The same JSON-RPC `id` is sent again and the saved response is returned. The
  counter is 1. A new id increments it.
- **The window expires.** After `Resume` seconds with no return, the row is gone and `login`
  `{Token}` is 401. Locks of that session are gone with the sweep.
- **Tabs reopen on another instance of the same base.** Two processes. The first dies (or its row is
  the one the second adopts). `login` `{Token}` on the second returns a new `Client`. `restore` with
  the stored addresses opens the forms. `OnCreate` has run (the test's handler sets a field the
  draft then replaces).
- **The draft.** An event on an unmodified form writes no row. A settled event that changes the
  object writes one row, attributes and tabular rows both. A later event that changes nothing writes
  no second blob. `Write` / save deletes the row. Reopen applies it after `OnCreate`, the unsaved
  field is the drafted value, and the form is modified.
- **Locks move.** The first session holds a record. It dies. The second logs in by token. Before the
  10-second sweep, `sys_lock.sessionGuid` is the new session, and that session opens the record. A
  third session still conflicts.

## Where it stops

- A script mid-execution, and a question waiting for the person, do not survive a move to another
  server. They do survive a return to the same process.
- Form-module `Var` is not restored.
- The client's frame is not the copy the server restores from, and the person's events are not
  replayed.
- Idempotency is the last answered call of a session this process still holds. A call the crash
  interrupted is not returned and not replayed.
- A spreadsheet's unsaved sheet is outside the first cut of the draft.
- The protocol version stays 2. Existing names and numbers stay. `fileserver` takes the same path.
- The worker pool's `Await` / `Wake` is untouched.
