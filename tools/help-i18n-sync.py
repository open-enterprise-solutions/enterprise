#!/usr/bin/env python3
"""
Sync help-related UI strings into the canonical gettext catalogs and
compile them to .mo files at the path the runtime actually loads
(`lang/<lang>/open_es.mo`).

Why this exists: the previous workflow shipped two parallel catalog
locations — `locale/{ru,uk}.po` (newer) and `lang/<lang>/open_es.po`
(older, but the only one wxLocale's catalog lookup hits at runtime).
The drift meant new translations landed in `locale/` and never reached
the user. This script unifies them: `locale/{ru,uk}.po` are the source
of truth, this script compiles them to `lang/<lang>/open_es.mo`, and a
CMake POST_BUILD step keeps the build tree's binaries pointed at the
same compiled catalogs.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO   = Path(__file__).resolve().parents[1]
LOCALE = REPO / "locale"
LANG   = REPO / "lang"

# Strings the help UI surface wraps in _(...). Each value is a tuple
# (ru, uk). Adding a new _() in C++ here means adding one entry to
# this table, re-running this script, and recompiling.
STRINGS: dict[str, tuple[str, str]] = {
    "Parameters":    ("Параметры",     "Параметри"),
    "Returns":       ("Возвращает",    "Повертає"),
    "Example":       ("Пример",        "Приклад"),
    "Availability":  ("Доступность",   "Доступність"),
    "See also":      ("См. также",     "Див. також"),

    # Menu items / sidebar caption.
    "Syntax Helper":         ("Синтакс-помощник",          "Синтаксис-помічник"),
    "Show Syntax Helper":    ("Показать Синтакс-помощник", "Показати Синтаксис-помічник"),
    "Look up in Syntax Helper":
        ("Искать в Синтакс-помощнике", "Шукати в Синтаксис-помічнику"),

    # Notebook tabs + dialog labels.
    "Contents": ("Содержание",     "Зміст"),
    "Index":    ("Указатель",      "Покажчик"),
    "Search":   ("Поиск",          "Пошук"),

    "Select topic":               ("Выбор темы",            "Вибір теми"),
    "Select a topic from the list:":
        ("Выберите тему из списка:", "Виберіть тему зі списку:"),

    # Search results badge.
    "Found: %zu":                  ("Найдено: %zu",        "Знайдено: %zu"),
    "Found: 0 (enter a query)":    ("Найдено: 0 (введите запрос)",
                                    "Знайдено: 0 (введіть запит)"),

    # Empty-state hint in the detail pane.
    "Select an entry in the tree on the left, or press Ctrl+F1 on an identifier in the editor.":
        ("Выберите статью в дереве слева или нажмите Ctrl+F1 "
         "на идентификаторе в редакторе.",
         "Виберіть статтю в дереві ліворуч або натисніть Ctrl+F1 "
         "на ідентифікаторі в редакторі."),

    "Draft — this entry awaits editorial review.":
        ("Черновик — статья ожидает редакторской проверки.",
         "Чернетка — стаття очікує редакторської перевірки."),

    # About dialog — header, subtitle, copyright, info-block labels,
    # contributors text. Wrapped in _() so the active locale catalog
    # supplies the translation at runtime.
    "Open Enterprise Solutions, build %i":
        ("Open Enterprise Solutions, сборка %i",
         "Open Enterprise Solutions, збірка %i"),
    "a RAD tool powered by wxWidgets framework":
        ("RAD-инструмент на базе фреймворка wxWidgets",
         "RAD-інструмент на базі фреймворка wxWidgets"),
    "(c) 2026 OES community":
        ("(c) 2026 сообщество OES", "(c) 2026 спільнота OES"),
    "Info":         ("Информация",    "Інформація"),
    "Application":  ("Приложение",    "Застосунок"),
    "User":         ("Пользователь",  "Користувач"),
    "Locale":       ("Локаль",        "Локаль"),
    "Plugins":      ("Плагины",       "Плагіни"),
    "Thanks":       ("Благодарности", "Подяки"),
    "wxWidgets and wxFormBuilder, Unknown Worlds Entertainment team":
        ("wxWidgets и wxFormBuilder, команда Unknown Worlds Entertainment",
         "wxWidgets та wxFormBuilder, команда Unknown Worlds Entertainment"),
    "2C team, whose ideas were taken as the basis for building the interpreter":
        ("Команда 2C — идеи легли в основу интерпретатора",
         "Команда 2C — ідеї лягли в основу інтерпретатора"),
    "Tomasz Sowa who developed ttmath":
        ("Tomasz Sowa, разработчик ttmath",
         "Tomasz Sowa, розробник ttmath"),
    "And also everyone who was not mentioned here":
        ("И всем, кто не был здесь упомянут",
         "Та усім, кого тут не згадали"),
}


def parse_po(path: Path) -> tuple[list[str], set[str]]:
    """Return raw lines plus the set of msgids already present."""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    msgids: set[str] = set()
    # Match the FIRST msgid on each block. Single-line form only —
    # multi-line msgids are rare in this codebase and don't matter for
    # the additive case here.
    pat = re.compile(r'^msgid\s+"(.*)"\s*$')
    for line in lines:
        m = pat.match(line)
        if m:
            msgids.add(m.group(1).replace('\\"', '"'))
    return lines, msgids


def escape_po(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def append_entries(po_path: Path, locale: str) -> int:
    lines, present = parse_po(po_path)
    added = 0
    new_block: list[str] = []
    for english, (ru, uk) in STRINGS.items():
        if english in present:
            continue
        translation = ru if locale == "ru" else uk
        new_block.append("\n")
        new_block.append("#: src/engine/frontend/help/ (syntax helper UI)\n")
        new_block.append(f'msgid "{escape_po(english)}"\n')
        new_block.append(f'msgstr "{escape_po(translation)}"\n')
        added += 1
    if added == 0:
        return 0
    with po_path.open("a", encoding="utf-8") as f:
        f.writelines(new_block)
    return added


def append_pot() -> int:
    pot = LOCALE / "open_es.pot"
    lines, present = parse_po(pot)
    added = 0
    new_block: list[str] = []
    for english in STRINGS.keys():
        if english in present:
            continue
        new_block.append("\n")
        new_block.append("#: src/engine/frontend/help/ (syntax helper UI)\n")
        new_block.append(f'msgid "{escape_po(english)}"\n')
        new_block.append('msgstr ""\n')
        added += 1
    if added:
        with pot.open("a", encoding="utf-8") as f:
            f.writelines(new_block)
    return added


def compile_mo(locale: str) -> Path:
    """Compile locale/<locale>.po into lang/<locale>/open_es.mo."""
    src = LOCALE / f"{locale}.po"
    out_dir = LANG / locale
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "open_es.mo"
    result = subprocess.run(
        ["msgfmt", "--output-file", str(out), str(src)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"msgfmt failed for {locale}: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-append", action="store_true",
                         help="skip adding strings to .po, only recompile .mo")
    args = parser.parse_args()

    if not args.no_append:
        pot_added = append_pot()
        ru_added = append_entries(LOCALE / "ru.po", "ru")
        uk_added = append_entries(LOCALE / "uk.po", "uk")
        print(f"pot: +{pot_added}  ru: +{ru_added}  uk: +{uk_added}")

    ru_mo = compile_mo("ru")
    uk_mo = compile_mo("uk")
    print(f"compiled: {ru_mo}")
    print(f"compiled: {uk_mo}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
