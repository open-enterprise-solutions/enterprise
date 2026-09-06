#!/usr/bin/env python3

import json
import re
import shutil
import subprocess
import tempfile
from collections import defaultdict, deque
from pathlib import Path


VERSION = "2.26.0"
ROOT_PACKAGE = "@ui5/webcomponents"
ENTRYPOINTS = (
    "@ui5/webcomponents/dist/Assets.js",
    "@ui5/webcomponents/dist/Button.js",
    "@ui5/webcomponents/dist/Input.js",
    "@ui5/webcomponents/dist/CheckBox.js",
    "@ui5/webcomponents-icons/dist/value-help.js",
    "@ui5/webcomponents-icons/dist/decline.js",
    "@ui5/webcomponents-icons/dist/slim-arrow-down.js",
)
# This list tracks the platform's lang/ directory.
LOCALES = ("en", "ru", "uk")

VALID_SPECIFIER_PATTERN = re.compile(
    r"(?:\.{0,2}/[A-Za-z0-9._/-]+|"
    r"@[A-Za-z0-9._-]+/[A-Za-z0-9._-]+(?:/[A-Za-z0-9._/-]+)?|"
    r"[A-Za-z0-9._-]+(?:/[A-Za-z0-9._/-]+)?)"
)
JSDELIVR_PATTERN = re.compile(
    r"https://cdn\.jsdelivr\.net/npm/"
    r"(?P<name>@[^/]+/[^@/]+|[^@/]+)@(?P<version>[^/]+)/"
    r"(?P<path>[^\s\"')`]+)"
)
OPENUI5_CLDR = (
    "https://cdn.jsdelivr.net/npm/@openui5/sap.ui.core@1.120.17/"
    "src/sap/ui/core/cldr/en.json"
)
SOURCE_MAP_PATTERN = re.compile(
    r"(?://[#@]\s*sourceMappingURL=[^\r\n]*|/\*[#@]\s*sourceMappingURL=.*?\*/)",
    re.DOTALL,
)
DISALLOWED_SUFFIXES = (".d.ts", ".ts", ".map")


def package_name(specifier: str) -> str:
    parts = specifier.split("/")
    return "/".join(parts[:2]) if specifier.startswith("@") else parts[0]


def javascript_tokens(source: str):
    tokens = []
    index = 0
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if char.isspace():
            index += 1
            continue
        if char == "/" and following == "/":
            newline = source.find("\n", index + 2)
            index = len(source) if newline < 0 else newline + 1
            continue
        if char == "/" and following == "*":
            end = source.find("*/", index + 2)
            index = len(source) if end < 0 else end + 2
            continue
        if char in ("\"", "'", "`"):
            quote = char
            index += 1
            start = index
            escaped = False
            while index < len(source):
                current = source[index]
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == quote:
                    break
                index += 1
            if quote != "`":
                tokens.append(("string", source[start:index]))
            index += 1
            continue
        if char.isalpha() or char in "_$":
            start = index
            index += 1
            while index < len(source) and (
                source[index].isalnum() or source[index] in "_$"
            ):
                index += 1
            tokens.append(("identifier", source[start:index]))
            continue
        tokens.append(("punctuation", char))
        index += 1
    return tokens


def module_specifiers(source: str):
    tokens = javascript_tokens(source)
    for index, token in enumerate(tokens):
        kind, value = token
        if kind != "identifier":
            continue
        candidate = None
        if value == "from" and index + 1 < len(tokens):
            if tokens[index + 1][0] == "string":
                candidate = tokens[index + 1][1]
        elif value == "import" and index + 1 < len(tokens):
            next_token = tokens[index + 1]
            if next_token[0] == "string":
                candidate = next_token[1]
            elif (
                next_token == ("punctuation", "(")
                and index + 2 < len(tokens)
                and tokens[index + 2][0] == "string"
            ):
                candidate = tokens[index + 2][1]
        if candidate and VALID_SPECIFIER_PATTERN.fullmatch(candidate):
            yield candidate


def literal_call_arguments(source: str, function_names):
    tokens = javascript_tokens(source)
    for index in range(len(tokens) - 2):
        if (
            tokens[index][0] == "identifier"
            and tokens[index][1] in function_names
            and tokens[index + 1] == ("punctuation", "(")
            and tokens[index + 2][0] == "string"
        ):
            yield tokens[index + 2][1]


def module_resources(source: str):
    tokens = javascript_tokens(source)
    signature = (
        ("identifier", "new"),
        ("identifier", "URL"),
        ("punctuation", "("),
    )
    suffix = (
        ("punctuation", ","),
        ("identifier", "import"),
        ("punctuation", "."),
        ("identifier", "meta"),
        ("punctuation", "."),
        ("identifier", "url"),
    )
    for index in range(len(tokens) - 9):
        if (
            tuple(tokens[index : index + 3]) == signature
            and tokens[index + 3][0] == "string"
            and tuple(tokens[index + 4 : index + 10]) == suffix
        ):
            yield tokens[index + 3][1]


def find_installed_package(start: Path, install_root: Path, name: str) -> Path:
    current = start
    while current == install_root or install_root in current.parents:
        candidate = current / "node_modules" / name
        if candidate.is_dir():
            return candidate
        if current == install_root:
            break
        current = current.parent
    raise RuntimeError(f"npm did not install imported package {name}")


def select_export(value):
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        for condition in ("browser", "import", "default"):
            selected = select_export(value.get(condition))
            if selected:
                return selected
    return None


def exported_target(manifest: dict, subpath: str):
    exports = manifest.get("exports")
    export_key = "." if not subpath else f"./{subpath}"
    entry = exports
    if isinstance(exports, dict) and any(key.startswith(".") for key in exports):
        entry = exports.get(export_key)
        if entry is None:
            for key, value in exports.items():
                if "*" not in key:
                    continue
                prefix, suffix = key.split("*", 1)
                if export_key.startswith(prefix) and export_key.endswith(suffix):
                    wildcard = export_key[len(prefix) :]
                    if suffix:
                        wildcard = wildcard[: -len(suffix)]
                    selected = select_export(value)
                    return selected.replace("*", wildcard) if selected else None

    selected = select_export(entry)
    if selected:
        return selected
    if not subpath:
        return manifest.get("module") or manifest.get("main")
    return f"./{subpath}"


def prune_locale_loader(relative: Path, source: str) -> str:
    path = relative.as_posix()
    if "/generated/json-imports/" not in path:
        return source
    if relative.name not in ("i18n.js", "LocaleData.js"):
        return source

    locale_set = set(LOCALES)

    def keep_locale_case(match):
        return match.group(0) if match.group("locale") in locale_set else ""

    source = re.sub(
        r'^\s*case\s+["\'](?P<locale>[^"\']+)["\']:[^\n]*(?:\n|$)',
        keep_locale_case,
        source,
        flags=re.MULTILINE,
    )
    locale_array = json.dumps(list(LOCALES))
    source = re.sub(
        r"const localeIds = \[.*?\];",
        f"const localeIds = {locale_array};",
        source,
        flags=re.DOTALL,
    )
    source = re.sub(
        r"const availableLocales = \[.*?\];",
        f"const availableLocales = {locale_array};",
        source,
        flags=re.DOTALL,
    )
    return source


def prune_asset_parameters(relative: Path, source: str) -> str:
    if relative.as_posix() != "dist/generated/AssetParameters.js":
        return source
    match = re.search(r"const assetParameters = (\{.*\});", source)
    if not match:
        raise RuntimeError("could not read generated asset parameters")
    parameters = json.loads(match.group(1))
    parameters["languages"]["all"] = list(LOCALES)
    parameters["locales"]["all"] = list(LOCALES)
    replacement = json.dumps(parameters, separators=(",", ":"))
    return source[: match.start(1)] + replacement + source[match.end(1) :]


def license_files(package_dir: Path):
    candidates = []
    for pattern in ("LICENSE*", "LICENCE*", "NOTICE*"):
        candidates.extend(path for path in package_dir.glob(pattern) if path.is_file())
    return sorted(set(candidates))


def write_consolidated_licenses(staged: Path, packages: dict) -> None:
    lines = [
        "Third-party licences for vendored UI5 browser assets",
        "====================================================",
        "",
        "Package inventory",
        "-----------------",
    ]
    notices = defaultdict(list)
    for name in sorted(packages):
        info = packages[name]
        manifest = info["manifest"]
        license_name = manifest.get("license", "Not specified")
        lines.append(f"{name}@{info['version']}: {license_name}")
        files = license_files(info["installed"])
        if files:
            text = "\n\n".join(path.read_text(errors="replace").strip() for path in files)
            notices[(license_name, text)].append(name)
        else:
            notices[(license_name, "No licence file was included in the npm package.")].append(
                name
            )

    lines.extend(("", "Licence texts", "-------------"))
    for (license_name, text), names in sorted(
        notices.items(), key=lambda item: (item[0][0], item[1])
    ):
        lines.extend(
            (
                "",
                license_name,
                f"Applies to: {', '.join(sorted(names))}",
                "",
                text,
            )
        )
    (staged / "THIRD_PARTY_LICENSES.txt").write_text("\n".join(lines).rstrip() + "\n")


asset_root = Path(__file__).resolve().parents[1]
ui5_root = asset_root / "ui5"
destination = ui5_root / VERSION
smoke_page = asset_root / "smoke.html"

with tempfile.TemporaryDirectory(prefix="oes-ui5-", dir="/private/tmp") as temp:
    scratch = Path(temp)
    subprocess.run(
        [
            "npm",
            "install",
            "--prefix",
            temp,
            "--ignore-scripts",
            "--no-audit",
            "--no-fund",
            f"{ROOT_PACKAGE}@{VERSION}",
        ],
        check=True,
    )

    installed_root = scratch / "node_modules" / ROOT_PACKAGE
    root_manifest = json.loads((installed_root / "package.json").read_text())
    if root_manifest.get("version") != VERSION:
        raise RuntimeError(
            f"npm returned {ROOT_PACKAGE}@{root_manifest.get('version')}"
        )

    staged = scratch / "staged" / VERSION
    queue = deque()
    seen = set()
    packages = {}
    bare_specifiers = set()

    def record_package(name: str, installed: Path):
        manifest = json.loads((installed / "package.json").read_text())
        if manifest.get("name") != name:
            raise RuntimeError(f"expected {name}, found {manifest.get('name')}")
        existing = packages.get(name)
        if existing and existing["version"] != manifest.get("version"):
            raise RuntimeError(f"multiple installed versions of {name}")
        packages[name] = {
            "version": manifest.get("version"),
            "manifest": manifest,
            "installed": installed,
        }
        return manifest

    def enqueue(name: str, installed: Path, relative):
        relative = Path(str(relative).removeprefix("./"))
        source = (installed / relative).resolve()
        installed_resolved = installed.resolve()
        if source != installed_resolved and installed_resolved not in source.parents:
            raise RuntimeError(f"asset escapes npm package: {name}/{relative}")
        if not source.is_file():
            raise RuntimeError(f"browser asset is absent: {name}/{relative}")
        relative = source.relative_to(installed_resolved)
        if relative.name in ("package.json", "package-lock.json"):
            raise RuntimeError(f"browser graph requested package metadata: {name}/{relative}")
        if relative.as_posix().endswith(DISALLOWED_SUFFIXES):
            raise RuntimeError(f"browser graph requested non-runtime file: {name}/{relative}")
        record_package(name, installed)
        queue.append((name, installed, relative))

    def resolve_bare(specifier: str, start: Path):
        name = package_name(specifier)
        installed = find_installed_package(start, scratch, name)
        manifest = record_package(name, installed)
        subpath = "" if specifier == name else specifier[len(name) + 1 :]
        target = exported_target(manifest, subpath)
        if not target:
            raise RuntimeError(f"cannot resolve bare import {specifier}")
        return name, installed, target.removeprefix("./")

    for specifier in ENTRYPOINTS:
        name, installed, relative = resolve_bare(specifier, installed_root)
        bare_specifiers.add(specifier)
        enqueue(name, installed, relative)

    while queue:
        name, installed, relative = queue.popleft()
        source_path = (installed / relative).resolve()
        if source_path in seen:
            continue
        seen.add(source_path)

        destination_path = staged / name / relative
        destination_path.parent.mkdir(parents=True, exist_ok=True)

        if source_path.suffix not in (".js", ".mjs"):
            shutil.copy2(source_path, destination_path)
            continue

        source = source_path.read_text()
        source = prune_locale_loader(relative, source)
        source = prune_asset_parameters(relative, source)
        source = SOURCE_MAP_PATTERN.sub("", source).rstrip() + "\n"

        local_cldr = (
            f"./assets/ui5/{VERSION}/@ui5/webcomponents-localization/"
            "dist/generated/assets/cldr/en.json"
        )
        if OPENUI5_CLDR in source:
            source = source.replace(OPENUI5_CLDR, local_cldr)
            localization = find_installed_package(
                installed, scratch, "@ui5/webcomponents-localization"
            )
            enqueue(
                "@ui5/webcomponents-localization",
                localization,
                "dist/generated/assets/cldr/en.json",
            )

        for match in list(JSDELIVR_PATTERN.finditer(source)):
            dependency_name = match.group("name")
            dependency = find_installed_package(installed, scratch, dependency_name)
            manifest = record_package(dependency_name, dependency)
            if manifest.get("version") != match.group("version"):
                raise RuntimeError(
                    f"CDN asset requests {dependency_name}@{match.group('version')}, "
                    f"npm installed {manifest.get('version')}"
                )
            dependency_path = match.group("path")
            enqueue(dependency_name, dependency, dependency_path)
            local_url = (
                f"./assets/ui5/{VERSION}/{dependency_name}/{dependency_path}"
            )
            source = source.replace(match.group(0), local_url)

        destination_path.write_text(source)

        for specifier in module_specifiers(source):
            if specifier.startswith((".", "/", "#")):
                relative_target = (relative.parent / specifier).as_posix()
                enqueue(name, installed, relative_target)
                continue
            if specifier.startswith("node:"):
                raise RuntimeError(f"browser graph reached platform import {specifier}")
            if ":" in specifier.split("/", 1)[0]:
                raise RuntimeError(f"browser graph reached external import {specifier}")
            imported_name, imported, imported_relative = resolve_bare(specifier, installed)
            bare_specifiers.add(specifier)
            enqueue(imported_name, imported, imported_relative)

        for resource in module_resources(source):
            if resource.startswith((".", "/")):
                enqueue(name, installed, relative.parent / resource)

        for icon_name in literal_call_arguments(
            source, {"getIconData", "getIconDataSync"}
        ):
            if "/" in icon_name:
                continue
            icon_specifier = f"@ui5/webcomponents-icons/dist/{icon_name}.js"
            icon_package, icon_installed, icon_relative = resolve_bare(
                icon_specifier, installed
            )
            bare_specifiers.add(icon_specifier)
            enqueue(icon_package, icon_installed, icon_relative)

    write_consolidated_licenses(staged, packages)

    imports = {}
    for name in sorted(packages):
        imports[f"{name}/"] = f"./assets/ui5/{VERSION}/{name}/"

    for specifier in sorted(bare_specifiers):
        name = package_name(specifier)
        info = packages[name]
        subpath = "" if specifier == name else specifier[len(name) + 1 :]
        target = exported_target(info["manifest"], subpath)
        if not target:
            raise RuntimeError(f"cannot map bare import {specifier}")
        relative = target.removeprefix("./")
        if not (staged / name / relative).is_file():
            raise RuntimeError(f"vendored import has no target: {specifier} -> {relative}")
        if not subpath or relative != subpath:
            imports[specifier] = f"./assets/ui5/{VERSION}/{name}/{relative}"

    import_map = json.dumps({"imports": imports}, indent=2) + "\n"
    (staged / "importmap.json").write_text(import_map)

    forbidden = [
        path
        for path in staged.rglob("*")
        if path.is_file()
        and (
            path.as_posix().endswith(DISALLOWED_SUFFIXES)
            or path.name in ("package.json", "package-lock.json")
            or path.name.startswith(("README", "LICENSE", "LICENCE"))
        )
    ]
    if forbidden:
        raise RuntimeError("non-runtime files were staged: " + ", ".join(map(str, forbidden)))
    if list(staged.rglob("*.map")):
        raise RuntimeError("source maps were staged")
    if any("sourceMappingURL" in path.read_text(errors="ignore") for path in staged.rglob("*.js")):
        raise RuntimeError("source-map requests remain in staged JavaScript")
    for dropped in (
        "@ui5/webcomponents-icons-tnt",
        "@ui5/webcomponents-icons-business-suite",
    ):
        if (staged / dropped).exists():
            raise RuntimeError(f"unused icon package was staged: {dropped}")

    smoke = smoke_page.read_text()
    indented_map = "\n".join(f"  {line}" for line in import_map.rstrip().splitlines())
    replacement = f'<script type="importmap">\n{indented_map}\n  </script>'
    smoke, replacements = re.subn(
        r'<script type="importmap">.*?</script>', replacement, smoke, flags=re.DOTALL
    )
    if replacements != 1:
        raise RuntimeError("smoke.html must contain exactly one import map")

    shutil.rmtree(destination, ignore_errors=True)
    ui5_root.mkdir(parents=True, exist_ok=True)
    shutil.copytree(staged, destination)
    smoke_page.write_text(smoke)

print(f"Vendored {ROOT_PACKAGE}@{VERSION} in {destination}")
for name in sorted(packages):
    package_files = sum(1 for path in (destination / name).rglob("*") if path.is_file())
    print(f"  {name}@{packages[name]['version']}: {package_files} files")
icons = sorted(
    path.stem
    for path in (destination / "@ui5/webcomponents-icons/dist").glob("*.js")
    if (destination / "@ui5/webcomponents-icons/dist/v4" / path.name).is_file()
    and (destination / "@ui5/webcomponents-icons/dist/v5" / path.name).is_file()
)
print("  locales: " + ", ".join(LOCALES))
print("  icons: " + ", ".join(icons))
