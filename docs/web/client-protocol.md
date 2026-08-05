# Web client protocol — frames, batches, and the acknowledged counter

> **Scope:** the wire contract between the browser client and `wenterprise-server`. What a
> command is, what a frame is, how the two travel, and what guarantees each side owes the
> other. This is the protocol layer only — control rendering is
> [conventions.md](conventions.md), the runtime shape underneath is
> [architecture.md](architecture.md), and what is / is not ported is
> [open-issues.md](open-issues.md).
>
> **Status: DESIGN — no code yet.** Every mechanism it *builds on* is in the tree today
> (§7); nothing described in §2–§6 is implemented. Do not read a section heading as a
> feature.
>
> **Why it exists.** The client today speaks one HTTP request per user action, each answered
> with the full JSON tree of the active form, plus a 2-second poll. On a LAN that is
> invisible. On a mobile link it is not: keep-alive is disabled
> (`set_keep_alive_max_count(1)`, [main.cpp](../../src/engine/wenterprise-server/main.cpp)),
> so every action pays a fresh TCP connect, and every answer ships a whole tree. This
> document is the shape that fixes both without giving the browser any authority it should
> not have.

---

## 1. The model in one paragraph

The **server owns the truth**; the browser owns nothing but pixels and the user's intent.
A user action becomes a **command**. Commands queue on the client and travel in **batches**.
The server applies a batch *in order* on the session's worker, and answers with one
**frame** — the state of the active form after the whole batch. Commands are reliable and
ordered; frames are disposable — only the newest matters. The two directions are therefore
**not symmetric**, and deliberately so (§3).

This is the same split a networked game makes between *input* and *snapshots*, and the same
split a data-exchange plan makes between *sent* and *received* message numbers. Neither is
an analogy stretched for its own sake — both are load-bearing below.

---

## 2. Vocabulary

| Term | Is | Lives |
|---|---|---|
| **Command** | one user intent — `{controlId, kind, value}` | client queue → batch |
| **Batch** | an ordered, numbered list of commands, applied as one server tick | one HTTP POST |
| **Frame** | the form state *after* a batch — today a full tree, later a diff (§8) | batch response, or an SSE push |
| **Batch number** | monotonic per session; the acknowledgement unit | client counter ↔ server counter |
| **Frame number** | monotonic per session; lets the client drop stale state | server counter → client |

`kind` is the string the existing dispatcher already speaks — `click` / `text` / `toggle` —
so a batch entry is exactly the argument triple of
`ibWebApplication::Dispatch(controlId, kind, value)`. The batch endpoint adds no vocabulary;
it adds a loop.

---

## 3. Two channels, two sets of guarantees

The single most important rule in this document. A data-exchange plan is symmetric because
both of its nodes are databases. Here the two directions carry different cargo:

| | Client → server (commands) | Server → client (frames) |
|---|---|---|
| Loss tolerated | **never** — a dropped command is lost user input | yes — an intermediate frame is worthless once a newer one exists |
| Ordering | strict; one batch in flight at a time | latest-wins; older frame numbers are dropped on arrival |
| Retransmit | yes — same batch, same number, until acknowledged | **no** — the next frame supersedes |
| Acknowledged | yes, by the server's received-counter | no — an ack per frame would cost a round trip per push and undo the win |

Making the frame channel symmetric is the tempting mistake: it looks tidier and costs one
RTT on every push.

---

## 4. The counter — sent, received, and the cached answer

Borrowed wholesale from the exchange-plan discipline: each side keeps *the number it sent*
and *the number it accepted*. Until the peer's accepted number advances, the same packet
goes again.

**Client.** Holds `nextBatch` and `lastAckedBatch`. One batch in flight. While it is in
flight, new commands accumulate into the next one (§5). On timeout or a dropped connection
it resends **the same batch with the same number** — never a renumbered copy.

**Server.** Holds, per session, `lastAppliedBatch` **and the frame it produced**. Three
cases, and the third is the one that is usually forgotten:

| Incoming batch number | Action |
|---|---|
| `== lastApplied + 1` | apply on the worker, store number **and** response, answer |
| `== lastApplied` | **answer from cache — do not execute** |
| `< lastApplied` | stale; drop with a marker so the client can resync |

**Caching the number without the answer is a silent data-loss bug.** Sequence: the batch
arrives, executes, the frame is built — and the response is lost on the way back. The client
resends. A server that remembers only the number replies "already applied" and the client
never receives that frame. The state it is drawing is now permanently behind, with no error
anywhere.

**The counter advances after execution, not on receipt.** It is bumped when the batch has
gone through `RunOnWorker` and a frame exists. Bumping it at HTTP-accept time would let the
client retire commands from its queue that were never applied — the same class of silent
loss, from the other end.

### 4.1 The case an exchange plan does not have — the session is gone

Exchange-plan nodes are permanent; a web session is not (idle sweep evicts after 30 min, and
a metadata deploy evicts every session at once). A client returning from a tunnel will resend
batch 47 to a server that has never heard of it and, without a distinct answer, will resend
it forever.

The server must answer **"unknown session — reset"**: counters back to zero, a full frame,
and no attempt to interpret the number. The precedent is already in the tree — `g_metaGeneration`
plus the client's `showSessionLost` already distinguish *"your session ended"* from *"the
configuration changed"* instead of collapsing both into one error.

---

## 5. The client queue — it coalesces, it does not merely accumulate

Accepting into the queue **never blocks**. Three states, not two: *queued*, *in flight*,
*applying a frame*. The interface stays live in all three; only the server serialises.

A queue that merely appends is a hazard, not a buffer:

| Command kind | Rule | What it prevents |
|---|---|---|
| text edit on one control | latest-wins — keep the final value | five intermediate values shipped for one word typed |
| button click | de-duplicate | a user who taps "Post" three times because "nothing happens" posting three documents |
| scroll / resize | latest-wins | a queue that grows faster than it drains |
| distinct controls | never merged | order between different controls is meaning, not noise |

De-duplicating a click is *not* a substitute for disabling the button locally the moment it
is pressed. Do both: the local disable is the user-visible answer, the de-dup is the
correctness net.

---

## 6. Prediction and replay

With a 300 ms link, a field that echoes only what the server confirms feels broken. The fix
is the networked-game one: apply locally at once, mark unconfirmed, reconcile on the frame.

**The server is always right.** Prediction buys visual credit, never a decision.

| Predict locally | Wait for the frame |
|---|---|
| character in a text box | a value substituted from a reference |
| checkbox toggle | any total recomputed from other fields |
| tree node expand / collapse | posting, deleting, closing |
| tab switch, focus | anything a right can veto |

**Replay is not optional.** A frame answers the state as of batch N; the user has typed more
since. Applying the frame flat **erases what they typed** — the classic "my letters keep
disappearing" bug. The frame is the base; unconfirmed local commands are replayed on top of
it. The server is authoritative about data, the client about input that the snapshot could
not have seen.

### 6.1 A command carries the frame it was formed on

Opening a tab is the longest tick there is: `OnOpen`, queries, a rebuilt tree. Commands
queued during that second refer to the **previous** state. Each command therefore carries the
frame number it was formed against, and the server decides:

- harmless (`text`, `toggle`) — apply;
- consequential (post, delete, close) — **reject and ask for a repeat**, because the user
  pressed that button while looking at a different form.

The same number that gives idempotence on retransmit gives staleness detection here. One
mechanism, three jobs: ordering, duplicates, stale input.

---

## 7. What this builds on — already in the tree

Nothing below needs to be invented; the protocol is a loop and a counter placed over doors
that exist.

| Piece | Where | Role in the protocol |
|---|---|---|
| `netFetch` | client JS, wraps all call sites | the one chokepoint the batcher installs into — no call site changes |
| `ibWebApplication::Dispatch(id, kind, value)` | [webApplication.h](../../src/engine/frontend/web/webApplication.h) | the door a batch loops over; polymorphic `HandleRequest` underneath |
| `RunOnWorker(fn).get()` | same header | **one submit per batch**, not per command — otherwise N worker hops and N tree rebuilds per user action |
| `GET /stream` | [main.cpp](../../src/engine/wenterprise-server/main.cpp) | the frame channel; retires the 2-second poll |
| `GenerateNewID` / `controlId` | `visualHost.cpp` | stable node identity — what makes a diff possible later without a virtual DOM |
| `g_metaGeneration` / `showSessionLost` | `wfrontend.cpp` + client | the precedent for §4.1's reset answer |

**Compression is not ours to write.** `CPPHTTPLIB_ZLIB_SUPPORT` is not defined anywhere in
the tree, so nothing is compressed today. Defining it plus linking zlib gives
`Content-Encoding: gzip` and **zero lines of client code** — the browser decompresses. A form
tree is highly redundant JSON (`"type"`, `"layout"`, `"id"` repeated hundreds of times), so
the ratio is at the good end. Writing a private packing layer on top of HTTP would be a
second mechanism for a solved problem.

---

## 8. Order of work, and what is deliberately deferred

1. **Batch endpoint** over `Dispatch`, one worker submit, **one frame at the end of the
   batch** — not one per command, which would *increase* traffic.
2. **Batch numbers** + the cached answer (§4) + the reset answer (§4.1).
3. **SSE for frames**, retiring the poll.
4. **gzip** — one define.
5. **Client queue** with coalescing (§5), then prediction + replay (§6).

**Deferred on purpose — the diff (§ frame contents).** Today a frame is the whole tree.
Compression hides that on the wire but not in the DOM, and a thousand-row table will make it
the dominant cost. The identity needed for a diff already exists (`controlId`), so the option
stays open. Build it when real forms show *what* actually changes — not before, because the
protocol above already collects most of the win at a fraction of the work.

**Window size 1 is a decision, not a limitation.** One batch in flight means one batch per
RTT — at 500 ms, two per second, far above what typed input needs once the queue coalesces.
Bulk work (file import, export) will want more; widening the window to N changes how many
unacknowledged packets may be in the air and nothing else. The counters are unchanged.

---

## 9. Prior art — where each half comes from

Recorded so a reader does not mistake convergence for invention, and so the next question
("what do people do about X?") has a place to look.

| Half | Established as | Note |
|---|---|---|
| server owns the widget tree, browser renders | **server-driven UI** — Phoenix LiveView, Hotwire, Blazor Server; at product scale, Airbnb / Shopify | wfrontend already *is* this; the batch is the missing transport discipline |
| batched intent + authoritative snapshot + prediction with rollback | **networked-game netcode** (Quake 3 lineage) | frame number, stale-input rejection, replay-on-top |
| sent / received counters with retransmit-until-acknowledged | **the exchange plan we already ship**, and stop-and-wait ARQ generally | window 1, cumulative ack |
| reliable-ordered input vs. latest-wins state | same netcode split | §3 — the asymmetry that keeps the frame channel cheap |

What this is **not**: it is not local-first / CRDT sync (Figma, Linear, Replicache). Those
resolve concurrent edits by merging. An accounting system must not merge — it must have one
authority, one order, and a transaction. Optimistic UI is borrowed; conflict-free merge is
rejected on purpose.
