# Public design documents

Design documents anyone may read: people building a configuration on the platform, people
extending the engine, people deciding whether to adopt it. Written here rather than in
`docs/private/` because nothing in them has to stay inside the organisation.

This folder is the place for the design work that others are meant to build on. Put a document
here when it answers a question somebody outside the team would ask — how a subsystem is meant to
be used, why an interface has the shape it has, what a mechanism guarantees and what it does not.

## What belongs here, and what belongs in `private/`

| Here, in `public/` | In [`private/`](../private/) |
|---|---|
| How a subsystem works and how to use it | Arcs in progress, and what is still broken in them |
| Why an interface is shaped the way it is | Roadmaps, plans and their ordering |
| What a mechanism guarantees, and its limits | Customer names, deployments, commercial context |
| The format of something others write against — a file, a protocol, a script API | Measurements and defect autopsies that name unreleased work |

A document does not move to `private/` because it is unfinished. It moves there because its
contents are ours alone. If in doubt, write it here: the platform is easier to build on when the
reasoning behind it is readable.

## Writing one

The house rules are in [development.md](../development.md) §8, and they hold here:

- **English**, and in the same push as the code it describes.
- **Terse: what it is, why it exists, what it guarantees and where it stops.** Short statements,
  no story. How it was built, the forks, the measurements and what is still broken are written
  in full in `private/`.
- **State the need, not the status.** "The engine cannot do X yet" goes stale the day X lands;
  "a report needs X, because …" stays true.
- **Say why an interface has its shape**, in a line. The roads not taken are argued in `private/`.
- Name the file after its subject, in lower case with dashes: `record-locks.md`, not `RecordLocks.md`.

Add a row to the group it belongs to when you add a document, so this folder can be read from one place.

## Documents

### Building a configuration

| Document | What it holds |
|---|---|
| [access-rights.md](access-rights.md) | Roles: the rights on an object kind and on the configuration, row restrictions as a debuggable handler, how several roles compose |
| [ai-quickstart.md](ai-quickstart.md) | Building a configuration with an assistant: connecting it to the designer over MCP, the first request, where it stops |
| [configuration-files.md](configuration-files.md) | A configuration as a directory of text files: the layout, the value kinds, export and import over MCP, publication and refusal |
| [event-handlers.md](event-handlers.md) | Event handlers: one event of many objects handled in one module, the call order, the composite rule, the manager events |
| [functional-options.md](functional-options.md) | Functional options: a constant that decides what is shown, membership kept on the member, the one rule, where it stops |
| [registers.md](registers.md) | The four registers: what each keeps and how it is read, a recorder's set, totals kept by the database, what a balance guarantees |
| [reports.md](reports.md) | Reports and the data composer: a declared schema rendered to query text, composing as a background read, the reader's settings and variants |
| [translations.md](translations.md) | The engine's own translations: one dictionary per language, chosen per thread by the base it works for; the base's locale; a language's name |

### What a script works with

| Document | What it holds |
|---|---|
| [date-model.md](date-model.md) | The date: a wall-clock reading in `ibDateTime`, its calendar, its doors, stored forms, where it stops |
| [http-client.md](http-client.md) | HTTP and HTTPS from a script: the values, what they guarantee, where they stop |
| [json.md](json.md) | JSON from a script: what reading gives, what the writer guarantees, where it stops |
| [number-model.md](number-model.md) | The number: an exact decimal in `ibNumber`, the one bounded step (division), the rounding rules, its doors |
| [query-language.md](query-language.md) | The query language: sources, references, expressions, totals, the package; the one door under every front-end |
| [xml.md](xml.md) | XML from a script: the reader node by node, the writer, the encodings, what nothing outside the document can reach |

### Running a base

| Document | What it holds |
|---|---|
| [application-cluster.md](application-cluster.md) | The application cluster: a process per base, the server folder, restarts, routing, TLS, sealed passwords, the memory wall |
| [applying-a-configuration.md](applying-a-configuration.md) | Updating the database configuration: the schema as a function of two configurations, all or nothing, the dry run, where it stops |
| [debugger.md](debugger.md) | The debugger: the debuggee listens, one debugger and many listeners, a stop per session, the sandbox that changes nothing |
| [jobs.md](jobs.md) | Jobs: platform, scheduled and background work in one manager, the schedule, starting one from a script, one run across servers |
| [record-locks.md](record-locks.md) | Record locks: the version on the object, the row lock while writing, the session's lock that names its holder |

### Clients and the wire

| Document | What it holds |
|---|---|
| [style.md](style.md) | The style: the items the look is painted with, a light and a dark value for each, the platform's style and one lying over it |
| [thin-client.md](thin-client.md) | The thin client: its parts, who does what, the document/view skeleton, spreadsheets, printing, a file base, what it guarantees |
| [browser-client.md](browser-client.md) | The browser client: a page with no build, its parts, who does what, what it guarantees, where it stops |
| [client-protocol.md](client-protocol.md) | How a client works with a base: transports, methods, the frame, schemas, questions, files, the menu, what it guarantees |
| [listeners.md](listeners.md) | The listeners and the ports: TLS when backend.conf names a certificate; where the clear is allowed, and where it is refused |

### The engine's words

| Document | What it holds |
|---|---|
| [vocabulary.md](vocabulary.md) | The words the engine is built in: door, owner, road, holder, refusal…; each with its rule and one place in the code where it holds |
