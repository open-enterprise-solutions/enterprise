#!/usr/bin/env python3

# Vendors Tabulator into webClient/assets/tabulator/<version>/.
#
# Tabulator is the grid engine every OES table is drawn with (ADR-003).
# It is fetched HERE, once, and served from our own origin afterwards:
# the client loads nothing from a CDN at run time, and there is no
# bundler in the loop — the ESM build is a single self-contained module
# and the stylesheet is a single file.
#
# Re-running this is expected to reproduce the same bytes; the manifest
# it writes is what says so.

import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

VERSION = "6.5.2"
PACKAGE = "tabulator-tables"

# Two files, and the reason each is here:
#   the ESM build so the client imports it like any other module, and
#   the unstyled base CSS — the look comes from our own tokens, not from
#   one of Tabulator's shipped themes.
FILES = (
    ("dist/js/tabulator_esm.js", "tabulator.js"),
    ("dist/css/tabulator.min.css", "tabulator.css"),
)

SOURCE_MAP_PATTERN = re.compile(
    r"(?://[#@]\s*sourceMappingURL=[^\r\n]*|/\*[#@]\s*sourceMappingURL=.*?\*/)",
    re.DOTALL,
)

root = Path(__file__).resolve().parents[1]
destination = root / "tabulator" / VERSION

with tempfile.TemporaryDirectory() as workspace:
    work = Path(workspace)
    subprocess.run(
        ["npm", "install", "--no-audit", "--no-fund", "--loglevel", "error",
         f"{PACKAGE}@{VERSION}"],
        cwd=work, check=True,
    )
    installed = work / "node_modules" / PACKAGE
    manifest = json.loads((installed / "package.json").read_text())
    if manifest.get("version") != VERSION:
        raise RuntimeError(
            f"npm returned {PACKAGE}@{manifest.get('version')}, expected {VERSION}")

    staged = work / "staged"
    staged.mkdir()
    checksums = {}
    for source, name in FILES:
        text = (installed / source).read_text()
        # A source-map comment is a request for a file we do not ship;
        # the browser asks for it and gets a 404 on every load.
        text = SOURCE_MAP_PATTERN.sub("", text).rstrip() + "\n"
        (staged / name).write_text(text)
        checksums[name] = hashlib.sha256(text.encode()).hexdigest()

    licence = installed / "LICENSE"
    if not licence.is_file():
        raise RuntimeError("tabulator-tables shipped no LICENSE file")
    (staged / "LICENSE.txt").write_text(
        f"{PACKAGE} {VERSION} — {manifest.get('license', 'see below')}\n"
        f"{manifest.get('homepage', '')}\n\n" + licence.read_text())

    (staged / "manifest.json").write_text(json.dumps({
        "package": PACKAGE,
        "version": VERSION,
        "license": manifest.get("license"),
        "sha256": checksums,
    }, indent=2, sort_keys=True) + "\n")

    shutil.rmtree(destination, ignore_errors=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(staged, destination)

print(f"Vendored {PACKAGE}@{VERSION} in {destination}")
for name in sorted(checksums):
    size = (destination / name).stat().st_size
    print(f"  {name}: {size:,} bytes  sha256:{checksums[name][:16]}…")
