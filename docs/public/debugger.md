# The debugger

**What.** The designer debugs a configuration's code where it runs: breakpoints, step into, over and out,
pause, the call stack, the locals, watch expressions evaluated in the stopped frame. The same stop serves
a role's row restriction ([access-rights.md](access-rights.md)) and an event handler alike - they are
modules like any other.

**Why.** Code that runs on a server has nowhere to print to and nobody watching. A stop in the running
process, with its own data and its own session, answers "why" where a log only answers "what". The
debugger is the platform's, not the designer's: the engine carries it, and the designer is one listener.

## How it works

- **The process being debugged listens; the designer connects.** A process that opens a file base offers
  a debug server on `localhost`, the first free port of 7700-7709, and the designer reaches out to it. It is the debuggee that knows it exists; a tool cannot be found by a
  process that is already running. Debugging is switched on and off per process, as the person allows.
- **One debugger per server, many listeners.** A thin designer holds no debugger of its own: the
  server's engine debugger sends it every event over the client protocol, and its commands go back the
  same way, on a second connection so a step is never queued behind a frame
  ([client-protocol.md](client-protocol.md)). An assistant over MCP listens to the same stop beside the
  person - the same stack, the same locals.
- **A stop belongs to a session.** Each session parks on its own, with its own watch list, so two
  sessions stopped at the same time do not wake each other. The breakpoints are the process's, as the
  compiled modules are.
- **Lines follow the text.** The breakpoints are kept on the server, one map; a saved edit moves them
  with the lines they were set on.

## What it guarantees

- **The right code under the cursor.** On connecting, the debuggee says which configuration it runs;
  the designer takes the connection only for the configuration it has open.
- **A watch changes nothing.** A watch, a tooltip or a completion probe is evaluated so that no write,
  no handler and no message to the person can happen from it.
- **The sandbox changes nothing either.** Whole statements run in the stopped session, inside a
  transaction that is rolled back - a commit the code makes itself included - so an assistant can read
  real data and try a write without keeping it. What the code prints, alerts or asks comes back as text
  to whoever asked.
- **Answers reach whoever asked.** Each listener stamps its questions, and an answer goes only to the
  one that carries its stamp.

## Where it stops

- An application server ([application-cluster.md](application-cluster.md)) opens its bases with no debug
  server, and no one session's person may switch on a server that would stop every base and session it
  holds. Code that runs there is debugged on a file base.
- The debug server listens on this machine only. Its handshake checks the configuration, not the
  person: it is not an authentication, and the loopback is what keeps a stranger out.
- The handshake compares the configuration's identity, not its version: a debuggee still running an
  older build of the same configuration is accepted, and its lines may not match the editor's.
- A breakpoint is a line: there is no condition on it, and no stop on an exception.
- No stepping inside the sandbox: its code is the caller's own, and a breakpoint there would nest one
  stop inside another.
