#!/usr/bin/env python3
"""
oes-mcp smoke test.

Launches the oes-mcp binary, drives it over stdio JSON-RPC, and asserts
the documented contract:

  1. initialize       → returns serverInfo + capabilities.tools
  2. tools/list       → returns >=10 tools including meta_query, meta_create
  3. tools/call config_info → success
  4. tools/call list_objects → returns array (may be empty if no config)
  5. tools/call save_config  → succeeds when a config is loaded
  6. lock-conflict path — when sys/.oes.lock pins an exclusive holder
     held by a live pid, oes-mcp must exit code 2 with the documented
     "configuration is locked exclusively" diagnostic on stderr.

Skips when the binary does not exist (build target not configured).
Skips load-bound assertions when no OES_CONFIG_PATH is set.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

BIN_CANDIDATES = [
    REPO_ROOT / "build" / "bin" / "Debug"   / "oes-mcp",
    REPO_ROOT / "build" / "bin" / "Release" / "oes-mcp",
    REPO_ROOT / "build" / "bin"             / "oes-mcp",
]


def find_binary() -> Path | None:
    for c in BIN_CANDIDATES:
        if c.exists() and os.access(c, os.X_OK):
            return c
    return None


def send(proc: subprocess.Popen, req: dict) -> None:
    line = json.dumps(req) + "\n"
    proc.stdin.write(line.encode("utf-8"))
    proc.stdin.flush()


def recv(proc: subprocess.Popen, timeout_s: float = 5.0) -> dict:
    deadline = time.time() + timeout_s
    chunks: list[bytes] = []
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            time.sleep(0.05)
            continue
        chunks.append(line)
        try:
            return json.loads(b"".join(chunks).decode("utf-8"))
        except json.JSONDecodeError:
            continue
    raise TimeoutError("oes-mcp did not respond within timeout")


def main() -> int:
    binary = find_binary()
    if binary is None:
        print("SKIP: oes-mcp binary not found in build/bin/{Debug,Release,}/",
              file=sys.stderr)
        return 0

    config_path = os.environ.get("OES_CONFIG_PATH", "")
    protocol_only = not config_path
    if protocol_only:
        print("INFO: OES_CONFIG_PATH not set — running protocol-only smoke "
              "with --no-config (tools that need a config will return isError)",
              file=sys.stderr)
        cmd = [str(binary), "--no-config"]
    else:
        cmd = [str(binary), config_path]
    print(f"Launching: {' '.join(cmd)}", file=sys.stderr)
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    # Allow init to settle (sees "oes-mcp: ready on stdio" on stderr).
    time.sleep(0.5)
    if proc.poll() is not None:
        err = proc.stderr.read().decode("utf-8", errors="replace")
        print(f"FAIL: oes-mcp exited during startup. stderr:\n{err}",
              file=sys.stderr)
        return 1

    failures: list[str] = []

    # 1) initialize
    send(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
    init = recv(proc)
    if init.get("result", {}).get("serverInfo", {}).get("name") != "oes-mcp":
        failures.append(f"initialize: unexpected response: {init}")
    # MCP spec 2025-06-18: capabilities.resources.subscribe must be true so
    # clients know they can call resources/subscribe. capabilities.tools is
    # advertised as an object with listChanged: true.
    caps = init.get("result", {}).get("capabilities", {})
    if not isinstance(caps.get("resources"), dict):
        failures.append("initialize: capabilities.resources missing")
    elif caps["resources"].get("subscribe") is not True:
        failures.append(
            f"initialize: capabilities.resources.subscribe must be true, "
            f"got: {caps.get('resources')}")

    # notifications/initialized — no response expected
    send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})

    # 2) tools/list
    send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
    lst = recv(proc)
    tools = lst.get("result", {}).get("tools", [])
    names = {t["name"] for t in tools if "name" in t}
    if len(tools) < 37:
        failures.append(f"tools/list: only {len(tools)} tools, expected >=37")
    for required in ("meta_query", "meta_create", "list_objects", "config_info",
                     "snapshots_list", "snapshot_rollback",
                     "headless_smoke_run"):
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")

    # Auto-snapshot tools (added 2026-05-21): assert annotations match the
    # design — snapshots_list is read-only/idempotent; snapshot_rollback
    # carries the destructive hint so agents prompt the user even on
    # dry-run (per spec, hint is a property of the tool, not the call).
    declared_for_snap = {t["name"]: t for t in tools}
    sl = declared_for_snap.get("snapshots_list")
    if sl is not None:
        ann = sl.get("annotations", {})
        if ann.get("readOnlyHint") is not True:
            failures.append("snapshots_list: readOnlyHint must be true")
        if ann.get("destructiveHint") is True:
            failures.append("snapshots_list: destructiveHint must be false")
        if not isinstance(sl.get("outputSchema"), dict):
            failures.append("snapshots_list: missing outputSchema")
    sr = declared_for_snap.get("snapshot_rollback")
    if sr is not None:
        ann = sr.get("annotations", {})
        if ann.get("destructiveHint") is not True:
            failures.append("snapshot_rollback: destructiveHint must be true")
        if ann.get("idempotentHint") is not True:
            failures.append("snapshot_rollback: idempotentHint must be true")
        if not isinstance(sr.get("outputSchema"), dict):
            failures.append("snapshot_rollback: missing outputSchema")

    # 3) tools/call config_info
    send(proc, {
        "jsonrpc": "2.0", "id": 3, "method": "tools/call",
        "params": {"name": "config_info", "arguments": {}},
    })
    info = recv(proc)
    if "result" not in info:
        failures.append(f"config_info: no result: {info}")

    # 4) tools/call list_objects
    send(proc, {
        "jsonrpc": "2.0", "id": 4, "method": "tools/call",
        "params": {"name": "list_objects", "arguments": {}},
    })
    objs = recv(proc)
    if "result" not in objs:
        failures.append(f"list_objects: no result: {objs}")

    # 5) tools/call headless_smoke_run — in --no-config mode it must return a
    # structured isError rather than crash; with a loaded config it should
    # return load/compile/tests fields.
    send(proc, {
        "jsonrpc": "2.0", "id": 5, "method": "tools/call",
        "params": {
            "name": "headless_smoke_run",
            "arguments": {"runTests": False},
        },
    })
    smoke = recv(proc)
    if "result" not in smoke:
        failures.append(f"headless_smoke_run: no result: {smoke}")
    else:
        sc = smoke["result"].get("structuredContent", {})
        for key in ("ok", "load", "compile", "tests"):
            if key not in sc:
                failures.append(
                    f"headless_smoke_run: structuredContent missing {key}: {sc}")

    # 6) tools/call save_config — skipped in --no-config mode (would just
    # report isError; protocol-only mode already proved by step 4).
    if not protocol_only:
        send(proc, {
            "jsonrpc": "2.0", "id": 6, "method": "tools/call",
            "params": {"name": "save_config", "arguments": {}},
        })
        save = recv(proc)
        # save_config can legitimately fail if the config dir is read-only;
        # we accept either a clean success or an isError envelope.
        if "result" not in save:
            failures.append(f"save_config: no result: {save}")

    # 7) tools/call sigma_check — proxies to Pugi MCP. The tool MUST
    # always return a well-formed envelope: either a real Pugi response,
    # a Pugi-side error (isError=true with HTTP status), or the offline
    # fail-open envelope. Any of those is acceptable; the failure mode is
    # the server crashing or returning no result.
    send(proc, {
        "jsonrpc": "2.0", "id": 7, "method": "tools/call",
        "params": {
            "name": "sigma_check",
            "arguments": {"metadata": {"kind": "Catalog", "name": "SmokeTest"}},
        },
    })
    sigma = recv(proc, timeout_s=15.0)  # Pugi roundtrip can take a few seconds.
    if "result" not in sigma:
        failures.append(f"sigma_check: no result envelope: {sigma}")
    else:
        env = sigma["result"]
        # Envelope must carry at least content[] (per MCP spec). isError
        # and structuredContent are optional but the content list is not.
        if not isinstance(env.get("content"), list) or not env["content"]:
            failures.append(f"sigma_check: malformed envelope (no content): {env}")

    # 8) MCP spec 2025-06-18: tools/list MUST surface outputSchema for the
    # three read-heavy tools (meta_query, list_objects, read_module). The
    # other tools either return unstructured text or already document the
    # shape in their description.
    declared = {t["name"]: t for t in tools}
    for required_name in ("meta_query", "list_objects", "read_module"):
        td = declared.get(required_name)
        if td is None:
            continue  # already reported above
        out_schema = td.get("outputSchema")
        if not isinstance(out_schema, dict) or out_schema.get("type") != "object":
            failures.append(f"{required_name}: missing/malformed outputSchema in tools/list")
        elif "properties" not in out_schema:
            failures.append(f"{required_name}: outputSchema has no properties")

    # 8) tools/call meta_query, list_objects, read_module — when not an
    # error envelope, MUST emit structuredContent matching the declared
    # outputSchema (basic shape check: required keys present).
    #
    # In --no-config mode these tools return isError envelopes (no config
    # loaded); the spec allows structuredContent to be omitted or carry an
    # error-shaped object in that case. We only assert presence of the
    # required keys when isError is false.
    expected_required = {
        "meta_query":   {"fullName", "kind"},
        "list_objects": {"count", "objects"},
        "read_module":  {"path", "kind", "source"},
    }
    probes = [
        ("meta_query",   {"fullName": "Catalog.SmokeProbe"}),
        ("list_objects", {}),
        ("read_module",  {"fullName": "Catalog.SmokeProbe.ObjectModule"}),
    ]
    for idx, (tool_name, tool_args) in enumerate(probes, start=100):
        send(proc, {
            "jsonrpc": "2.0", "id": idx, "method": "tools/call",
            "params": {"name": tool_name, "arguments": tool_args},
        })
        resp = recv(proc)
        env = resp.get("result")
        if not isinstance(env, dict):
            failures.append(f"{tool_name}: no result envelope: {resp}")
            continue
        if env.get("isError"):
            # Acceptable in --no-config mode or when the probe target
            # doesn't exist. We don't enforce structuredContent shape on
            # error envelopes — error shape is allowed to differ from the
            # declared outputSchema per spec.
            continue
        sc = env.get("structuredContent")
        if not isinstance(sc, dict):
            failures.append(f"{tool_name}: success envelope missing structuredContent")
            continue
        missing = expected_required[tool_name] - set(sc.keys())
        if missing:
            failures.append(
                f"{tool_name}: structuredContent missing required keys {missing}")

    # 8b) Pugi template-proxy tools (added 2026-05-21). All four must be
    # advertised by tools/list and must respond to tools/call with a
    # well-formed envelope. Three outcomes are acceptable per spec:
    #   a) success envelope with content[] (Pugi reachable + populated)
    #   b) Pugi-error envelope (isError=true; mock mode or remote 4xx)
    #   c) offline envelope (no aiBridge.env / no creds)
    template_tools = (
        "oes_templates_list",
        "oes_template_get",
        "oes_template_customize",
        "oes_demo_data_get",
    )
    for required in template_tools:
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")
    # oes_templates_list also declares an outputSchema — verify it.
    td_list = declared.get("oes_templates_list")
    if td_list is not None:
        out_schema = td_list.get("outputSchema")
        if not isinstance(out_schema, dict) or out_schema.get("type") != "object":
            failures.append("oes_templates_list: missing/malformed outputSchema")
        elif "templates" not in out_schema.get("properties", {}):
            failures.append("oes_templates_list: outputSchema missing 'templates' property")

    # tools/call probes — args chosen to pass client-side validation so the
    # only failure surface is the Pugi round-trip itself.
    template_probes = [
        ("oes_templates_list",     {}),
        ("oes_template_get",       {"templateId": "accounting-demo"}),
        ("oes_template_customize", {"templateId": "accounting-demo",
                                    "userPrompt": "rename Контрагенты to Партнёры"}),
        ("oes_demo_data_get",      {"templateId": "accounting-demo"}),
    ]
    for idx, (tool_name, tool_args) in enumerate(template_probes, start=200):
        send(proc, {
            "jsonrpc": "2.0", "id": idx, "method": "tools/call",
            "params": {"name": tool_name, "arguments": tool_args},
        })
        # Pugi round-trip can take a few seconds; mirror sigma_check's timeout.
        resp = recv(proc, timeout_s=15.0)
        env = resp.get("result")
        if not isinstance(env, dict):
            failures.append(f"{tool_name}: no result envelope: {resp}")
            continue
        # All three acceptable outcomes carry a non-empty content[] list.
        if not isinstance(env.get("content"), list) or not env["content"]:
            failures.append(f"{tool_name}: malformed envelope (no content): {env}")

    # 8c) Functional test runner (added 2026-05-21). Tool must be
    # advertised in tools/list with an outputSchema declaring summary +
    # tests[] shape. In --no-config mode, run_tests returns isError
    # because activeMetaData isn't loaded.
    if "run_tests" not in names:
        failures.append("tools/list: missing tool 'run_tests'")
    else:
        td_rt = declared.get("run_tests")
        if td_rt is not None:
            out_schema_rt = td_rt.get("outputSchema")
            if not isinstance(out_schema_rt, dict) or out_schema_rt.get("type") != "object":
                failures.append("run_tests: missing/malformed outputSchema")
            else:
                props_rt = out_schema_rt.get("properties", {})
                for required_prop in ("summary", "tests"):
                    if required_prop not in props_rt:
                        failures.append(
                            f"run_tests: outputSchema missing '{required_prop}' property")
            # Annotations sanity: readOnly=true, openWorld=false. The tool
            # touches the DB transiently but rollback yields a net-read
            # semantic; agents should treat it as read-only.
            ann_rt = td_rt.get("annotations") or {}
            if ann_rt.get("openWorldHint") is True:
                failures.append("run_tests: openWorldHint must be false")
        send(proc, {
            "jsonrpc": "2.0", "id": 400, "method": "tools/call",
            "params": {"name": "run_tests", "arguments": {"filter": "*"}},
        })
        rt_resp = recv(proc, timeout_s=10.0)
        rt_env = rt_resp.get("result")
        if not isinstance(rt_env, dict):
            failures.append(f"run_tests: no result envelope: {rt_resp}")
        elif protocol_only:
            # --no-config mode: must surface isError with a structuredContent
            # error envelope (OES_E_NO_CONFIG).
            if not rt_env.get("isError"):
                failures.append(
                    "run_tests: expected isError in --no-config mode, "
                    f"got: {rt_env}")
        else:
            # Config-loaded mode: envelope must include structuredContent
            # with summary+tests keys (test list may be empty if no @test
            # markers exist in the config).
            sc = rt_env.get("structuredContent")
            if not isinstance(sc, dict):
                failures.append("run_tests: success envelope missing structuredContent")
            else:
                missing_rt = {"summary", "tests"} - set(sc.keys())
                if missing_rt:
                    failures.append(
                        f"run_tests: structuredContent missing keys {missing_rt}")

    # 8d) Form-layout authoring tools (added 2026-05-21). Both
    # form_layout_read and form_layout_set are advertised with
    # outputSchema today, but their server-side implementation is
    # DEFERRED — they always return isError. The smoke test pins:
    #   - both names visible in tools/list
    #   - outputSchema declared on both
    #   - --no-config mode: returns isError (RequireConfig path) with
    #     a structuredContent envelope carrying errorCode
    form_layout_tools = ("form_layout_read", "form_layout_set")
    for required in form_layout_tools:
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")
            continue
        td_fl = declared.get(required)
        if td_fl is None:
            continue
        out_schema_fl = td_fl.get("outputSchema")
        if not isinstance(out_schema_fl, dict) or out_schema_fl.get("type") != "object":
            failures.append(f"{required}: missing/malformed outputSchema")
        # Annotations sanity: form_layout_read is read-only, set is
        # destructive (overwrites the entire form layout when wired).
        ann_fl = td_fl.get("annotations") or {}
        if required == "form_layout_read" and ann_fl.get("readOnlyHint") is not True:
            failures.append("form_layout_read: readOnlyHint must be true")
        if required == "form_layout_set" and ann_fl.get("destructiveHint") is not True:
            failures.append("form_layout_set: destructiveHint must be true")
        if ann_fl.get("openWorldHint") is True:
            failures.append(f"{required}: openWorldHint must be false")

    if "form_layout_read" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 500, "method": "tools/call",
            "params": {"name": "form_layout_read",
                       "arguments": {"fullName": "Catalog.SmokeProbe.Forms.ItemForm"}},
        })
        fr_resp = recv(proc, timeout_s=5.0)
        fr_env = fr_resp.get("result")
        if not isinstance(fr_env, dict):
            failures.append(f"form_layout_read: no result envelope: {fr_resp}")
        elif not fr_env.get("isError"):
            failures.append(
                f"form_layout_read: expected isError (deferred + "
                f"--no-config), got: {fr_env}")
        else:
            sc = fr_env.get("structuredContent")
            if not isinstance(sc, dict) or "errorCode" not in sc:
                failures.append(
                    f"form_layout_read: error envelope missing "
                    f"structuredContent.errorCode: {fr_env}")

    if "form_layout_set" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 501, "method": "tools/call",
            "params": {"name": "form_layout_set",
                       "arguments": {"fullName": "Catalog.SmokeProbe.Forms.ItemForm",
                                     "controls": []}},
        })
        fs_resp = recv(proc, timeout_s=5.0)
        fs_env = fs_resp.get("result")
        if not isinstance(fs_env, dict):
            failures.append(f"form_layout_set: no result envelope: {fs_resp}")
        elif not fs_env.get("isError"):
            failures.append(
                f"form_layout_set: expected isError (deferred + "
                f"--no-config), got: {fs_env}")
        else:
            sc = fs_env.get("structuredContent")
            if not isinstance(sc, dict) or "errorCode" not in sc:
                failures.append(
                    f"form_layout_set: error envelope missing "
                    f"structuredContent.errorCode: {fs_env}")

    # 8e) BAS / 1С migration tools (added 2026-05-21). Both must be
    # advertised in tools/list with outputSchema and annotations.
    # In --no-config mode they return isError (RequireConfig path);
    # smoke verifies the envelope shape.
    bas_tools = ("import_bas_xml", "import_bas_cf")
    for required in bas_tools:
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")
            continue
        td_bas = declared.get(required)
        if td_bas is None:
            continue
        out_schema_bas = td_bas.get("outputSchema")
        if not isinstance(out_schema_bas, dict) or out_schema_bas.get("type") != "object":
            failures.append(f"{required}: missing/malformed outputSchema")
        # Both BAS tools read local files only — openWorld MUST be false.
        ann_bas = td_bas.get("annotations") or {}
        if ann_bas.get("openWorldHint") is True:
            failures.append(f"{required}: openWorldHint must be false (local file read)")
        # destructive=false (additive — wizard Applier owns apply decision).
        if ann_bas.get("destructiveHint") is True:
            failures.append(f"{required}: destructiveHint must be false")

    # Probe import_bas_xml: in --no-config mode -> isError (no config loaded).
    if "import_bas_xml" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 600, "method": "tools/call",
            "params": {"name": "import_bas_xml",
                       "arguments": {"configurationPath": "/nonexistent/Configuration.xml"}},
        })
        bx_resp = recv(proc, timeout_s=5.0)
        bx_env = bx_resp.get("result")
        if not isinstance(bx_env, dict):
            failures.append(f"import_bas_xml: no result envelope: {bx_resp}")
        elif not bx_env.get("isError"):
            failures.append(
                f"import_bas_xml: expected isError in --no-config mode, got: {bx_env}")

    # Probe import_bas_cf: same expectation.
    if "import_bas_cf" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 601, "method": "tools/call",
            "params": {"name": "import_bas_cf",
                       "arguments": {"cfPath": "/nonexistent/fake.cf"}},
        })
        bc_resp = recv(proc, timeout_s=5.0)
        bc_env = bc_resp.get("result")
        if not isinstance(bc_env, dict):
            failures.append(f"import_bas_cf: no result envelope: {bc_resp}")
        elif not bc_env.get("isError"):
            failures.append(
                f"import_bas_cf: expected isError in --no-config mode, got: {bc_env}")

    # Validation guard for oes_demo_data_get: both args together → isError.
    send(proc, {
        "jsonrpc": "2.0", "id": 299, "method": "tools/call",
        "params": {"name": "oes_demo_data_get",
                   "arguments": {"templateId": "accounting-demo",
                                 "configHints": {"kind": "Catalog"}}},
    })
    bad = recv(proc, timeout_s=15.0)
    bad_env = bad.get("result") or {}
    if not bad_env.get("isError"):
        failures.append(
            "oes_demo_data_get: both templateId+configHints should be "
            f"rejected client-side, got: {bad_env}")

    # 8f) Role / ACL / Journal / Register / Predefined tools (added 2026-05-21).
    # All 8 must be advertised in tools/list. Read-only ones declare
    # outputSchema; mutating ones don't. In --no-config mode every tool
    # returns isError because RequireConfig fails first.
    new_tools_readonly = {
        "role_list":              True,   # outputSchema required
        "role_acl_read":          True,
        "journal_query":          True,
        "register_query":         True,
        "predefined_values_list": True,
    }
    new_tools_mutating = {
        "role_acl_set":           True,   # destructiveHint required
        "register_write":         False,  # additive
        "predefined_values_set":  True,
    }
    for required in (*new_tools_readonly.keys(), *new_tools_mutating.keys()):
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")
            continue
        td_new = declared.get(required)
        if td_new is None:
            continue
        ann_new = td_new.get("annotations") or {}
        # openWorld=false on every new tool (closed metadata domain).
        if ann_new.get("openWorldHint") is True:
            failures.append(f"{required}: openWorldHint must be false")
        # Read-only ones must declare outputSchema.
        if required in new_tools_readonly:
            if ann_new.get("readOnlyHint") is not True:
                failures.append(f"{required}: readOnlyHint must be true")
            out_schema_new = td_new.get("outputSchema")
            if not isinstance(out_schema_new, dict) or out_schema_new.get("type") != "object":
                failures.append(f"{required}: missing/malformed outputSchema")
        # Mutating ones — destructiveHint per per-tool expectation.
        else:
            expected_destructive = new_tools_mutating[required]
            if ann_new.get("destructiveHint") is not expected_destructive:
                failures.append(
                    f"{required}: destructiveHint must be {expected_destructive}")

    # In --no-config mode every new tool returns isError. We probe each
    # with minimal valid args so the path-walking and shape-checking lands
    # on the RequireConfig guard rather than failing client-side validation.
    new_tool_probes = [
        ("role_list",              {}),
        ("role_acl_read",          {"fullName": "Role.Smoke"}),
        ("role_acl_set",           {"fullName": "Role.Smoke", "permissions": []}),
        ("journal_query",          {"fullName": "Document.Smoke"}),
        ("register_query",         {"fullName": "AccumulationRegister.Smoke"}),
        ("register_write",         {"fullName": "AccumulationRegister.Smoke",
                                    "recordType": "Receive",
                                    "period": "2026-01-01T00:00:00"}),
        ("predefined_values_list", {"fullName": "Catalog.Smoke"}),
        ("predefined_values_set",  {"fullName": "Catalog.Smoke",
                                    "predefined": [{"name": "Demo"}]}),
    ]
    for idx, (tool_name, tool_args) in enumerate(new_tool_probes, start=700):
        send(proc, {
            "jsonrpc": "2.0", "id": idx, "method": "tools/call",
            "params": {"name": tool_name, "arguments": tool_args},
        })
        nt_resp = recv(proc, timeout_s=5.0)
        nt_env = nt_resp.get("result")
        if not isinstance(nt_env, dict):
            failures.append(f"{tool_name}: no result envelope: {nt_resp}")
            continue
        if not nt_env.get("isError"):
            # In --no-config mode every new tool must return isError. With
            # a real config some tools might succeed (e.g. role_list with
            # zero roles) — accept that too. Only flag when --no-config
            # mode produces a success envelope (means RequireConfig was
            # skipped, which would be a regression).
            if protocol_only:
                failures.append(
                    f"{tool_name}: expected isError in --no-config mode, "
                    f"got: {nt_env}")

    # 9) MCP resources (spec 2025-06-18, added 2026-05-21).
    #
    # The server advertises 5 resources:
    #   concrete  - oes://config/current, oes://sigma-rules
    #   templates - oes://catalog/{name}, oes://module/{owner}/{kind},
    #               oes://docs/syntax/{lang}
    # We assert list shapes, then probe a few reads. The docs/syntax read
    # depends on docs/oes-product-reference.md being reachable from CWD;
    # since the smoke test runs from the repo root that's the default.

    # resources/list — concrete only.
    send(proc, {"jsonrpc": "2.0", "id": 900, "method": "resources/list", "params": {}})
    r_list = recv(proc)
    r_arr = r_list.get("result", {}).get("resources", [])
    r_uris = {r.get("uri") for r in r_arr if isinstance(r, dict)}
    if "oes://config/current" not in r_uris:
        failures.append(f"resources/list: missing oes://config/current ({r_arr})")
    if "oes://sigma-rules" not in r_uris:
        failures.append(f"resources/list: missing oes://sigma-rules ({r_arr})")
    if len(r_arr) < 2:
        failures.append(f"resources/list: expected >=2 concrete entries, got {len(r_arr)}")

    # resources/templates/list — templates only.
    send(proc, {"jsonrpc": "2.0", "id": 901, "method": "resources/templates/list", "params": {}})
    t_list = recv(proc)
    t_arr = t_list.get("result", {}).get("resourceTemplates", [])
    t_uris = {t.get("uriTemplate") for t in t_arr if isinstance(t, dict)}
    for expected_template in (
        "oes://catalog/{name}",
        "oes://module/{owner}/{kind}",
        "oes://docs/syntax/{lang}",
    ):
        if expected_template not in t_uris:
            failures.append(
                f"resources/templates/list: missing {expected_template} ({t_arr})")
    if len(t_arr) < 3:
        failures.append(
            f"resources/templates/list: expected >=3 templates, got {len(t_arr)}")

    # resources/read on oes://sigma-rules — must return >=4 rules.
    send(proc, {
        "jsonrpc": "2.0", "id": 902, "method": "resources/read",
        "params": {"uri": "oes://sigma-rules"},
    })
    sr_resp = recv(proc)
    sr_contents = sr_resp.get("result", {}).get("contents", [])
    if not sr_contents:
        failures.append(f"resources/read sigma-rules: no contents ({sr_resp})")
    else:
        sr_text = sr_contents[0].get("text", "")
        try:
            sr_obj = json.loads(sr_text)
        except json.JSONDecodeError as e:
            failures.append(f"resources/read sigma-rules: invalid JSON: {e}")
            sr_obj = None
        if isinstance(sr_obj, dict):
            rules = sr_obj.get("rules", [])
            if len(rules) < 4:
                failures.append(
                    f"resources/read sigma-rules: expected >=4 rules, got {len(rules)}")
            rule_names = {r.get("name") for r in rules if isinstance(r, dict)}
            for required_rule in ("Σ-unique", "Σ-balance", "Σ-nonneg", "Σ-files"):
                if required_rule not in rule_names:
                    failures.append(
                        f"resources/read sigma-rules: missing rule '{required_rule}'")

    # resources/read on oes://docs/syntax/ces — must return non-empty markdown.
    # Set OES_DOCS_PATH via subprocess env if available (smoke might run
    # outside the repo root). The fallback inside resources.cpp walks the
    # CWD-relative paths so this typically passes from repo root.
    send(proc, {
        "jsonrpc": "2.0", "id": 903, "method": "resources/read",
        "params": {"uri": "oes://docs/syntax/ces"},
    })
    ds_resp = recv(proc)
    ds_result = ds_resp.get("result")
    if isinstance(ds_result, dict):
        ds_contents = ds_result.get("contents", [])
        if not ds_contents:
            failures.append(f"resources/read docs/syntax/ces: no contents")
        else:
            ds_text = ds_contents[0].get("text", "")
            if not ds_text or len(ds_text) < 100:
                failures.append(
                    f"resources/read docs/syntax/ces: text too short "
                    f"({len(ds_text)} bytes) — likely doc lookup failed")
    elif ds_resp.get("error", {}).get("code") == -32603:
        # Internal error — the docs file wasn't reachable. Acceptable
        # when smoke runs outside the repo (CI containers, etc.) but
        # noisy when running from the dev tree; flag as INFO not failure.
        print(f"INFO: resources/read docs/syntax/ces returned -32603 "
              f"(docs file not on path): {ds_resp.get('error', {}).get('message')}",
              file=sys.stderr)
    else:
        failures.append(f"resources/read docs/syntax/ces: unexpected envelope: {ds_resp}")

    # resources/read on an unknown URI — must return JSON-RPC error -32602.
    send(proc, {
        "jsonrpc": "2.0", "id": 904, "method": "resources/read",
        "params": {"uri": "oes://does-not-exist"},
    })
    un_resp = recv(proc)
    un_err = un_resp.get("error")
    if not isinstance(un_err, dict) or un_err.get("code") != -32602:
        failures.append(
            f"resources/read unknown: expected error -32602, got: {un_resp}")

    # resources/subscribe — concrete URI accepted.
    send(proc, {
        "jsonrpc": "2.0", "id": 905, "method": "resources/subscribe",
        "params": {"uri": "oes://config/current"},
    })
    sub_resp = recv(proc)
    if "result" not in sub_resp:
        failures.append(f"resources/subscribe: no result: {sub_resp}")

    # resources/subscribe on a template-expanded URI — accepted (matches
    # the registered template). Use a name that won't exist; subscribe
    # only validates the template match, not the underlying object.
    send(proc, {
        "jsonrpc": "2.0", "id": 906, "method": "resources/subscribe",
        "params": {"uri": "oes://catalog/SmokeProbe"},
    })
    sub2_resp = recv(proc)
    if "result" not in sub2_resp:
        failures.append(f"resources/subscribe (template): no result: {sub2_resp}")

    # resources/subscribe on a URI that matches no resource — error.
    send(proc, {
        "jsonrpc": "2.0", "id": 907, "method": "resources/subscribe",
        "params": {"uri": "oes://nope/wat"},
    })
    sub3_resp = recv(proc)
    if "error" not in sub3_resp:
        failures.append(
            f"resources/subscribe (unknown): expected error envelope: {sub3_resp}")

    # resources/unsubscribe — always succeeds (set semantics).
    send(proc, {
        "jsonrpc": "2.0", "id": 908, "method": "resources/unsubscribe",
        "params": {"uri": "oes://config/current"},
    })
    unsub_resp = recv(proc)
    if "result" not in unsub_resp:
        failures.append(f"resources/unsubscribe: no result: {unsub_resp}")

    # 10) MCP prompts (spec 2025-06-18, added 2026-05-21).
    #
    # 7 prompts surfaced as slash commands in MCP hosts:
    #   oes:new-catalog, oes:new-document, oes:migrate-1c-xml,
    #   oes:explain-object, oes:audit-security, oes:write-report,
    #   oes:write-form.
    #
    # initialize must already advertise capabilities.prompts.listChanged=true
    # (asserted in section 1 below — re-checked here for completeness so
    # this section is self-contained).
    prompts_cap = caps.get("prompts") if isinstance(caps, dict) else None
    if not isinstance(prompts_cap, dict):
        failures.append("initialize: capabilities.prompts missing")
    elif prompts_cap.get("listChanged") is not True:
        failures.append(
            f"initialize: capabilities.prompts.listChanged must be true, "
            f"got: {prompts_cap}")

    # prompts/list — must return 7 descriptors with name + description +
    # arguments.
    send(proc, {"jsonrpc": "2.0", "id": 1000, "method": "prompts/list", "params": {}})
    p_list = recv(proc)
    p_arr = p_list.get("result", {}).get("prompts", [])
    p_names = {p.get("name") for p in p_arr if isinstance(p, dict)}
    expected_prompts = {
        "oes:new-catalog",
        "oes:new-document",
        "oes:migrate-1c-xml",
        "oes:explain-object",
        "oes:audit-security",
        "oes:write-report",
        "oes:write-form",
    }
    missing_prompts = expected_prompts - p_names
    if missing_prompts:
        failures.append(f"prompts/list: missing {missing_prompts} (got {p_names})")
    if len(p_arr) < 7:
        failures.append(f"prompts/list: expected >=7 entries, got {len(p_arr)}")
    # Shape check on every entry: name + description + arguments[] present.
    for entry in p_arr:
        if not isinstance(entry, dict):
            continue
        if not entry.get("name"):
            failures.append(f"prompts/list: entry missing name: {entry}")
        if not entry.get("description"):
            failures.append(
                f"prompts/list: entry '{entry.get('name')}' missing description")
        if not isinstance(entry.get("arguments"), list):
            failures.append(
                f"prompts/list: entry '{entry.get('name')}' "
                f"arguments must be a list")

    # prompts/get on oes:new-catalog with {name: "Test"} — must return 1
    # user message containing the canonical tool sequence markers.
    send(proc, {
        "jsonrpc": "2.0", "id": 1001, "method": "prompts/get",
        "params": {"name": "oes:new-catalog", "arguments": {"name": "Test"}},
    })
    nc_resp = recv(proc)
    nc_result = nc_resp.get("result")
    if not isinstance(nc_result, dict):
        failures.append(f"prompts/get oes:new-catalog: no result: {nc_resp}")
    else:
        msgs = nc_result.get("messages", [])
        if not isinstance(msgs, list) or len(msgs) != 1:
            failures.append(
                f"prompts/get oes:new-catalog: expected exactly 1 message, "
                f"got: {msgs}")
        else:
            first = msgs[0]
            if not isinstance(first, dict):
                failures.append(
                    f"prompts/get oes:new-catalog: message not an object: {first}")
            else:
                if first.get("role") != "user":
                    failures.append(
                        f"prompts/get oes:new-catalog: role must be 'user', "
                        f"got: {first.get('role')}")
                content = first.get("content")
                if not isinstance(content, dict):
                    failures.append(
                        f"prompts/get oes:new-catalog: content not an object")
                elif content.get("type") != "text":
                    failures.append(
                        f"prompts/get oes:new-catalog: content.type must "
                        f"be 'text', got: {content.get('type')}")
                else:
                    txt = content.get("text", "")
                    for marker in ("meta_create", "sigma_check"):
                        if marker not in txt:
                            failures.append(
                                f"prompts/get oes:new-catalog: rendered text "
                                f"missing '{marker}' marker")
                    if "Test" not in txt:
                        failures.append(
                            "prompts/get oes:new-catalog: rendered text does "
                            "not substitute the 'name' argument")

    # prompts/get with a missing required argument — must return error.
    send(proc, {
        "jsonrpc": "2.0", "id": 1002, "method": "prompts/get",
        "params": {"name": "oes:new-catalog", "arguments": {}},
    })
    miss_resp = recv(proc)
    miss_err = miss_resp.get("error")
    if not isinstance(miss_err, dict) or miss_err.get("code") != -32602:
        failures.append(
            f"prompts/get missing-arg: expected error -32602, got: {miss_resp}")

    # prompts/get on unknown name — must return error.
    send(proc, {
        "jsonrpc": "2.0", "id": 1003, "method": "prompts/get",
        "params": {"name": "oes:does-not-exist", "arguments": {}},
    })
    unk_resp = recv(proc)
    unk_err = unk_resp.get("error")
    if not isinstance(unk_err, dict) or unk_err.get("code") != -32602:
        failures.append(
            f"prompts/get unknown-name: expected error -32602, got: {unk_resp}")

    # 8g) Refactoring primitives (5) — added 2026-05-21. find_references,
    # rename_with_refs, metadata_diff, dependency_graph,
    # extract_module_to_common. All 5 must be advertised; find_references
    # and dependency_graph declare outputSchema. In --no-config mode every
    # tool returns isError because RequireConfig fails first.
    refactor_tools_readonly = {
        "find_references":   True,  # outputSchema required
        "dependency_graph":  True,  # outputSchema required
    }
    refactor_tools_other = {
        "rename_with_refs":         False,  # destructive=true
        "metadata_diff":            True,   # readOnly=true (inline-snapshot mode)
        "extract_module_to_common": False,  # destructive=true
    }
    for required in (*refactor_tools_readonly.keys(), *refactor_tools_other.keys()):
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")
            continue
        td_rf = declared.get(required)
        if td_rf is None:
            continue
        ann_rf = td_rf.get("annotations") or {}
        if ann_rf.get("openWorldHint") is True:
            failures.append(f"{required}: openWorldHint must be false")
        # Read-only ones must declare outputSchema.
        if required in refactor_tools_readonly:
            if ann_rf.get("readOnlyHint") is not True:
                failures.append(f"{required}: readOnlyHint must be true")
            out_schema_rf = td_rf.get("outputSchema")
            if not isinstance(out_schema_rf, dict) or out_schema_rf.get("type") != "object":
                failures.append(f"{required}: missing/malformed outputSchema")
        # Destructive ones: destructiveHint=true except metadata_diff.
        else:
            if required == "metadata_diff":
                if ann_rf.get("readOnlyHint") is not True:
                    failures.append(f"{required}: readOnlyHint must be true")
            else:
                if ann_rf.get("destructiveHint") is not True:
                    failures.append(f"{required}: destructiveHint must be true")

    # In --no-config mode every refactor tool returns isError. We probe each
    # with minimal valid args so the path-walking lands on RequireConfig.
    refactor_probes = [
        ("find_references",    {"fullName": "Catalog.SmokeProbe"}),
        ("rename_with_refs",   {"fullName": "Catalog.SmokeProbe",
                                 "newName": "Catalog.SmokeProbe2",
                                 "dryRun": True}),
        ("metadata_diff",      {"leftSnapshot": {}, "rightSnapshot": {}}),
        ("dependency_graph",   {"fullName": "Catalog.SmokeProbe",
                                 "direction": "both", "depth": 2}),
        ("extract_module_to_common",
            {"sourceFullName":     "Document.SmokeProbe.ObjectModule",
             "procedureName":      "Foo",
             "targetCommonModule": "CommonModules.Smoke",
             "dryRun":             True}),
    ]
    for idx, (tool_name, tool_args) in enumerate(refactor_probes, start=800):
        send(proc, {
            "jsonrpc": "2.0", "id": idx, "method": "tools/call",
            "params": {"name": tool_name, "arguments": tool_args},
        })
        rf_resp = recv(proc, timeout_s=5.0)
        rf_env = rf_resp.get("result")
        if not isinstance(rf_env, dict):
            failures.append(f"{tool_name}: no result envelope: {rf_resp}")
            continue
        if not rf_env.get("isError"):
            # metadata_diff with empty snapshots is a valid call that
            # legitimately succeeds (returns empty added/removed/modified).
            # All other refactor tools require a loaded config — flag if
            # they succeed in --no-config mode.
            if protocol_only and tool_name != "metadata_diff":
                failures.append(
                    f"{tool_name}: expected isError in --no-config mode, "
                    f"got: {rf_env}")

    # 8d) Auto-snapshot tools (added 2026-05-21). In --no-config mode
    # snapshots_list must return a structured error (no config loaded —
    # the store is config-relative), and snapshot_rollback must reject
    # an unknown id with isError + structuredContent.errorCode.
    # In real-config mode, snapshots_list succeeds (possibly with an
    # empty list) and snapshot_rollback on an unknown id still surfaces
    # the OES_E_SNAPSHOT_NOT_FOUND envelope.
    snapshot_probes = [
        ("snapshots_list",    {"limit": 10}),
        ("snapshot_rollback", {"snapshotId": "00000000T000000-9999", "dryRun": True}),
    ]
    for idx, (tool_name, tool_args) in enumerate(snapshot_probes, start=500):
        send(proc, {
            "jsonrpc": "2.0", "id": idx, "method": "tools/call",
            "params": {"name": tool_name, "arguments": tool_args},
        })
        sn_resp = recv(proc, timeout_s=5.0)
        sn_env = sn_resp.get("result")
        if not isinstance(sn_env, dict):
            failures.append(f"{tool_name}: no result envelope: {sn_resp}")
            continue
        # Both probes target failure paths in --no-config mode. In real-
        # config mode, snapshots_list legitimately succeeds; the rollback
        # probe always errors because the id is fabricated.
        if tool_name == "snapshots_list" and protocol_only:
            if not sn_env.get("isError"):
                failures.append(
                    "snapshots_list: expected isError in --no-config mode, "
                    f"got: {sn_env}")
        if tool_name == "snapshot_rollback":
            if not sn_env.get("isError"):
                failures.append(
                    "snapshot_rollback: expected isError for unknown id, "
                    f"got: {sn_env}")

    # 8h) Transactional staging tools (added 2026-05-21).
    # Three tools: transaction_begin / transaction_commit / transaction_rollback.
    # All three must be advertised in tools/list with the correct annotations.
    # Protocol-level contract (--no-config or real config — both apply):
    #   - transaction_begin returns ok=true + transactionId
    #   - double transaction_begin returns isError (OES_E_TX_ALREADY_ACTIVE)
    #   - transaction_commit with valid id returns ok=true
    #   - transaction_commit with unknown id returns isError (OES_E_TX_NOT_FOUND)
    #   - transaction_rollback with unknown id returns isError (OES_E_TX_NOT_FOUND)
    # The transaction tools do NOT require a loaded config (they operate on
    # the metaBridge undo stack, which is always present).
    tx_tools = ("transaction_begin", "transaction_commit", "transaction_rollback")
    for required in tx_tools:
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")
            continue
        td_tx = declared.get(required)
        if td_tx is None:
            continue
        ann_tx = td_tx.get("annotations") or {}
        # All three: openWorld=false, readOnly=false.
        if ann_tx.get("openWorldHint") is True:
            failures.append(f"{required}: openWorldHint must be false")
        if ann_tx.get("readOnlyHint") is True:
            failures.append(f"{required}: readOnlyHint must be false (mutating)")
        # transaction_rollback must carry destructiveHint=true (it undoes mutations).
        if required == "transaction_rollback":
            if ann_tx.get("destructiveHint") is not True:
                failures.append("transaction_rollback: destructiveHint must be true")
        # transaction_commit and transaction_rollback are idempotent.
        if required in ("transaction_commit", "transaction_rollback"):
            if ann_tx.get("idempotentHint") is not True:
                failures.append(f"{required}: idempotentHint must be true")
        # transaction_begin must declare an outputSchema (transactionId field).
        out_schema_tx = td_tx.get("outputSchema")
        if not isinstance(out_schema_tx, dict) or out_schema_tx.get("type") != "object":
            failures.append(f"{required}: missing/malformed outputSchema")

    # Probe: transaction_begin -> returns transactionId.
    tx_id = None
    if "transaction_begin" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 1100, "method": "tools/call",
            "params": {
                "name": "transaction_begin",
                "arguments": {"label": "smoke-test"},
            },
        })
        tb_resp = recv(proc, timeout_s=5.0)
        tb_env = tb_resp.get("result")
        if not isinstance(tb_env, dict):
            failures.append(f"transaction_begin: no result envelope: {tb_resp}")
        elif tb_env.get("isError"):
            failures.append(
                f"transaction_begin: unexpected isError: {tb_env}")
        else:
            sc = tb_env.get("structuredContent")
            if not isinstance(sc, dict):
                failures.append("transaction_begin: success envelope missing structuredContent")
            else:
                tx_id = sc.get("transactionId")
                if not tx_id:
                    failures.append(
                        "transaction_begin: structuredContent missing transactionId")
                if "undoStackBefore" not in sc:
                    failures.append(
                        "transaction_begin: structuredContent missing undoStackBefore")

        # Probe: double transaction_begin must return isError.
        send(proc, {
            "jsonrpc": "2.0", "id": 1101, "method": "tools/call",
            "params": {"name": "transaction_begin", "arguments": {}},
        })
        tb2_resp = recv(proc, timeout_s=5.0)
        tb2_env = tb2_resp.get("result")
        if not isinstance(tb2_env, dict):
            failures.append(f"transaction_begin double: no result: {tb2_resp}")
        elif not tb2_env.get("isError"):
            failures.append(
                "transaction_begin double: expected isError (already active), "
                f"got: {tb2_env}")
        else:
            sc2 = tb2_env.get("structuredContent") or {}
            if sc2.get("errorCode") != "OES_E_TX_ALREADY_ACTIVE":
                failures.append(
                    "transaction_begin double: expected errorCode "
                    f"OES_E_TX_ALREADY_ACTIVE, got: {sc2.get('errorCode')}")

    # Probe: transaction_commit on the id we just opened.
    if tx_id and "transaction_commit" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 1102, "method": "tools/call",
            "params": {"name": "transaction_commit",
                       "arguments": {"transactionId": tx_id}},
        })
        tc_resp = recv(proc, timeout_s=5.0)
        tc_env = tc_resp.get("result")
        if not isinstance(tc_env, dict):
            failures.append(f"transaction_commit: no result: {tc_resp}")
        elif tc_env.get("isError"):
            failures.append(
                f"transaction_commit: unexpected isError for valid id: {tc_env}")
        else:
            sc_tc = tc_env.get("structuredContent") or {}
            if sc_tc.get("ok") is not True:
                failures.append(
                    f"transaction_commit: structuredContent.ok must be true: {sc_tc}")
            if "mutationsApplied" not in sc_tc:
                failures.append(
                    "transaction_commit: structuredContent missing mutationsApplied")
            if "undoStackAfter" not in sc_tc:
                failures.append(
                    "transaction_commit: structuredContent missing undoStackAfter")

    # Probe: transaction_commit with unknown id must return isError.
    if "transaction_commit" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 1103, "method": "tools/call",
            "params": {"name": "transaction_commit",
                       "arguments": {"transactionId": "00000000-0000-0000-0000-000000000000"}},
        })
        tc_unk_resp = recv(proc, timeout_s=5.0)
        tc_unk_env = tc_unk_resp.get("result")
        if not isinstance(tc_unk_env, dict):
            failures.append(f"transaction_commit unknown-id: no result: {tc_unk_resp}")
        elif not tc_unk_env.get("isError"):
            failures.append(
                "transaction_commit unknown-id: expected isError, "
                f"got: {tc_unk_env}")
        else:
            sc_unk = tc_unk_env.get("structuredContent") or {}
            if sc_unk.get("errorCode") != "OES_E_TX_NOT_FOUND":
                failures.append(
                    "transaction_commit unknown-id: expected OES_E_TX_NOT_FOUND, "
                    f"got: {sc_unk.get('errorCode')}")

    # Probe: transaction_rollback with unknown id must return isError.
    if "transaction_rollback" in names:
        send(proc, {
            "jsonrpc": "2.0", "id": 1104, "method": "tools/call",
            "params": {"name": "transaction_rollback",
                       "arguments": {"transactionId": "00000000-0000-0000-0000-000000000000"}},
        })
        tr_unk_resp = recv(proc, timeout_s=5.0)
        tr_unk_env = tr_unk_resp.get("result")
        if not isinstance(tr_unk_env, dict):
            failures.append(f"transaction_rollback unknown-id: no result: {tr_unk_resp}")
        elif not tr_unk_env.get("isError"):
            failures.append(
                "transaction_rollback unknown-id: expected isError, "
                f"got: {tr_unk_env}")
        else:
            sc_tr_unk = tr_unk_env.get("structuredContent") or {}
            if sc_tr_unk.get("errorCode") != "OES_E_TX_NOT_FOUND":
                failures.append(
                    "transaction_rollback unknown-id: expected OES_E_TX_NOT_FOUND, "
                    f"got: {sc_tr_unk.get('errorCode')}")

    proc.stdin.close()
    proc.wait(timeout=3)

    # 9) Lock-conflict path (Designer concurrency, 2026-05-21). When a
    # configuration directory contains a sys/.oes.lock manifest pinning
    # an EXCLUSIVE holder whose pid is alive, oes-mcp must refuse to
    # start and exit with code 2. We exercise this against a freshly
    # created empty directory — we don't need a real sys.fdb because
    # the lock probe runs BEFORE CreateFileAppDataEnv.
    #
    # We use the current python interpreter's pid as the "live exclusive
    # holder" so the liveness probe sees a real process. The probe is
    # best-effort cross-platform (signal 0 / OpenProcess) and treats
    # this script's pid as alive.
    print("INFO: probing lock-conflict path with a synthetic exclusive holder",
          file=sys.stderr)
    with tempfile.TemporaryDirectory(prefix="oes-mcp-lock-") as tmp:
        sys_dir = Path(tmp) / "sys"
        sys_dir.mkdir()
        lock_manifest = {
            "seq": 1,
            "holders": [
                {
                    "pid":     os.getpid(),
                    "mode":    "exclusive",
                    "since":   "2026-05-21T00:00:00Z",
                    "program": "smoke-test-fake-designer",
                }
            ],
        }
        (sys_dir / ".oes.lock").write_text(json.dumps(lock_manifest))

        result = subprocess.run(
            [str(binary), tmp],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=20,
        )
        if result.returncode != 2:
            failures.append(
                f"lock-conflict: expected exit code 2, got {result.returncode}. "
                f"stderr:\n{result.stderr.decode('utf-8', 'replace')}"
            )
        # The diagnostic must mention exclusive locking so operators can
        # see what to do (restart Designer in shared mode). We accept any
        # of the spec phrases — the wording can drift over translations.
        stderr_text = result.stderr.decode("utf-8", "replace")
        if "locked exclusively" not in stderr_text:
            failures.append(
                f"lock-conflict: stderr missing 'locked exclusively' phrase. "
                f"Got:\n{stderr_text}"
            )

    # 10) Clean-dir path. A configuration directory with NO lock file at
    # all must not blow up the lock probe — oes-mcp should treat the
    # absence as "no conflict" and proceed to the normal config load
    # (which may then succeed or fail on its own merits). We test by
    # pointing oes-mcp at an empty temp dir and expecting it NOT to exit
    # with code 2. The config load itself will fail (no sys.fdb), giving
    # exit code 1 — that's fine; the lock-probe path is what's under
    # test.
    print("INFO: probing clean-dir path (no .oes.lock present)", file=sys.stderr)
    with tempfile.TemporaryDirectory(prefix="oes-mcp-clean-") as clean_dir:
        result = subprocess.run(
            [str(binary), clean_dir],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=20,
        )
        if result.returncode == 2:
            failures.append(
                f"clean-dir: false positive lock conflict — exit code 2 with "
                f"no .oes.lock present. stderr:\n"
                f"{result.stderr.decode('utf-8', 'replace')}"
            )

    if failures:
        print("FAIL:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print("PASS: oes-mcp smoke test", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
