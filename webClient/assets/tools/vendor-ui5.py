#!/usr/bin/env python3

import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from collections import defaultdict, deque
from pathlib import Path


VERSION = "2.26.0"
ROOT_PACKAGE = "@ui5/webcomponents"
# Installed alongside the root. The graph walk only ever *finds* packages
# npm has already put on disk, and webcomponents-fiori is a sibling of the
# root rather than a dependency of it -- the chrome (shell bar, side
# navigation) lives there.
COMPANION_PACKAGES = ("@ui5/webcomponents-fiori",)
ENTRYPOINTS = (
    "@ui5/webcomponents/dist/Assets.js",
    # Form controls.
    "@ui5/webcomponents/dist/Button.js",
    "@ui5/webcomponents/dist/Input.js",
    "@ui5/webcomponents/dist/CheckBox.js",
    # Chrome: the command bar, the tab strip, the window's own bars.
    # ui5-bar takes arbitrary slotted content, which ui5-toolbar does not
    # -- a command carries a raster icon out of the metadata, and only a
    # free slot can hold one.
    "@ui5/webcomponents/dist/Bar.js",
    "@ui5/webcomponents/dist/Toolbar.js",
    "@ui5/webcomponents/dist/ToolbarButton.js",
    "@ui5/webcomponents/dist/ToolbarSeparator.js",
    "@ui5/webcomponents/dist/ToolbarSpacer.js",
    "@ui5/webcomponents/dist/TabContainer.js",
    "@ui5/webcomponents/dist/Tab.js",
    "@ui5/webcomponents/dist/TabSeparator.js",
    "@ui5/webcomponents/dist/Title.js",
    "@ui5/webcomponents/dist/Label.js",
    "@ui5/webcomponents/dist/Panel.js",
    # Assets.js is what REGISTERS a package's own theme parameters. The
    # graph walk cannot find it from a component: the component asks for
    # its bundle by name at run time, so without this entry the fiori
    # parameters are simply absent and its components fall back to the
    # base ones.
    "@ui5/webcomponents-fiori/dist/Assets.js",
    "@ui5/webcomponents-fiori/dist/ShellBar.js",
    "@ui5/webcomponents-fiori/dist/ShellBarItem.js",
    "@ui5/webcomponents-fiori/dist/SideNavigation.js",
    "@ui5/webcomponents-fiori/dist/SideNavigationItem.js",
    "@ui5/webcomponents-icons/dist/value-help.js",
    "@ui5/webcomponents-icons/dist/decline.js",
    "@ui5/webcomponents-icons/dist/slim-arrow-down.js",
    "@ui5/webcomponents-icons/dist/overflow.js",
    "@ui5/webcomponents-icons/dist/menu2.js",
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
JSON_DYNAMIC_IMPORT_PATTERN = re.compile(
    r"(?P<prefix>\bimport\s*\(\s*(?:/\*.*?\*/\s*)?)"
    r"(?P<quote>[\"'])(?P<specifier>[^\"']+\.json)(?P=quote)",
    re.DOTALL,
)
DISALLOWED_SUFFIXES = (".d.ts", ".ts", ".map")


def relative_module_url(module_path: Path, resource_path: Path) -> str:
    relative = Path(os.path.relpath(resource_path, module_path.parent)).as_posix()
    return relative if relative.startswith(".") else f"./{relative}"


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


def local_module_target(staged: Path, module_path: Path, specifier: str) -> Path:
    if not specifier.startswith("."):
        raise RuntimeError(
            f"JSON module import is not package-relative: {module_path}: {specifier}"
        )
    target = (module_path.parent / specifier).resolve()
    staged_resolved = staged.resolve()
    if target != staged_resolved and staged_resolved not in target.parents:
        raise RuntimeError(f"JSON module import escapes staged tree: {module_path}: {specifier}")
    return target


def convert_json_module_imports(staged: Path) -> None:
    # UI5's generated JSON imports expect a bundler. Browser-native modules
    # need JavaScript siblings instead; JSON retained for new URL resources
    # still serves fetch callers with its original MIME type.
    resource_targets = set()
    javascript_paths = sorted(staged.rglob("*.js"))
    for module_path in javascript_paths:
        source = module_path.read_text()
        for resource in module_resources(source):
            if resource.endswith(".json"):
                resource_targets.add(local_module_target(staged, module_path, resource))

    converted_targets = set()
    for module_path in javascript_paths:
        source = module_path.read_text()

        def replace_json_import(match):
            specifier = match.group("specifier")
            target = local_module_target(staged, module_path, specifier)
            if not target.is_file():
                raise RuntimeError(f"JSON module target is absent: {module_path}: {specifier}")
            converted_targets.add(target)
            rewritten = specifier[: -len(".json")] + ".js"
            return match.group("prefix") + match.group("quote") + rewritten + match.group("quote")

        rewritten = JSON_DYNAMIC_IMPORT_PATTERN.sub(replace_json_import, source)
        if rewritten != source:
            module_path.write_text(rewritten)

    for json_path in sorted(converted_targets):
        payload = json.loads(json_path.read_text())
        module_path = json_path.with_suffix(".js")
        module_path.write_text(
            "export default " + json.dumps(payload, separators=(",", ":")) + ";\n"
        )
        if json_path not in resource_targets:
            json_path.unlink()

    surviving_imports = []
    for module_path in sorted(staged.rglob("*.js")):
        for specifier in module_specifiers(module_path.read_text()):
            if specifier.endswith(".json"):
                surviving_imports.append(f"{module_path}: {specifier}")
    if surviving_imports:
        raise RuntimeError(
            "JSON module imports remain after conversion: "
            + ", ".join(surviving_imports)
        )


asset_root = Path(__file__).resolve().parents[1]
repo_root  = Path(__file__).resolve().parents[3]
ui5_root = asset_root / "ui5"
smoke_page = asset_root / "smoke.html"
client_page = repo_root / "webClient" / "client.html"


def tree_digest(root: Path) -> str:
    # Every file's path and bytes, in one order. The served directory is
    # named after this, because `immutable` is a promise that the bytes at
    # a URL never change -- and re-vendoring the same VERSION with a
    # different tree breaks it. A browser that cached the old
    # json-imports/Themes.js kept importing a parameters bundle this tree
    # no longer has, for the whole year the header promised, and the theme
    # never loaded. Found in Firefox on 2026-09-07; a fresh browser could
    # not see it, which is the shape of every cache bug.
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()[:12]

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
            *(f"{name}@{VERSION}" for name in COMPANION_PACKAGES),
        ],
        check=True,
    )

    installed_root = scratch / "node_modules" / ROOT_PACKAGE
    root_manifest = json.loads((installed_root / "package.json").read_text())
    if root_manifest.get("version") != VERSION:
        raise RuntimeError(
            f"npm returned {ROOT_PACKAGE}@{root_manifest.get('version')}"
        )
    for name in COMPANION_PACKAGES:
        manifest_path = scratch / "node_modules" / name / "package.json"
        if not manifest_path.is_file():
            raise RuntimeError(f"npm did not install {name}")
        companion_version = json.loads(manifest_path.read_text()).get("version")
        if companion_version != VERSION:
            raise RuntimeError(f"npm returned {name}@{companion_version}")

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
        rewritten_module_resources = set()

        cldr_target = (
            staged
            / "@ui5/webcomponents-localization"
            / "dist/generated/assets/cldr/en.json"
        )
        if OPENUI5_CLDR in source:
            local_cldr = relative_module_url(destination_path, cldr_target)
            quoted_cldr = re.compile(
                rf'(?P<quote>["\'`]){re.escape(OPENUI5_CLDR)}(?P=quote)'
            )
            source, replacement_count = quoted_cldr.subn(
                lambda _match: f"new URL({json.dumps(local_cldr)}, import.meta.url).href",
                source,
            )
            if replacement_count != 1:
                raise RuntimeError("could not rewrite the fallback CLDR module URL")
            rewritten_module_resources.add(local_cldr)
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
            resource_target = staged / dependency_name / dependency_path
            local_url = relative_module_url(destination_path, resource_target)
            if relative.as_posix() != "dist/generated/css/FontFace.css.js":
                raise RuntimeError(
                    f"unhandled CDN runtime URL context in {name}/{relative}"
                )
            module_expression = (
                "${new URL("
                + json.dumps(local_url)
                + ", import.meta.url).href}"
            )
            source = source.replace(match.group(0), module_expression)

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
            if resource in rewritten_module_resources:
                continue
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

    convert_json_module_imports(staged)
    for package_name_with_themes in (
        "@ui5/webcomponents-theming",
        "@ui5/webcomponents",
    ):
        for required_theme in ("sap_horizon", "sap_horizon_dark"):
            required_bundle = (
                staged
                / package_name_with_themes
                / "dist/generated/assets/themes"
                / required_theme
                / "parameters-bundle.css.js"
            )
            if not required_bundle.is_file():
                raise RuntimeError(f"required theme bundle was pruned: {required_bundle}")

    write_consolidated_licenses(staged, packages)

    # Everything staged is final now, so the tree can be named after its
    # own contents.
    build_id = f"{VERSION}-{tree_digest(staged)}"
    asset_prefix = f"./assets/ui5/{build_id}"
    destination = ui5_root / build_id

    imports = {}
    for name in sorted(packages):
        imports[f"{name}/"] = f"{asset_prefix}/{name}/"

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
            imports[specifier] = f"{asset_prefix}/{name}/{relative}"

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
    document_relative_urls = [
        path
        for path in staged.rglob("*.js")
        if "./assets/ui5/" in path.read_text(errors="ignore")
    ]
    if document_relative_urls:
        raise RuntimeError(
            "document-relative runtime URLs remain: "
            + ", ".join(map(str, document_relative_urls))
        )
    for dropped in (
        "@ui5/webcomponents-icons-tnt",
        "@ui5/webcomponents-icons-business-suite",
    ):
        if (staged / dropped).exists():
            raise RuntimeError(f"unused icon package was staged: {dropped}")

    # Both pages that carry an import map are repointed at the new
    # directory. A prefix substitution rather than a whole-map rewrite:
    # the client's map has entries of its own (tabulator), and they are
    # none of this script's business.
    prefix_pattern = re.compile(r"\./assets/ui5/[^/\"']+/")
    for page in (smoke_page, client_page):
        if not page.is_file():
            raise RuntimeError(f"page with an import map is missing: {page}")
        text = page.read_text()
        patched, count = prefix_pattern.subn(f"{asset_prefix}/", text)
        if count == 0:
            raise RuntimeError(f"no asset prefix to repoint in {page}")
        page.write_text(patched)

    # One tree is served, and it is this one. An older build left in place
    # would be dead weight in the repository and a second answer to the
    # same question.
    for stale in sorted(ui5_root.glob("*")) if ui5_root.is_dir() else []:
        if stale.is_dir() and stale.name != build_id:
            shutil.rmtree(stale, ignore_errors=True)
    shutil.rmtree(destination, ignore_errors=True)
    ui5_root.mkdir(parents=True, exist_ok=True)
    shutil.copytree(staged, destination)

print(f"Vendored {ROOT_PACKAGE}@{VERSION} in {destination}")
print(f"  build id: {build_id} (content digest — the cache key)")
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
