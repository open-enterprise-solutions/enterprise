#!/usr/bin/env python3
"""
oes-rag-local smoke test.

Verifies the v1 contract:
  1. `--help` exits 0 and lists the three subcommands.
  2. `ingest` against a missing directory exits non-zero with structured error.
  3. `ingest` against a directory with at least one .xml file produces a
     JSON index (chunks > 0).
  4. `serve` starts, /health returns valid JSON (200 OR 503 depending on
     whether Ollama is reachable on the machine running the test).
  5. `serve` with /query returns 200 + results array.
  6. `serve` with bad payload returns 400.

Skips when the binary does not exist (build target not configured on
this CI worker).
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

BIN_CANDIDATES = [
    REPO_ROOT / "build" / "bin" / "Debug" / "oes-rag-local",
    REPO_ROOT / "build" / "bin" / "Release" / "oes-rag-local",
    REPO_ROOT / "build" / "bin" / "oes-rag-local",
]


def find_binary() -> Path | None:
    for c in BIN_CANDIDATES:
        if c.exists() and os.access(c, os.X_OK):
            return c
    return None


def http_get(url: str, timeout: float = 3.0) -> tuple[int, dict]:
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            payload = json.loads(e.read().decode("utf-8"))
        except Exception:
            payload = {}
        return e.code, payload


def http_post(url: str, body: dict, timeout: float = 3.0) -> tuple[int, dict]:
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            payload = json.loads(e.read().decode("utf-8"))
        except Exception:
            payload = {}
        return e.code, payload


def main() -> int:
    binary = find_binary()
    if not binary:
        print("oes-rag-local binary not found — skipping smoke test.")
        return 0

    failures: list[str] = []

    # --- 1. --help ---
    r = subprocess.run(
        [str(binary), "--help"],
        capture_output=True, text=True, timeout=10,
    )
    if r.returncode != 0:
        failures.append(f"--help exited {r.returncode}, expected 0")
    if "ingest" not in r.stderr or "serve" not in r.stderr or "health" not in r.stderr:
        failures.append("--help output missing one of: ingest/serve/health")

    # --- 2. ingest with missing directory ---
    r = subprocess.run(
        [str(binary), "ingest", "/tmp/oes-rag-local-does-not-exist-xyz"],
        capture_output=True, text=True, timeout=10,
    )
    if r.returncode == 0:
        failures.append("ingest <missing-dir> succeeded, expected non-zero exit")

    # --- 3. ingest with real corpus ---
    with tempfile.TemporaryDirectory() as td:
        corpus = Path(td) / "corpus"
        corpus.mkdir()
        (corpus / "c.xml").write_text(
            '<?xml version="1.0"?>\n<Catalog name="SmokeTestCatalog">'
            '<attribute name="Code"/></Catalog>\n', encoding="utf-8")
        index_path = Path(td) / "index.meta.json"
        r = subprocess.run(
            [str(binary), "ingest", str(corpus), "--out", str(index_path)],
            capture_output=True, text=True, timeout=10,
        )
        if r.returncode != 0:
            failures.append(
                f"ingest succeeded path exited {r.returncode}: {r.stderr}")
        if not index_path.exists():
            failures.append("ingest did not produce index file")
        else:
            try:
                parsed = json.loads(index_path.read_text())
                if "chunks" not in parsed or len(parsed["chunks"]) < 1:
                    failures.append("ingest index has no chunks")
            except Exception as e:
                failures.append(f"ingest index not valid JSON: {e}")

        # --- 4. serve + /health ---
        port = 11707  # unlikely to clash with anything
        proc = subprocess.Popen(
            [str(binary), "serve", str(index_path), "--port", str(port),
             "--ollama", "http://localhost:1"],   # ollama=invalid → /health=503
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        try:
            time.sleep(1.0)
            try:
                code, body = http_get(f"http://127.0.0.1:{port}/health")
            except Exception as e:
                failures.append(f"/health probe raised {e}")
                code, body = 0, {}

            # Either 200 (Ollama reachable) or 503 (not) — both are
            # contract-correct depending on the machine.
            if code not in (200, 503):
                failures.append(f"/health returned unexpected status {code}")
            for k in ("ok", "chunks", "ollamaUp", "loaded"):
                if k not in body:
                    failures.append(f"/health body missing '{k}'")

            # --- 5. /query happy path ---
            try:
                code, body = http_post(
                    f"http://127.0.0.1:{port}/query",
                    {"text": "SmokeTestCatalog", "topK": 3},
                )
            except Exception as e:
                failures.append(f"/query probe raised {e}")
                code, body = 0, {}
            if code != 200:
                failures.append(f"/query returned {code}, expected 200")
            if not isinstance(body.get("results"), list):
                failures.append("/query body missing 'results' array")
            if body.get("_source") != "local-rag-fallback":
                failures.append("/query body missing '_source' marker")

            # --- 6. /query bad payload ---
            try:
                code, body = http_post(
                    f"http://127.0.0.1:{port}/query", {},
                )
            except Exception as e:
                failures.append(f"/query bad-payload raised {e}")
                code, body = 0, {}
            if code != 400:
                failures.append(f"/query bad-payload returned {code}, expected 400")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    if failures:
        print("FAIL — oes-rag-local smoke test:")
        for f in failures:
            print(f"  - {f}")
        return 1

    print("PASS — oes-rag-local smoke test (6/6)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
