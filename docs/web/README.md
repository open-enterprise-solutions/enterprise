# Web runtime — working notes

Session notes for the OES Enterprise web frontend prototype. Complementary
to `docs/ARCHITECTURE.md`; captures active state, open issues, and the
debugging tools wired into the web build. Update as the prototype moves.

## Files in this folder

- [`architecture.md`](architecture.md) — split of `wfrontend.dll` /
  `wenterprise-server.exe`, session model, process-level entry points.
- [`session-lifecycle.md`](session-lifecycle.md) — `ibWebApplication`
  OnInit/OnExit, StartMainModule/ExitMainModule, SEH-wrapped call path.
- [`debugging.md`](debugging.md) — how to localize crashes in the web
  runtime (dbghelp symbol resolution, LoadControl traces, how to run the
  server locally).
- [`open-issues.md`](open-issues.md) — active TODOs and known bugs:
  unregistered control types, desktop-only singletons still being hit,
  follow-up control ports.
- [`client-protocol.md`](client-protocol.md) — **DESIGN, no code yet**: the wire
  contract the client and `wenterprise-server` are meant to speak — commands
  batched into numbered packets, one frame per batch, acknowledged counters with
  retransmit, and the client-side queue that coalesces and predicts. Written for
  slow links, where the cost is round trips rather than bytes.
- [`conventions.md`](conventions.md) — web-specific code conventions:
  `ibValuePtr` usage, `OES_USE_WEB` ifdef policy, JS per-type control
  renderers on the client.
- [`event-dispatcher.md`](event-dispatcher.md) — plan (not yet
  implemented): route incoming events through `wxEvtHandler::QueueEvent`
  reusing real wx event types (`wxEVT_BUTTON`, `wxEVT_TIMER`, ...);
  `ibWebApplication` as the session-level dispatcher, individual
  controls as per-control dispatchers.
- [`label-alignment.md`](label-alignment.md) — web counterpart of
  desktop's `CalculateAndApply`: post-render `requestAnimationFrame`
  pass measures labels client-side and sets `min-width` so columns
  line up. Mirrors the horizontal-sizer column-break rule.

## Quick reference — launch

```
bin\Win32\Debug\wenterprise-server.exe ^
  --file=F:\projects\oes-bin\examples\fb_test251 ^
  --port=8080
```

`--file=` is a **directory**; `appDataCreateFile` appends `sys.fdb`.
Browse to `http://localhost:8080/w/fb_test251/`.

## Quick reference — endpoints

| Path | Purpose |
|---|---|
| `GET /w/<prefix>/` | HTML shell; mints a new `oes_session` cookie |
| `POST /w/<prefix>/login` | `user=...&password=...` — runs `StartMainModule` |
| `GET /w/<prefix>/menu` | Top-level navigation JSON |
| `GET /w/<prefix>/forms` | Flat list of form metadata |
| `GET /w/<prefix>/form/<metaID>` | Open form, return control-tree JSON |
| `POST /w/<prefix>/action/<controlID>` | Fire control event (buttons today) |
| `POST /w/<prefix>/tab/<i>` | Switch active tab to index i, return new tab JSON |
| `DELETE /w/<prefix>/tab/<i>` | Close tab i, return new active tab JSON |
| `GET /w/<prefix>/session` | Session info: tabs, activeTab, formName |
| `GET /w/<prefix>/demo` / `/demo2` | Diagnostic: typed / name-based factories |
| `GET /w/<prefix>/diag/ctors` | Registered control ctors visible to wfrontend |
