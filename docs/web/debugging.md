# Debugging the web runtime

> **Scope — read this first, the two are often confused.** This file is about debugging
> **the platform itself**: running `wenterprise-server.exe` locally, catching access
> violations in C++, resolving a fault address to a source line, and the diagnostic HTTP
> endpoints. It is a C++ engineer's page.
>
> Debugging **a configuration's script** (breakpoints, stepping, Watch) is a different
> subsystem entirely — the Designer attaching over TCP to a running runtime. That is
> [../debugger-architecture.md](../debugger-architecture.md); note that unifying it with
> the web transport is still an open arc ([../ROADMAP.md § 2](../ROADMAP.md)), so the
> script debugger described there talks to `wenterprise-server` over the same TCP path as
> a desktop runtime.

## Running the server locally

```
bin\Win32\Debug\wenterprise-server.exe ^
  --file=F:\projects\oes-bin\examples\fb_test251 ^
  --port=8080
```

Notes:

- `--file=<dir>` expects a **directory**; `appDataCreateFile` appends
  `sys.fdb`. Passing the .fdb path directly leads to a double-fdb
  (`...\SYS.FDB\sys.fdb`) "Unsuccessful execution caused by a system
  error" message.
- Designer must not be holding the same database; Firebird embedded in
  Classic mode is multi-process but OES's exclusive-mode flag collides
  with Designer on the same file.
- If you get `ConfigStorage: Cannot initialize the shared memory region`
  in `bin/Win32/Debug/firebird.log`, check `%TEMP%` for orphaned
  `fb_lock_*` / `fb_monitor_*` / `fb_snap_*` / `fb_tpc_*` from a prior
  crash. Safe to remove when no FB/enterprise/designer process is alive.

## Catching crashes

Access violations in the script call path are caught by
`StartMainModuleSafe` / `ExitMainModuleSafe` in `webApplication.cpp`. The
SEH handler resolves the fault address to a function + source line using
`dbghelp.dll` (`SymFromAddr`, `SymGetLineFromAddr64`) and logs:

```
[app] <label> SEH: code=0xCode at 0xAddr (module.dll+0xRVA)
    symbol+0xDisp [file:line]
```

Requires the PDB to sit next to the DLL (always true for our Debug build).
`SymInitialize` is called lazily on first fault with
`SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME`.

## Narrowing a crash site

Once you have `module+RVA`, options in increasing effort:

1. **Symbol + line is already enough** — the handler prints it. Go read
   the code at that line.
2. **Still ambiguous (+0x80 into a function with multiple calls)**: add
   `std::cerr << "[TraceName] step"` lines around the block under
   `#ifdef OES_USE_WEB` — the same technique found the
   `this == nullptr` in `LoadControl`. See `formMem.cpp` and
   `frame.cpp` for the pattern (iostream include is also
   `OES_USE_WEB`-guarded to keep desktop build clean).
3. **Stack walk** would also be possible from the `__except` filter
   (`StackWalk64` + `SymFromAddr` per frame) — not implemented yet; add
   when a crash address alone is no longer enough.

## Useful diagnostic endpoints

- `GET /session` — `{tabCount, activeTab, tabs:[{formName,...}]}`
  confirms whether `CreateNewForm` ran (e.g. OnStart opening two forms
  produces tabCount=2).
- `GET /diag/ctors` — enumerates every ctor `ibValue::GetAvailableCtor`
  sees from wfrontend.dll's address space. Short/empty means
  `CONTROL_TYPE_REGISTER` statics were dropped by the linker (MSVC
  happily drops internal-linkage statics with no external references).
- `GET /forms` — flat list of every `ibValueMetaObjectFormBase` in the
  loaded configuration, as `{id, name, owner_id, owner_name}`.
- `GET /demo` / `GET /demo2` — builds a form via the typed factory
  (`CreateAndPrepareValueRef<T>`) and the name-based factory
  (`ibValueForm::CreateControl(wxT("Boxsizer"), ...)`) respectively.
  Returns the JSON tree so you can verify each pipeline in isolation.

## Log prefixes used

- `[app]` — `ibWebApplication` lifecycle (OnInit, CreateMainModule,
  StartMainModule + SEH). Live.

Two prefixes this file used to list — `[LoadControl]` (per-invocation trace in
`ibValueFrame::LoadControl`) and `[LoadChildForm]` (`NewObject` failures during
child-control deserialisation) — were **temporary instrumentation and are no longer in the
tree**. They are kept here only as the worked example of technique #2 above: when a fault
address alone was not enough, `std::cerr` traces around the suspect block under
`#ifdef OES_USE_WEB` found a `this == nullptr` in `LoadControl`. Add them the same way when
needed, and remove them again when the bug is closed.
