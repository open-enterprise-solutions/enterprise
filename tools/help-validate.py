#!/usr/bin/env python3
"""
help-validate.py — CI validator for the OES-HELP-1.0 corpus.

Walks data/help/<locale>/ and checks every bucket against the design
v5 §2 / §3 contract:

  - Top-level `format` == "OES-HELP-1.0", `schema_version` == 1, `locale`
    matches the directory name.
  - Each entry has required fields: id, name_local, name_en, kind,
    category_keys. Unknown kinds reject.
  - `id` grammar matches the §2.2 prefix table.
  - Every category_key resolves in `_categories.json` for the locale.
  - Every see_also id resolves somewhere in the same locale's corpus.
  - Duplicate ids inside a single locale are fatal.
  - Three first-class locales (en-US, ru-RU, uk-UA) must each have
    every entry id present (cross-locale coverage parity) — only
    enforced when `--require-locale-parity` is passed.
  - reviewed:false entries are allowed but counted; CI can use the
    `--no-drafts` switch (Phase 7 cutoff PR) to refuse any draft.

Exit codes:
  0 — clean
  1 — at least one validation failure
  2 — usage / I/O error

Usage:
    tools/help-validate.py --root data/help [--require-locale-parity] [--no-drafts]
"""

import argparse
import json
import re
import sys
from pathlib import Path


FORMAT_NAME    = "OES-HELP-1.0"
SCHEMA_VERSION = 1

VALID_KINDS = {
    "keyword",
    "system_function",
    "system_procedure",
    "system_constant",
    "system_enum",
    "metaobject_type",
    "metaobject_attribute",
    "metaobject_method",
    "primitive_type",
    "collection",
    "event",
    "operator",
}

KIND_PREFIX = {
    "keyword":               "kw.",
    "system_function":       "fn.",
    "system_procedure":      "fn.",
    "system_constant":       "const.",
    "system_enum":           "enum.",
    "metaobject_type":       "mo.",
    "metaobject_attribute":  "attr.",
    "metaobject_method":     "meth.",
    "primitive_type":        "type.",
    "collection":            "cls.",
    "event":                 "ev.",
    "operator":              "op.",
}

REQUIRED_FIELDS = ["id", "name_local", "name_en", "kind", "category_keys"]

EXPECTED_LOCALES = {"en-US", "ru-RU", "uk-UA"}


class Issue:
    """One validation finding. Severity 'fatal' fails the run; 'warning'
    is logged but does not flip the exit code."""
    __slots__ = ("path", "entry_id", "severity", "message")

    def __init__(self, path: Path, entry_id: str,
                 severity: str, message: str):
        self.path     = path
        self.entry_id = entry_id
        self.severity = severity
        self.message  = message

    def __str__(self) -> str:
        tag = "FATAL " if self.severity == "fatal" else "WARN  "
        eid = f" [{self.entry_id}]" if self.entry_id else ""
        return f"{tag}{self.path}{eid}: {self.message}"


def load_bucket(path: Path, issues: list) -> dict:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except json.JSONDecodeError as exc:
        issues.append(Issue(path, "", "fatal",
                            f"malformed JSON: {exc}"))
        return None
    except OSError as exc:
        issues.append(Issue(path, "", "fatal",
                            f"I/O error: {exc}"))
        return None


def validate_bucket_header(path: Path, doc: dict, expected_locale: str,
                            issues: list) -> bool:
    ok = True
    if doc.get("format") != FORMAT_NAME:
        issues.append(Issue(path, "", "fatal",
                            f"bad format: expected {FORMAT_NAME!r}, "
                            f"got {doc.get('format')!r}"))
        ok = False
    if doc.get("schema_version") != SCHEMA_VERSION:
        issues.append(Issue(path, "", "fatal",
                            f"bad schema_version: expected {SCHEMA_VERSION}, "
                            f"got {doc.get('schema_version')!r}"))
        ok = False
    if doc.get("locale") != expected_locale:
        issues.append(Issue(path, "", "fatal",
                            f"locale mismatch: directory says "
                            f"{expected_locale!r}, file says "
                            f"{doc.get('locale')!r}"))
        ok = False
    return ok


def validate_entry(path: Path, entry: dict, categories: set,
                    seen_ids: dict, issues: list) -> None:
    entry_id = entry.get("id", "<no-id>")

    # Required fields
    for field in REQUIRED_FIELDS:
        if field not in entry:
            issues.append(Issue(path, entry_id, "fatal",
                                f"missing required field {field!r}"))
            return  # cannot validate further without an id / kind

    # Kind validity
    kind = entry["kind"]
    if kind not in VALID_KINDS:
        issues.append(Issue(path, entry_id, "fatal",
                            f"unknown kind {kind!r} (valid: "
                            f"{', '.join(sorted(VALID_KINDS))})"))
        return

    # Id prefix matches kind
    expected_prefix = KIND_PREFIX[kind]
    if not entry_id.startswith(expected_prefix):
        issues.append(Issue(path, entry_id, "fatal",
                            f"id prefix mismatch — kind {kind!r} "
                            f"expects {expected_prefix!r}-prefixed id"))

    # Id grammar
    if not re.fullmatch(r"[a-z]+(\.[A-Za-z_][A-Za-z0-9_]*)+", entry_id):
        issues.append(Issue(path, entry_id, "fatal",
                            f"id {entry_id!r} does not match "
                            r"<prefix>.<Word>[.<Word>]+ grammar"))

    # Duplicate within bucket / locale
    if entry_id in seen_ids:
        issues.append(Issue(path, entry_id, "fatal",
                            f"duplicate id (also at {seen_ids[entry_id]})"))
    else:
        seen_ids[entry_id] = path

    # Category keys resolve in dictionary
    for key in entry["category_keys"]:
        if key not in categories:
            issues.append(Issue(path, entry_id, "fatal",
                                f"category_key {key!r} not in "
                                f"_categories.json"))

    # name fields non-empty
    if not entry["name_local"]:
        issues.append(Issue(path, entry_id, "warning", "empty name_local"))
    if not entry["name_en"]:
        issues.append(Issue(path, entry_id, "warning", "empty name_en"))


def load_categories(locale_dir: Path, issues: list) -> set:
    cat_path = locale_dir / "_categories.json"
    if not cat_path.exists():
        issues.append(Issue(cat_path, "", "fatal", "missing _categories.json"))
        return set()
    doc = load_bucket(cat_path, issues)
    if doc is None:
        return set()
    if doc.get("format") != FORMAT_NAME:
        issues.append(Issue(cat_path, "", "fatal",
                            f"bad format: {doc.get('format')!r}"))
    cats = doc.get("categories")
    if not isinstance(cats, dict):
        issues.append(Issue(cat_path, "", "fatal",
                            "missing or invalid 'categories' object"))
        return set()
    return set(cats.keys())


def validate_locale(root: Path, locale: str,
                     issues: list, no_drafts: bool) -> tuple[set, int]:
    locale_dir = root / locale
    if not locale_dir.is_dir():
        issues.append(Issue(locale_dir, "", "fatal",
                            f"locale directory missing"))
        return set(), 0

    categories = load_categories(locale_dir, issues)
    seen_ids   = {}
    draft_count = 0
    locale_ids = set()

    for bucket_path in sorted(locale_dir.glob("*.json")):
        if bucket_path.name.startswith("_"):
            continue
        doc = load_bucket(bucket_path, issues)
        if doc is None:
            continue
        validate_bucket_header(bucket_path, doc, locale, issues)
        entries = doc.get("entries")
        if not isinstance(entries, list):
            issues.append(Issue(bucket_path, "", "fatal",
                                "missing or invalid 'entries' array"))
            continue
        for entry in entries:
            if not isinstance(entry, dict):
                issues.append(Issue(bucket_path, "", "fatal",
                                    f"non-object entry: {entry!r}"))
                continue
            validate_entry(bucket_path, entry, categories, seen_ids, issues)
            eid = entry.get("id")
            if eid:
                locale_ids.add(eid)
            if not entry.get("reviewed", False):
                draft_count += 1
                if no_drafts:
                    issues.append(Issue(bucket_path, eid or "<no-id>", "fatal",
                                        "draft entry (reviewed=false) "
                                        "rejected by --no-drafts"))

    # See-also resolves in the same locale (after all buckets collected)
    for bucket_path in sorted(locale_dir.glob("*.json")):
        if bucket_path.name.startswith("_"):
            continue
        doc = load_bucket(bucket_path, [])
        if doc is None: continue
        for entry in doc.get("entries", []):
            for ref in entry.get("see_also", []):
                if ref not in locale_ids:
                    issues.append(Issue(bucket_path, entry.get("id",""),
                                        "warning",
                                        f"dangling see_also: {ref!r}"))

    return locale_ids, draft_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=Path("data/help"), type=Path,
                        help="Help-corpus root.")
    parser.add_argument("--require-locale-parity", action="store_true",
                        help="Fail if any entry id is missing from "
                              "any of en-US / ru-RU / uk-UA.")
    parser.add_argument("--no-drafts", action="store_true",
                        help="Fail on any reviewed=false entry "
                              "(Phase 7 ship gate).")
    args = parser.parse_args()

    if not args.root.is_dir():
        print(f"error: corpus root not found: {args.root}", file=sys.stderr)
        return 2

    locales = sorted(p.name for p in args.root.iterdir() if p.is_dir())
    if not locales:
        print(f"error: no locale directories in {args.root}", file=sys.stderr)
        return 2

    all_issues: list = []
    locale_ids_map: dict = {}
    drafts_total = 0

    for locale in locales:
        ids, drafts = validate_locale(args.root, locale,
                                        all_issues, args.no_drafts)
        locale_ids_map[locale] = ids
        drafts_total += drafts

    if args.require_locale_parity:
        # Every id present in any locale must be present in all.
        union = set().union(*locale_ids_map.values())
        for locale, ids in locale_ids_map.items():
            missing = union - ids
            for mid in sorted(missing):
                all_issues.append(Issue(args.root / locale, mid, "fatal",
                                        "id missing from this locale "
                                        "(--require-locale-parity)"))

    fatal_count   = sum(1 for i in all_issues if i.severity == "fatal")
    warning_count = sum(1 for i in all_issues if i.severity == "warning")

    for issue in all_issues:
        print(str(issue), file=sys.stderr)

    print("", file=sys.stderr)
    print(f"summary: {fatal_count} fatal, {warning_count} warnings",
          file=sys.stderr)
    print(f"locales: {', '.join(locales)}", file=sys.stderr)
    print(f"drafts (reviewed=false): {drafts_total}", file=sys.stderr)
    for locale, ids in locale_ids_map.items():
        print(f"  {locale}: {len(ids)} entries", file=sys.stderr)

    return 1 if fatal_count > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
