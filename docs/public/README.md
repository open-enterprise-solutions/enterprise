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

Add a row to the table below when you add a document, so this folder can be read from one place.

## Documents

| Document | What it holds |
|---|---|
| [http-client.md](http-client.md) | HTTP and HTTPS from a script: the values, what they guarantee, where they stop |
| [json.md](json.md) | JSON from a script: what reading gives, what the writer guarantees, where it stops |
