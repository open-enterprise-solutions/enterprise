# Building a configuration with an assistant

**What.** The designer carries an MCP server. An assistant that speaks MCP - Claude Code, Claude
Desktop, any MCP client - reads and edits the configuration, applies it, runs code in the
application, composes reports and reads their cells back. What it builds is an ordinary
configuration: the same metadata, modules and forms a person makes with the mouse.

**Why.** A configuration is a description the platform turns into tables, forms and reports. An
assistant that can apply its own description and run it checks what it wrote the way a person does -
by running it - instead of guessing.

## Connect

1. Take the build for your platform from the
   [nightly release](https://github.com/open-enterprise-solutions/enterprise/releases/tag/nightly)
   (Windows, Linux, macOS; the database engine is inside) and unpack it.
2. Start `launcher`, press **Add**, choose **File (local database)**, pick an empty folder, save.
   Open it with **Designer**.
3. In the designer: **Tools → Options… → Assistant access (MCP)**. Tick *Let an assistant reach this
   designer* and press **Start now**.
4. Under *Give this to your assistant*: **Copy the configuration** for any MCP client, or **Copy the
   command** for Claude Code - one line, `claude mcp add --transport http oes <address> --header
   "Authorization: Bearer <key>"`.

The key is made once and kept, so a connected assistant stays connected. Whoever holds it can do in
this designer what you can.
The server listens on `127.0.0.1:3737`; set another address to reach it from another machine.

## The first request

Say what the business keeps and what it wants to see, in plain words:

> Build a configuration for a small service company: clients, services with a price, an order that
> lists services, and sales by client and service. Apply it, start the application, post three
> orders, and show sales by month.

The assistant creates the objects, writes the posting code, applies the configuration, runs the
application and reads the report back. The designer shows what it does as it does it; change
anything in plain words, or with the mouse.

## Where it stops

- One designer answers per address and port: with two open on the same port, the first one does.
- A change that restructures stored data is applied with the base held exclusively.
- The assistant works in the base the designer has open, and in nothing else.
