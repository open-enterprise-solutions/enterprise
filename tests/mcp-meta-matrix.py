#!/usr/bin/env python3
import json
import subprocess
import sys
import time


def send(proc, req):
    proc.stdin.write((json.dumps(req, ensure_ascii=False) + "\n").encode("utf-8"))
    proc.stdin.flush()


def recv(proc, expected_id=None, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            time.sleep(0.05)
            continue
        msg = json.loads(line.decode("utf-8"))
        if expected_id is None or msg.get("id") == expected_id:
            return msg
        continue
    raise TimeoutError("no JSON-RPC response")


def call(proc, id_, name, arguments, expect_ok=True):
    send(proc, {
        "jsonrpc": "2.0",
        "id": id_,
        "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    })
    res = recv(proc, expected_id=id_)
    if "error" in res:
        if expect_ok:
            raise RuntimeError(f"{name} protocol error: {res['error']}")
        return False, str(res["error"])
    result = res.get("result", {})
    text = result.get("content", [{}])[0].get("text", "")
    if result.get("isError"):
        if expect_ok:
            raise RuntimeError(f"{name} business error: {text}")
        return False, text
    return True, result


def wait_ready(proc, first_id):
    deadline = time.time() + 120
    req_id = first_id
    last = None
    while time.time() < deadline:
        ok, result = call(proc, req_id, "config_info", {})
        req_id += 1
        last = result.get("structuredContent", {})
        if not last:
            text = result.get("content", [{}])[0].get("text", "{}")
            try:
                last = json.loads(text)
            except json.JSONDecodeError:
                last = {"raw": text[:500]}
        if last.get("ready") is True:
            return req_id
        time.sleep(1)
    raise RuntimeError(f"configuration did not become ready: {last}")


def main():
    if len(sys.argv) != 3:
        print("usage: oes_mcp_meta_matrix.py <oes-mcp> <config-path>", file=sys.stderr)
        return 2

    binary, config_path = sys.argv[1], sys.argv[2]
    proc = subprocess.Popen(
        [binary, config_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    created_roots = []
    rows = []
    next_id = 2
    stamp = str(int(time.time()))

    def record(label, ok, detail=""):
        rows.append({"case": label, "ok": bool(ok), "detail": str(detail)[:260]})

    def create(kind, full_name, props=None, expect_ok=True):
        nonlocal next_id
        ok, detail = call(proc, next_id, "meta_create", {
            "kind": kind,
            "fullName": full_name,
            "properties": props or {},
        }, expect_ok=expect_ok)
        next_id += 1
        return ok, detail

    def query(full_name):
        nonlocal next_id
        ok, detail = call(proc, next_id, "meta_query", {"fullName": full_name}, expect_ok=False)
        next_id += 1
        return ok, detail

    def delete(full_name):
        nonlocal next_id
        ok, detail = call(proc, next_id, "meta_delete", {
            "fullName": full_name,
            "properties": {"force": True, "confirmBurst": True},
        }, expect_ok=False)
        next_id += 1
        return ok, detail

    try:
        time.sleep(0.5)
        send(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        print("initialize", file=sys.stderr, flush=True)
        init = recv(proc, expected_id=1)
        if init.get("result", {}).get("serverInfo", {}).get("name") != "oes-mcp":
            print(f"INIT_FAIL {init}", file=sys.stderr)
            return 1
        send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        print("wait_ready", file=sys.stderr, flush=True)
        next_id = wait_ready(proc, 2)
        print("ready", file=sys.stderr, flush=True)

        top_level = [
            ("Catalog", {"synonym": "Probe catalog", "comment": "probe"}),
            ("Document", {"synonym": "Probe document"}),
            ("Enumeration", {"synonym": "Probe enum"}),
            ("Constant", {"synonym": "Probe constant"}),
            ("CommonModule", {"synonym": "Probe module", "moduleCode": "Procedure Ping() {\n}\n"}),
            # CommonForm currently fails during runtime form lifecycle in
            # headless MCP; keep it in dedicated negative coverage instead
            # of poisoning the rest of the matrix.
            ("CommonTemplate", {"synonym": "Probe template"}),
            ("DataProcessor", {"synonym": "Probe processor"}),
            ("Report", {"synonym": "Probe report"}),
            ("InformationRegister", {"synonym": "Probe info register"}),
            ("AccumulationRegister", {"synonym": "Probe accum register"}),
            ("ChartOfCharacteristicTypes", {"synonym": "Probe characteristic chart"}),
            ("ChartOfAccounts", {"synonym": "Probe account chart"}),
            ("AccountingRegister", {"synonym": "Probe accounting register"}),
        ]

        for kind, props in top_level:
            root = f"{kind}.MCPMatrix{kind}{stamp}"
            ok, detail = create(kind, root, props, expect_ok=False)
            print(f"create {root}: {ok}", file=sys.stderr, flush=True)
            record(f"create {root}", ok, detail if not ok else "")
            if ok:
                created_roots.append(root)
                qok, qdetail = query(root)
                record(f"query {root}", qok, qdetail if not qok else "")

        roots_by_kind = {r.split(".", 1)[0]: r for r in created_roots}
        ref_catalog = roots_by_kind.get("Catalog")
        ref_type = f"CatalogRef.{ref_catalog.split('.', 1)[1]}" if ref_catalog else "String"

        child_cases = [
            ("Catalog", "Attributes.Phone", "Attribute", {"type": "String", "length": 20, "synonym": "Телефон"}),
            ("Catalog", "Forms.ItemForm", "Form", {"formType": "ItemForm", "synonym": "Форма елемента"}),
            ("Catalog", "Templates.PrintForm", "Template", {"synonym": "Друкована форма"}),
            ("Catalog", "TabularSections.Contacts", "TabularSection", {"synonym": "Контакти", "attributes": [{"name": "Value", "type": "String", "length": 80, "synonym": "Значення"}]}),
            ("Document", "Attributes.Amount", "Attribute", {"type": "Number", "precision": 2, "synonym": "Сума"}),
            ("Document", "Attributes.Counterparty", "Attribute", {"type": ref_type, "synonym": "Контрагент"}),
            ("Document", "Forms.ItemForm", "Form", {"formType": "ItemForm", "synonym": "Форма документа"}),
            ("Document", "TabularSections.Lines", "TabularSection", {"synonym": "Рядки", "attributes": [{"name": "Quantity", "type": "Number", "precision": 3, "synonym": "Кількість"}]}),
            ("Report", "Forms.ReportForm", "Form", {"formType": "Form", "synonym": "Форма звіту"}),
            ("DataProcessor", "Forms.ProcessorForm", "Form", {"formType": "Form", "synonym": "Форма обробки"}),
            ("InformationRegister", "Dimensions.Counterparty", "Dimension", {"type": ref_type, "synonym": "Контрагент"}),
            ("InformationRegister", "Resources.Amount", "Resource", {"type": "Number", "precision": 2, "synonym": "Сума"}),
            ("AccumulationRegister", "Dimensions.Warehouse", "Dimension", {"type": ref_type, "synonym": "Склад"}),
            ("AccumulationRegister", "Resources.Quantity", "Resource", {"type": "Number", "precision": 3, "synonym": "Кількість"}),
        ]
        for parent_kind, suffix, kind, props in child_cases:
            root = roots_by_kind.get(parent_kind)
            if not root:
                record(f"create {parent_kind}.{suffix}", False, "parent was not created")
                continue
            full_name = f"{root}.{suffix}"
            print(f"create {full_name}", file=sys.stderr, flush=True)
            ok, detail = create(kind, full_name, props, expect_ok=False)
            record(f"create {full_name}", ok, detail if not ok else "")
            if ok:
                print(f"query {full_name}", file=sys.stderr, flush=True)
                qok, qdetail = query(full_name)
                record(f"query {full_name}", qok, qdetail if not qok else "")
                if qok and kind in ("Attribute", "Dimension", "Resource"):
                    sc = qdetail.get("structuredContent", {})
                    text = qdetail.get("content", [{}])[0].get("text", "{}")
                    try:
                        payload = json.loads(text)
                    except json.JSONDecodeError:
                        payload = {}
                    type_count = payload.get("typeClassCount", 0)
                    record(f"type {full_name}", type_count > 0,
                           f"typeClassCount={type_count}")

        module_targets = [
            ("Catalog", "ObjectModule"),
            ("Catalog", "ManagerModule"),
            ("Document", "ObjectModule"),
            ("Document", "ManagerModule"),
            ("Report", "ObjectModule"),
            ("Report", "ManagerModule"),
            ("DataProcessor", "ObjectModule"),
            ("DataProcessor", "ManagerModule"),
            ("InformationRegister", "ObjectModule"),
            ("InformationRegister", "ManagerModule"),
            ("AccumulationRegister", "ObjectModule"),
            ("AccumulationRegister", "ManagerModule"),
            ("CommonModule", None),
        ]
        for parent_kind, module_name in module_targets:
            root = roots_by_kind.get(parent_kind)
            if not root:
                record(f"edit {parent_kind}.{module_name or ''}", False, "parent was not created")
                continue
            full_name = root if module_name is None else f"{root}.{module_name}"
            print(f"edit {full_name}", file=sys.stderr, flush=True)
            next_id += 1
            ok, detail = call(proc, next_id - 1, "meta_edit", {
                "fullName": full_name,
                "patch": {"moduleCode": "Procedure Probe() {\n}\n"} if module_name else {"moduleCode": "Procedure Probe() {\n}\n"},
            }, expect_ok=False)
            record(f"edit {full_name}", ok, detail if not ok else "")

        failures = [r for r in rows if not r["ok"]]
        print(json.dumps({
            "ok": not failures,
            "createdRoots": len(created_roots),
            "caseCount": len(rows),
            "failures": failures,
        }, ensure_ascii=False, indent=2))
        return 0 if not failures else 1
    finally:
        for root in reversed(created_roots):
            try:
                delete(root)
            except Exception:
                pass
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
