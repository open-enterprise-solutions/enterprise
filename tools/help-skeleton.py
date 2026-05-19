#!/usr/bin/env python3
"""
help-skeleton.py — generate empty OES-HELP-1.0 bucket skeletons from a
dumpHelp registry dump.

Consumes the JSON produced by `build/bin/Debug/dumpHelp` (the C++ CLI
tool in src/engine/dumpHelp/) — a flat list of every registered keyword
and built-in function in the OES core. Emits one bucket per (locale,
kind-category) into data/help/<locale>/ with `reviewed: false`,
empty body fields, and the right `category_keys` / `id` grammar so
the LLM filler stage (Phase 2 step 2) can drop prose into the right
slots without restructuring.

Phase 5 (per-configuration entries) is a separate generator that
consumes a metadata dump. This script only emits the platform corpus.

Usage:
    build/bin/Debug/dumpHelp > /tmp/oes-dump.json
    tools/help-skeleton.py --dump /tmp/oes-dump.json \\
                            --out  data/help \\
                            --locales en-US,ru-RU,uk-UA

Re-running the script is safe — it skips entries whose id is already
present in an existing bucket file UNLESS --overwrite is passed. By
default the generator preserves human / LLM-filled prose and only
adds new ids the registry has grown.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path


SCHEMA_VERSION = 1
FORMAT_NAME = "OES-HELP-1.0"

# Category keys per kind. Matches data/help/<locale>/_categories.json —
# the loader expects every category_key in an entry to exist in the
# dictionary. The full set of keys is owned by the dictionary file;
# this map is only the default landing spot for each generated entry.
KIND_CATEGORIES = {
    "keyword":         ["common_lang", "control_flow"],
    "system_function": ["global_context", "global_functions"],
    "system_procedure": ["global_context", "global_functions"],
}

# Default availability tier. Matches the hand-written en-US fixture
# convention. Per-locale overrides are layered in below.
DEFAULT_AVAILABILITY = {
    "en-US": "Designer, codeRunner, daemon, wenterprise-server",
    "ru-RU": "Designer, codeRunner, daemon, wenterprise-server",
    "uk-UA": "Designer, codeRunner, daemon, wenterprise-server",
}


def slugify(name_en: str) -> str:
    """Normalise a registry identifier into an id-grammar-safe segment.

    Strips a leading `#` (preprocessor directives like `#Define`,
    `#Ifdef`) since the id grammar in design v5 §2.2 is ASCII-alpha
    + `_` only. The `name_en` field retains the `#` for display so the
    UI still shows users the actual identifier they would type.
    Raises if anything else slips past — keeps the corpus from
    silently growing into a grammar the loader's resolver can't handle.
    """
    cleaned = name_en.lstrip("#")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", cleaned):
        raise ValueError(
            f"Registry identifier {name_en!r} is not a valid id segment "
            "— update the id-grammar handling in help-skeleton.py before "
            "regenerating."
        )
    return cleaned


def keyword_id(name_en: str) -> str:
    # Preprocessor directives need a separate id prefix — `#Else` /
    # `#Endif` would collide with the plain `Else` / `Endif` keyword
    # ids otherwise. Both kinds share the `keyword` ibHelpKind in the
    # corpus; the id prefix just distinguishes them so the resolver can
    # disambiguate. `name_en` retains the `#` for display either way.
    if name_en.startswith("#"):
        return f"kw.Pp{slugify(name_en)}"
    return f"kw.{slugify(name_en)}"


def function_id(name_en: str, is_procedure: bool) -> str:
    # Per design §2.2 the prefix is `fn.` for functions and procedures
    # alike — the kind enum already distinguishes them; doubling up
    # via a separate prefix would balloon the id space without helping
    # disambiguation.
    return f"fn.{slugify(name_en)}"


def make_keyword_entry(name_en: str, short_description: str) -> dict:
    return {
        "id":             keyword_id(name_en),
        "name_local":     name_en,
        "name_en":        name_en,
        "kind":           "keyword",
        "category_keys":  KIND_CATEGORIES["keyword"],
        "signature":      "",
        "description":    short_description,
        "syntax_block":   "",
        "parameters":     "",
        "return_descr":   "",
        "example":        "",
        "availability":   "",
        "see_also":       [],
        "reviewed":       False,
    }


def make_function_entry(name_en: str, signature: str,
                         param_count: int, is_procedure: bool) -> dict:
    kind = "system_procedure" if is_procedure else "system_function"
    return {
        "id":             function_id(name_en, is_procedure),
        "name_local":     name_en,
        "name_en":        name_en,
        "kind":           kind,
        "category_keys":  KIND_CATEGORIES[kind],
        "signature":      signature,
        "description":    "",
        "syntax_block":   signature,
        "parameters":     "",
        "return_descr":   "" if is_procedure else "",
        "example":        "",
        "availability":   "",
        "see_also":       [],
        "reviewed":       False,
    }


def load_existing_bucket(path: Path) -> dict:
    if not path.exists():
        return None
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"warn: could not read {path}: {exc}", file=sys.stderr)
        return None


def write_bucket(path: Path, locale: str, entries: list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    doc = {
        "format":         FORMAT_NAME,
        "schema_version": SCHEMA_VERSION,
        "locale":         locale,
        "entries":        entries,
    }
    with path.open("w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)
        f.write("\n")


def merge_entries(existing: list, new_entries: list,
                  overwrite: bool) -> tuple[list, int, int]:
    """Merge `new_entries` into `existing`, keyed by id.

    Returns (merged, added_count, kept_count). Existing entries are
    preserved verbatim unless --overwrite is set, in which case the
    generated stub replaces them (drops LLM/human prose — only useful
    for a forced regeneration).
    """
    by_id = {e["id"]: e for e in existing}
    added = 0
    kept = 0
    for new in new_entries:
        if new["id"] in by_id and not overwrite:
            kept += 1
            continue
        by_id[new["id"]] = new
        if new["id"] not in {e["id"] for e in existing}:
            added += 1
    return list(by_id.values()), added, kept


def emit_bucket(out_dir: Path, locale: str, bucket_name: str,
                new_entries: list, overwrite: bool) -> None:
    locale_dir = out_dir / locale
    path = locale_dir / f"{bucket_name}.json"

    existing_doc = load_existing_bucket(path)
    if existing_doc is None:
        merged, added, kept = new_entries, len(new_entries), 0
    elif overwrite:
        # --overwrite drops stale ids whose generator-side spelling
        # changed (preprocessor `kw.Else` → `kw.PpElse` after the
        # id-grammar fix), but reviewed=true entries are
        # human-authored and id-stable — preserve them and let the
        # skeleton replace only LLM-draft / fresh-stub entries.
        existing_entries  = existing_doc.get("entries", [])
        reviewed_existing = [e for e in existing_entries if e.get("reviewed")]
        merged, added, kept = merge_entries(
            reviewed_existing, new_entries, overwrite=False)
    else:
        existing_entries = existing_doc.get("entries", [])
        merged, added, kept = merge_entries(
            existing_entries, new_entries, overwrite)

    # Locale-default availability for entries that have none.
    for e in merged:
        if not e.get("availability"):
            e["availability"] = DEFAULT_AVAILABILITY.get(locale, "")

    write_bucket(path, locale, merged)
    print(f"  {path.relative_to(out_dir.parent)}: +{added} added, {kept} kept",
          file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", required=True, type=Path,
                        help="Path to JSON produced by dumpHelp.")
    parser.add_argument("--out",  default=Path("data/help"), type=Path,
                        help="Help-corpus root (one subdir per locale).")
    parser.add_argument("--locales", default="en-US,ru-RU,uk-UA",
                        help="Comma-separated list of locale codes to seed.")
    parser.add_argument("--overwrite", action="store_true",
                        help="Replace existing entries with fresh stubs "
                              "(drops LLM/human prose). Default is merge.")
    args = parser.parse_args()

    if not args.dump.exists():
        print(f"error: dump file not found: {args.dump}", file=sys.stderr)
        return 2

    with args.dump.open("r", encoding="utf-8") as f:
        dump = json.load(f)

    if dump.get("format") != "OES-HELP-DUMP-1.0":
        print(f"error: unexpected dump format: {dump.get('format')!r}",
              file=sys.stderr)
        return 2

    keywords = dump.get("keywords", [])
    functions = dump.get("functions", [])
    print(f"dump: {len(keywords)} keywords, {len(functions)} functions",
          file=sys.stderr)

    keyword_entries  = [
        make_keyword_entry(k["name_en"], k.get("short_description", ""))
        for k in keywords
    ]
    function_entries = [
        make_function_entry(f["name_en"], f.get("signature", ""),
                            f.get("param_count", 0),
                            bool(f.get("is_procedure", False)))
        for f in functions
    ]

    for locale in [s.strip() for s in args.locales.split(",") if s.strip()]:
        print(f"locale {locale}:", file=sys.stderr)
        emit_bucket(args.out, locale, "keywords",
                     keyword_entries, args.overwrite)
        emit_bucket(args.out, locale, "global_functions",
                     function_entries, args.overwrite)

    return 0


if __name__ == "__main__":
    sys.exit(main())
