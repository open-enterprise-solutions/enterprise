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

    # notifications/initialized — no response expected
    send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})

    # 2) tools/list
    send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
    lst = recv(proc)
    tools = lst.get("result", {}).get("tools", [])
    names = {t["name"] for t in tools if "name" in t}
    if len(tools) < 10:
        failures.append(f"tools/list: only {len(tools)} tools, expected >=10")
    for required in ("meta_query", "meta_create", "list_objects", "config_info"):
        if required not in names:
            failures.append(f"tools/list: missing tool '{required}'")

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

    # 5) tools/call save_config — skipped in --no-config mode (would just
    # report isError; protocol-only mode already proved by step 4).
    if not protocol_only:
        send(proc, {
            "jsonrpc": "2.0", "id": 5, "method": "tools/call",
            "params": {"name": "save_config", "arguments": {}},
        })
        save = recv(proc)
        # save_config can legitimately fail if the config dir is read-only;
        # we accept either a clean success or an isError envelope.
        if "result" not in save:
            failures.append(f"save_config: no result: {save}")

    # 6) tools/call sigma_check — proxies to Pugi MCP. The tool MUST
    # always return a well-formed envelope: either a real Pugi response,
    # a Pugi-side error (isError=true with HTTP status), or the offline
    # fail-open envelope. Any of those is acceptable; the failure mode is
    # the server crashing or returning no result.
    send(proc, {
        "jsonrpc": "2.0", "id": 6, "method": "tools/call",
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

    # 7) MCP spec 2025-06-18: tools/list MUST surface outputSchema for the
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
