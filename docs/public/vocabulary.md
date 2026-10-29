# The vocabulary: the words the engine is built in

**What.** Code comments, reviews and the documents here use a small set of plain words with a fixed
meaning. Each entry says what the word means in this engine, the rule it carries, and one place in the
code where the rule holds.

**Why.** The words are ordinary English, so a reader takes them in their ordinary sense and misses the
rule: a door is not any method, and a second road is a defect, not an alternative. The rules themselves
are argued in [development.md](../development.md) and [writing-code.md](../writing-code.md); this page is
the key to reading them. The words of the domain (catalog, document, register, posting) are in
[ai-context.md](../ai-context.md) §6.

Paths are under `src/engine/`.

## Where an act goes

**Door.** The one public entry an act of a class or a subsystem passes through. A door does its job or
throws; it does not decide for its caller. One door per act; whatever helps the door lives inside it.
*Holds at* `ibDatabaseLayer::Commit` (`backend/databaseLayer/databaseLayer.h`): commits or throws, and
leaves the transaction open for its owner. Rules: development.md §3, §4.

**Owner.** Whoever made a thing and keeps it. The owner decides what a door does not: retry, rollback,
fallback, teardown. It hands its context down as a reference; nothing below reaches for it through a
global or the active configuration.
*Holds at* `ibApplicationInstance::Close` (`backend/appData.h`): a base's teardown is run by its owner,
`ibApplicationHost::Close`, while the base is still listed. Rules: development.md §4, writing-code.md §10.

**Side.** The part of the system that knows a thing's class: server or client, backend or core. An object
is made, a word translated and a schema interpreted on the side that knows it. The core has no factory.
*Holds at* `ibVariant::FromNode` (`core/variant.h`): makes a primitive, and leaves an object to the side
that knows its class. Rules: writing-code.md §7.

**Entrance.** A way in for a different caller: the designer's mouse, a thin client, a browser, an
assistant over MCP. Every entrance reaches the same session, the same rights and the same doors. A new
entrance is not a new door.
*Holds at* `backend/mcp/mcpTool.h`: a tool calls the same door the button calls. Rules: writing-code.md
§11.

**Seam.** The line between two layers, and the one thing allowed across it. Narrow on purpose: the layer
above speaks only what the layer below reads.
*Holds at* `backend/composition/dataComposer.h`: the composer renders its schema into ordinary query
text, and the text is the only seam downward. Rules: writing-code.md §7.

## How a fact travels

**Road.** The path a value or a state takes from whoever writes it to everyone who reads it: setter,
serialisation, database, copy, wire, restart. One state, one road.
*Holds at* `ibApplicationInstance::Open` (`backend/appData.h`): the one road a base comes up by, a file
base and a server base alike. Rules: development.md §4.

**Second road.** A second path to an answer the engine already gives. It is how "works here, not there"
is born: the two drift. The fix converges the roads; it does not add a condition that tells the cases
apart.
*Holds at* `backend/composition/composeEvaluate.h`: one home for a helper that had two, after the two
drifted apart. Rules: development.md §4.

**Second copy.** Data kept beside its holder: a cache, a mirror table, a field remembering what an object
could be asked. It needs invalidating, and it drifts. Ask the holder instead; wanting `mutable` or a lock
is the sign.
*Holds at* `backend/job/jobManager.h`: every kind of job stamps and reads its last run in the one shared
record, never in a table of its own. Rules: writing-code.md §6.

**Second currency.** A second way to name the same thing: a string beside an id, an encoding beside a
type. Once the id works, the second currency goes; the name is spelled back from the id only for a
person.
*Holds at* `ibParamUnit::m_clsid` (`backend/compiler/byteCode.h`): a parameter's declared type is a class
id, no longer its name. Rules: writing-code.md §6.

**Description.** A schema as data, `ib…Description` in the core: copyable, comparable, storable as a
version. It holds no live runtime object, so it can be read while a configuration is still loading.
*Holds at* `core/description/`: every description of the engine, with no runtime beside it. Rules:
development.md §4, writing-code.md §7.

## Values and their lifetime

**Holder.** What keeps a reference-counted object alive: `ibValue`, `ibValuePtr<T>`. A new value is born
held; a bare pointer starts at a count of zero.
*Holds at* `ibValuePtr<T>` (`backend/value_ptr.h`). Rules: development.md §4, writing-code.md §5.

**Creator.** Whoever makes a value. It answers with a holder, never a bare pointer, and it decides how
the thing is built; a later reader does not decide it again.
*Holds at* `ibValue::CreateObject` (`backend/compiler/value.h`): answers an `ibValue`. Rules:
writing-code.md §5.

**Kind.** A meaning given its own type: an `enum class` with explicit values, or a registered class with
its own rule. Not two flags, not spare bits of an id, not a branch in every consumer.
*Holds at* `ibSessionKind` (`backend/session/session.h`): what a session is, a number on disk that is
never renumbered. Rules: development.md §4, writing-code.md §11.

## Saying no, and stopping

**Refusal.** A no with its reason: a typed exception of the subsystem that refused, or `false` with the
words. A refusal names its subject and reaches a person; a `false` nobody reads is half a fix.
*Holds at* `ibCompileCode::DoRefuse` (`backend/compiler/compileCode.h`): the one door every refusal of a
compile goes through. Rules: writing-code.md §9.

**Signal.** One act, one verb, at the owner. The owner passes the same word to everything doing its
work, and whoever started the work catches the interrupt.
*Holds at* `ibSession::Cancel` (`backend/session/session.h`): the one command to stop; the connection,
the runtime and the tenants each get it in turn. Rules: development.md §4.

## Around a change

**Neighbour.** The nearest existing thing that does a similar job. A new piece takes its place, its name
or its overload, and its shape; a new kind starts from the list of what its neighbour answers.
*Holds at* `ibAcctShape` (`backend/metaCollection/partial/accountingRegister.h`): named as a shape rather
than three flags, for the reason its neighbour states. Rules: development.md §3, writing-code.md §1, §2.

**Cell.** Where one value is shown and edited on the client. It knows the value's type, makes and adjusts
the value, and sends it. A property's logic lives in its class on the server and in its cell on the
client; nothing between them knows it.
*Holds at* `frmclient/backend/backend_type.h`: the type factories as a value cell asks them. Rules:
writing-code.md §11.

## Where it stops

- Only words that carry a rule are here. A word used in its plain sense is not.
- A word enters with its rule and one place in the code where the rule holds. If that place is
  gone, the entry is stale; say so in a pull request.
