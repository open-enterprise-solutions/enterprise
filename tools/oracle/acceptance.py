#!/usr/bin/env python3
"""Independent oracle for the accountant's examples (scenarios 1 and 2).

The figures are recomputed here from the input rows with decimal arithmetic.
Nothing in this file calls the engine. The C++ tests load the JSON this script
writes and compare the engine's answers to it.

    python3 tools/oracle/acceptance.py
"""

from __future__ import annotations

import json
from decimal import Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests" / "fixtures" / "acceptance"


def D(text: str) -> Decimal:
    return Decimal(text)


def num(value: Decimal) -> str:
    text = format(value, "f")
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def write(name: str, payload: dict) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {path.relative_to(ROOT)}")


# --- catalogs -----------------------------------------------------------------

GOODS = [
    {"code": "GOODS", "parent": "", "folder": True, "qty": "0", "deletion_mark": False, "predefined": "Goods"},
    {"code": "FOOD", "parent": "GOODS", "folder": True, "qty": "0", "deletion_mark": False, "predefined": ""},
    {"code": "BREAD", "parent": "FOOD", "folder": False, "qty": "10.5", "deletion_mark": False, "predefined": ""},
    {"code": "MILK", "parent": "FOOD", "folder": False, "qty": "4", "deletion_mark": False, "predefined": ""},
    {"code": "STALE", "parent": "FOOD", "folder": False, "qty": "100", "deletion_mark": True, "predefined": ""},
    {"code": "TOOLS", "parent": "GOODS", "folder": True, "qty": "0", "deletion_mark": False, "predefined": ""},
    {"code": "HAMMER", "parent": "TOOLS", "folder": False, "qty": "7.25", "deletion_mark": False, "predefined": ""},
    {"code": "SERVICES", "parent": "", "folder": True, "qty": "0", "deletion_mark": False, "predefined": ""},
    {"code": "DELIVERY", "parent": "SERVICES", "folder": False, "qty": "3", "deletion_mark": False, "predefined": ""},
]


def children_of(nodes, parent):
    return [n for n in nodes if n["parent"] == parent]


def subtree(nodes, root, include_self):
    """Parent-link walk. A node is expanded once, so a cycle cannot hang the oracle."""
    found = []
    seen = set()

    def walk(code, take):
        if code in seen:
            return
        seen.add(code)
        if take:
            found.append(code)
        for child in children_of(nodes, code):
            walk(child["code"], True)

    walk(root, include_self)
    return sorted(found)


def totals_under(nodes, root):
    by_code = {n["code"]: n for n in nodes}
    total = Decimal(0)
    for code in subtree(nodes, root, True):
        total += D(by_code[code]["qty"])
    return num(total)


def hierarchy_block(nodes):
    roots = ["GOODS", "FOOD", "TOOLS", "SERVICES"]
    return {
        "in_hierarchy": {root: subtree(nodes, root, True) for root in roots},
        "hierarchy_only": {"FOOD": subtree(nodes, "FOOD", False), "TOOLS": subtree(nodes, "TOOLS", False)},
        "totals": {root: totals_under(nodes, root) for root in roots},
    }


def catalogs():
    moved = []
    for node in GOODS:
        copy = dict(node)
        if copy["code"] == "HAMMER":
            copy["parent"] = "FOOD"
        moved.append(copy)

    owners = [
        {"code": "ACME", "predefined": "Acme"},
        {"code": "GLOBEX", "predefined": ""},
    ]
    contracts = [
        {"code": "C1", "owner": "ACME", "deletion_mark": False},
        {"code": "C2", "owner": "ACME", "deletion_mark": True},
        {"code": "C3", "owner": "GLOBEX", "deletion_mark": False},
    ]

    def of_owner(marked: bool):
        out = {}
        for owner in owners:
            codes = [
                row["code"]
                for row in contracts
                if row["owner"] == owner["code"] and (marked or not row["deletion_mark"])
            ]
            out[owner["code"]] = sorted(codes)
        return out

    return {
        "for_the_accountant": (
            "A goods catalog with folders. Bread, milk and a marked stale item sit in Food; "
            "a hammer sits in Tools, then moves into Food. A total for a folder adds everything "
            "recorded under it, including an item marked for deletion — the mark does not erase "
            "the row. The choice a person is offered is a different question: the marked item "
            "should not be offered, and a contract should be offered only for its owner. "
            "The platform, as it stands, still shows the marked item in the choice list and does "
            "not narrow that list by owner; a query that asks for the mark and the owner does."
        ),
        "goods": {
            "nodes": GOODS,
            "move": {"code": "HAMMER", "to": "FOOD"},
            "before": hierarchy_block(GOODS),
            "after": hierarchy_block(moved),
        },
        "choice": {
            "marked_code": "STALE",
            "platform_includes_deletion_mark": True,
            "reason": (
                "DeletionMark is documented as staying in the base and shown crossed out "
                "(commonObject.h, the DeletionMark property). FindValue adds no DeletionMark "
                "condition (referenceQuery.cpp). Excluding it from choice is not what that door does."
            ),
            "filtered_no_mark": sorted(n["code"] for n in GOODS if not n["deletion_mark"]),
        },
        "contracts": {
            "owners": owners,
            "items": contracts,
            "of_owner_including_marked": of_owner(True),
            "of_owner_choosable": of_owner(False),
            "choice_not_narrowed_by_owner": True,
            "reason": (
                "List owner says the choice is not narrowed to one owner by itself "
                "(catalog.h, the ListOwner property). A query of the Owner column's "
                "stored fields does narrow. ibDataQueryBuilder::Where on that reference "
                "returned no rows for the same bytes; the scenario asks the fields."
            ),
        },
    }


# --- accumulation register, balance type --------------------------------------

# Half-open days: a movement at midnight belongs to the day that starts there.
# A balance "at midnight, before that midnight" therefore leaves that day out.

MOVEMENTS = [
    {"doc": "R1", "line": 1, "when": "2026-03-01 10:00:00", "wh": "kitchen", "item": "flour", "receipt": True, "qty": "100.50", "active": True},
    {"doc": "R1", "line": 2, "when": "2026-03-01 10:00:00", "wh": "bar", "item": "flour", "receipt": True, "qty": "20", "active": True},
    {"doc": "X1", "line": 1, "when": "2026-03-01 11:00:00", "wh": "kitchen", "item": "flour", "receipt": True, "qty": "999", "active": False},
    {"doc": "R2", "line": 1, "when": "2026-03-01 18:30:00", "wh": "kitchen", "item": "flour", "receipt": False, "qty": "30.25", "active": True},
    {"doc": "R3", "line": 1, "when": "2026-03-02 00:00:00", "wh": "kitchen", "item": "flour", "receipt": True, "qty": "5", "active": True},
    {"doc": "R4", "line": 1, "when": "2026-03-02 12:00:00", "wh": "kitchen", "item": "flour", "receipt": False, "qty": "10", "active": True},
    {"doc": "R5", "line": 1, "when": "2026-03-15 00:00:00", "wh": "kitchen", "item": "flour", "receipt": True, "qty": "8", "active": True},
    {"doc": "R6", "line": 1, "when": "2026-03-15 09:00:00", "wh": "kitchen", "item": "sugar", "receipt": True, "qty": "2.50", "active": True},
    {"doc": "R7", "line": 1, "when": "2026-04-01 00:00:00", "wh": "kitchen", "item": "flour", "receipt": True, "qty": "1", "active": True},
]


def signed(row) -> Decimal:
    amount = D(row["qty"])
    return amount if row["receipt"] else -amount


def balance(rows, wh, item, at, excluding) -> Decimal:
    total = Decimal(0)
    for row in rows:
        if not row["active"] or row["wh"] != wh or row["item"] != item:
            continue
        if excluding and row["when"] < at:
            total += signed(row)
        elif not excluding and row["when"] <= at:
            total += signed(row)
    return total


def turnover(rows, wh, item, start, end):
    """Active movements in [start, end). End is exclusive, so midnight starts the next period."""
    receipt = Decimal(0)
    expense = Decimal(0)
    for row in rows:
        if not row["active"] or row["wh"] != wh or row["item"] != item:
            continue
        if row["when"] < start or row["when"] >= end:
            continue
        if row["receipt"]:
            receipt += D(row["qty"])
        else:
            expense += D(row["qty"])
    return receipt, expense


def bal(rows, wh, item, at, excluding):
    return {
        "kind": "balance",
        "wh": wh,
        "item": item,
        "at": at,
        "excluding": excluding,
        "qty": num(balance(rows, wh, item, at, excluding)),
    }


def turn(rows, wh, item, start, end):
    receipt, expense = turnover(rows, wh, item, start, end)
    return {
        "kind": "turnover",
        "wh": wh,
        "item": item,
        "from": start,
        "to": end,
        "receipt": num(receipt),
        "expense": num(expense),
        "net": num(receipt - expense),
    }


def bat(rows, wh, item, start, end):
    receipt, expense = turnover(rows, wh, item, start, end)
    opening = balance(rows, wh, item, start, True)
    return {
        "kind": "balance_and_turnovers",
        "wh": wh,
        "item": item,
        "from": start,
        "to": end,
        "opening": num(opening),
        "receipt": num(receipt),
        "expense": num(expense),
        "closing": num(opening + receipt - expense),
    }


def replace(rows, doc, line, **changes):
    out = []
    for row in rows:
        copy = dict(row)
        if copy["doc"] == doc and copy["line"] == line:
            copy.update(changes)
        out.append(copy)
    return out


def accumulation():
    rows = MOVEMENTS
    reposted = replace(rows, "R2", 1, qty="10.25")
    unposted = replace(reposted, "R3", 1, active=False)
    overdrawn = unposted + [{
        "doc": "R9",
        "line": 1,
        "when": "2026-04-02 10:00:00",
        "wh": "kitchen",
        "item": "flour",
        "receipt": False,
        "qty": "1000",
        "active": True,
    }]
    april = "2026-04-01 00:00:00"
    return {
        "for_the_accountant": (
            "Stock of flour in two warehouses, and sugar in the kitchen. Receipts add, expenses "
            "take away, and a line that was written but not posted adds nothing. The balance at "
            "midnight is the balance before the day that starts at that midnight: the receipt "
            "posted exactly at 2 March 00:00 is not in the balance at that midnight, and it is in "
            "the balance a moment later. A day's turnover is that day, from its midnight up to but "
            "not including the next. Posting the same document again replaces its line; undoing "
            "the posting takes the line back out. An expense larger than the stock is stored and "
            "the balance goes negative — the register does not refuse it. Whether it should is a "
            "question for the maintainer, not a figure this oracle invents a refusal for."
        ),
        "midnight_rule": (
            "A balance at a moment, excluding that moment, leaves out every movement at that "
            "moment and after it. At midnight that is the whole day which starts there."
        ),
        "movements": rows,
        "balances": [
            bal(rows, "kitchen", "flour", "2026-03-02 00:00:00", True),
            bal(rows, "kitchen", "flour", "2026-03-02 00:00:00", False),
            bal(rows, "kitchen", "flour", "2026-03-15 00:00:00", True),
            bal(rows, "kitchen", "flour", "2026-03-15 00:00:00", False),
            bal(rows, "kitchen", "flour", april, True),
            bal(rows, "kitchen", "flour", april, False),
            bal(rows, "bar", "flour", "2026-03-02 00:00:00", True),
            bal(rows, "kitchen", "sugar", "2026-03-15 00:00:00", True),
            bal(rows, "kitchen", "sugar", "2026-03-15 09:00:00", True),
            bal(rows, "kitchen", "sugar", "2026-03-15 09:00:00", False),
        ],
        "turnovers": [
            turn(rows, "kitchen", "flour", "2026-03-01 00:00:00", "2026-03-02 00:00:00"),
            turn(rows, "kitchen", "flour", "2026-03-02 00:00:00", "2026-03-03 00:00:00"),
            turn(rows, "kitchen", "flour", "2026-03-01 00:00:00", april),
            turn(rows, "bar", "flour", "2026-03-01 00:00:00", april),
        ],
        "balance_and_turnovers": [
            bat(rows, "kitchen", "flour", "2026-03-01 00:00:00", april),
            bat(rows, "bar", "flour", "2026-03-01 00:00:00", april),
        ],
        "repost": {
            "doc": "R2",
            "line": 1,
            "qty": "10.25",
            "receipt": False,
            "then": bal(reposted, "kitchen", "flour", april, True),
        },
        "unpost": {
            "doc": "R3",
            "line": 1,
            "then": bal(unposted, "kitchen", "flour", april, True),
        },
        "negative": {
            "doc": "R9",
            "line": 1,
            "when": "2026-04-02 10:00:00",
            "wh": "kitchen",
            "item": "flour",
            "receipt": False,
            "qty": "1000",
            "platform": "accepted",
            "accountant_expects": "refused",
            "reason": (
                "No negative-balance gate was found on the accumulation register's write path. "
                "The quantity below is the arithmetic if the expense is stored. Refusal would be "
                "a new rule; it is not invented here."
            ),
            "then": bal(overdrawn, "kitchen", "flour", "2026-04-02 10:00:00", False),
        },
    }


def main():
    write("catalogs.json", catalogs())
    write("accumulation.json", accumulation())


if __name__ == "__main__":
    main()
