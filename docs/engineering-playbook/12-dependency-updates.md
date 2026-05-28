# 12. Dependency Updates

## Why update

- **Security** — library vulnerabilities are patched (OpenSSL CVEs are a real threat)
- **Compatibility** — outdated libraries stop working with new OSes/compilers
- **Performance** — new versions are often faster
- **Features** — new wxWidgets capabilities, fixed bugs
- **Support** — EOL versions don't receive security fixes

The longer you delay an update, the more painful it gets. Regular small updates beat one massive one once a year.

---

## Cadence

| Action | Frequency | Who |
|----------|---------|-----|
| Check CVEs for libraries in use | Weekly (or in CI) | Automated / developer |
| Check for new dependency versions | Every 2 weeks | Developer |
| Patch updates (bug fixes) | Monthly | Developer |
| Minor updates (new features) | Quarterly | Developer |
| Major updates (breaking changes) | As needed | Tech lead + developer |
| Update Visual Studio / compiler | When a security update is released | Tech lead |
| Update Windows SDK | As needed | Tech lead |

---

## OES dependencies

### Key libraries

| Library | Current role | How to update |
|------------|-------------------|---------------|
| **wxWidgets** | GUI framework | Manually, see below |
| **IBPP** | Firebird C++ API | Manually from the IBPP repository |
| **OpenSSL** | Cryptography | vcpkg or manually |
| **zlib** | Compression | vcpkg or manually |
| **libpq** | PostgreSQL client | vcpkg or along with PostgreSQL |
| **SQLite** | Embedded DB | amalgamation file from sqlite.org |
| **Google Test** | Testing | vcpkg or FetchContent |
| **nlohmann/json** | JSON (if used) | vcpkg or single header |

### Where dependencies live

```
third-party/
├── IBPP/           — Firebird C++ API (sources, built ourselves)
├── wxWidgets/      — wxWidgets (symlink or submodule)
└── sqlite/         — SQLite amalgamation (sqlite3.c + sqlite3.h)

# Through vcpkg (recommended for new dependencies)
vcpkg.json          — Dependency manifest (if we've moved to vcpkg)
```

---

## Verification tools

### OSV Scanner — CVE checks

```bash
# Install (Go required)
go install github.com/google/osv-scanner/cmd/osv-scanner@latest

# Scan dependencies
# NOTE: osv-scanner does not recognize vcpkg.json as a lockfile format.
# Use --sbom with a CycloneDX/SPDX SBOM (generated via vcpkg export --json):
osv-scanner --sbom sbom.json .

# Or scan the whole directory (OSV detects what it can):
osv-scanner .

# Or through GitHub Actions (see below)
```

### vcpkg — dependency management

```bash
# Show installed packages
vcpkg list

# Show available upgrades (without applying)
vcpkg upgrade               # only lists outdated packages

# Apply upgrades (NOTE: actually updates packages)
vcpkg upgrade --no-dry-run  # --no-dry-run APPLIES upgrades, not just lists them!

# Check a specific package
vcpkg search openssl

# Example vcpkg.json (manifest)
{
    "name": "oes",
    "version": "1.2.0",
    "dependencies": [
        { "name": "wxwidgets", "version>=": "3.3.2" },
        { "name": "gtest" },
        { "name": "libpq" },
        { "name": "sqlite3" },
        { "name": "openssl", "version>=": "3.2.0" }
    ]
}
```

### Manual check for new versions

```bash
# wxWidgets
# https://github.com/wxWidgets/wxWidgets/releases

# IBPP (Firebird C++ API)
# https://github.com/FirebirdSQL/ibpp/releases

# OpenSSL
# https://openssl-library.org/news/changelog.html

# SQLite
# https://sqlite.org/changes.html
```

---

## Update process

### Step 1: Create a branch

```bash
git checkout master
git pull origin master
git checkout -b chore/update-deps-2026-03
```

### Step 2: Check for CVE vulnerabilities

```bash
# OSV Scanner
osv-scanner --lockfile vcpkg.json .

# Or check NVD (National Vulnerability Database) manually
# https://nvd.nist.gov/vuln/search
# Search for: wxwidgets, openssl, sqlite, firebird

# Check GitHub Security Advisories for the libraries you use
```

### Step 3: Check what's outdated

```bash
# vcpkg — list outdated packages (without applying)
vcpkg upgrade

# Manually compare versions in third-party/ with GitHub releases:
# wxWidgets:  https://github.com/wxWidgets/wxWidgets/releases
# SQLite:     https://sqlite.org/changes.html
# OpenSSL:    https://github.com/openssl/openssl/releases
```

Classify the updates:

| Update type | Example | Risk | Action |
|----------------|--------|------|----------|
| **Patch** (x.y.Z) | wxWidgets 3.3.2 → 3.3.3 | Low | Update freely |
| **Minor** (x.Y.0) | wxWidgets 3.3.2 → 3.4.0 | Medium | Update, read changelog |
| **Major** (X.0.0) | wxWidgets 3.x → 4.0 | High | Separate task, thorough testing |

### Step 4: Apply patch/minor updates

```bash
# Through vcpkg — apply upgrades
# NOTE: vcpkg install does NOT update already-installed packages;
# use vcpkg upgrade --no-dry-run to update them
vcpkg upgrade --no-dry-run

# Manual — example for updating the SQLite amalgamation
# 1. Download the new version from https://sqlite.org/download.html
# 2. Replace third-party/sqlite/sqlite3.c and sqlite3.h
# 3. Update the VERSION file or the comment inside the file

# Manual — example for updating OpenSSL via vcpkg
vcpkg install openssl:x64-windows

# Make sure everything builds
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# Run tests
ctest --test-dir build -C Debug --output-on-failure
```

### Step 5: Major updates — one at a time

```bash
# Update ONE dependency
# Example: updating wxWidgets to a new minor version

# 1. Download the new wxWidgets release
# https://github.com/wxWidgets/wxWidgets/releases

# 2. Rebuild wxWidgets
cd third-party/wxWidgets-3.4.0
./configure --enable-debug --with-gtk=3  # Linux

# Windows — use MSBuild via the build/msw/ directory:
# (./configure does NOT work on Windows without MSYS2/Cygwin)
# cd build\msw\
# msbuild wx_vc17.sln /p:Configuration=DLL Debug /p:Platform=x64 /m

# 3. Rebuild OES
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# 4. Fix breaking changes (if any)
# Check the wxWidgets Migration Guide

# 5. Run tests
ctest --test-dir build --output-on-failure

# 6. Manual testing — every major feature!
# - Run the app
# - Open several forms
# - Verify the designer
# - Verify Firebird connection

# If everything's good — next dependency
```

**Why one at a time:** if you upgrade everything at once and something breaks — you don't know which library is at fault. One at a time — the cause is obvious immediately.

### Step 6: Open the PR

```bash
git add third-party/sqlite/sqlite3.c third-party/sqlite/sqlite3.h
git add vcpkg.json vcpkg-lock.json
git commit -m "chore: update dependencies (March 2026)"
git push -u origin chore/update-deps-2026-03
```

PR description:

```markdown
## What
Monthly dependency update.

## Updated packages

### Security fixes
- OpenSSL: 3.2.0 → 3.2.1 (CVE-2024-XXXX: buffer overflow in X.509 parsing)

### Minor updates
- SQLite: 3.44.0 → 3.45.1
- Google Test: 1.14.0 → 1.15.0

### Major updates
- None in this PR

## Testing
- [x] msbuild Debug x64 — passes (no warnings)
- [x] msbuild Release x64 — passes
- [x] ctest unit tests — passes
- [x] ctest integration tests — passes
- [ ] Manual testing on staging (will be done after merge to dev)

## Breaking changes
None.
```

---

## Major framework upgrades

Upgrading wxWidgets, Firebird, Visual Studio, or the Windows SDK is **a separate task**, not part of the monthly update.

### Process

1. **Create a task** in the tracker: "Upgrade wxWidgets from 3.3 to 4.0"
2. **Read the changelog and migration guide** — what changed, what breaks
3. **Create a branch** `chore/upgrade-wxwidgets-4`
4. **Migrate** following the official migration guide
5. **Fix all compilation errors and warnings**
6. **Tests** — all must pass
7. **Manual testing** — walk through every major feature
8. **PR** with a detailed description of changes and verification steps
9. **Staging** — deploy and test on staging
10. **Release** — only after full testing

### Example: upgrading wxWidgets 3.3 → 3.4

```bash
# 1. Download wxWidgets 3.4.0
# https://github.com/wxWidgets/wxWidgets/releases

# 2. Read the migration guide:
# https://docs.wxwidgets.org/latest/overview_changes.html

# 3. Update the path in the project
# OES.vcxproj: change WX_DIR = third-party\wxWidgets-3.3.2
#         to:  WX_DIR = third-party\wxWidgets-3.4.0

# 4. Rebuild
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m
# Fix all errors and warnings

# 5. Tests
ctest --test-dir build -C Debug --output-on-failure

# 6. Manual testing (checklist)
# [ ] Main window opens
# [ ] All dialogs open correctly
# [ ] wxGrid works (data tables)
# [ ] Designer works
# [ ] File dialogs work
# [ ] DnD (drag-and-drop) works
# [ ] Printing works
# [ ] Unicode renders correctly
```

### Upgrading the Firebird client (fbclient.dll)

```bash
# When upgrading the Firebird server on customer machines:
# 1. Check fbclient.dll compatibility with IBPP
# 2. Check wire protocol compatibility with the new server version
# 3. Update fbclient.dll in the distribution

# IMPORTANT: fbclient.dll must match the Firebird server version
# Firebird 4.0 server + Firebird 3.0 fbclient.dll — may work, but not guaranteed
```

---

## Pinning critical dependency versions

```json
// vcpkg.json — pin minimum versions for critical libraries
{
    "name": "oes",
    "builtin-baseline": "a14b7c3d...",  // vcpkg commit SHA — required for reproducible builds
    "dependencies": [
        {
            "name": "wxwidgets",
            "version>=": "3.3.2"    // Minimum version
        },
        {
            "name": "openssl",
            "version>=": "3.2.1"    // Minimum with CVE-2024-XXXX fix
        },
        {
            "name": "gtest"         // No pinning — any version works
        }
    ]
}
```

**When to pin an exact version (not `>=`):**
- Known incompatibility with a newer version
- Critical dependence on specific API behaviour
- Production releases (for reproducible builds)

**When `>=` is acceptable:**
- Development tools (Google Test, clang-tidy)
- Utility libraries without ABI-breaking changes

---

## Dependency security

### CVE checks in CI

```yaml
# .github/workflows/security-audit.yml
name: Security Audit

on:
  schedule:
    - cron: '0 9 * * 1'  # Every Monday at 09:00
  push:
    branches: [master, dev]

jobs:
  osv-scan:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: OSV Scanner
        uses: google/osv-scanner-action@v1
        with:
          scan-args: |-
            --lockfile=vcpkg.json
            .

      - name: Check for known vulnerable versions
        run: |
          # Check critical libraries manually
          OPENSSL_VERSION=$(grep '"version"' vcpkg.json | grep openssl | grep -oP '"\d+\.\d+\.\d+"')
          echo "OpenSSL version in vcpkg.json: $OPENSSL_VERSION"
          # Add CVE check logic
```

### When a critical vulnerability is found

1. **Immediately** create a branch `fix/security-deps-CVE-XXXX`
2. Upgrade the vulnerable library to the fixed version
3. `msbuild && ctest`
4. PR → review → merge → new patch release
5. Notify customers about the required upgrade
6. **Don't wait** for the monthly update!

### Verifying digital signatures of downloaded libraries

```powershell
# Windows: verify the signature of a library binary
Get-AuthenticodeSignature "third-party\sqlite\sqlite3.dll"
# Status must be Valid

# For downloaded archives — verify SHA256
# Library sites publish hashes for verification:

# Example: verifying the SQLite amalgamation
$expectedHash = "abc123..."  # Hash from the official site
$actualHash = (Get-FileHash "sqlite-amalgamation-3450100.zip" -Algorithm SHA256).Hash
if ($expectedHash -ne $actualHash) {
    throw "SECURITY: Hash mismatch for SQLite download!"
}
```

---

## Visual Studio and Windows SDK

### Updating Visual Studio

```
When an MSVC security update is released:
1. Help → Check for Updates → Update Visual Studio
2. After the update: rebuild the project
3. Check there are no new compiler warnings
4. Run tests: ctest --output-on-failure
5. Update .github/workflows/ if the toolset version changed:
   msbuild-version: 'latest'  or pin a specific one
```

### Updating the Windows SDK

```xml
<!-- OES.vcxproj — Windows SDK version -->
<WindowsTargetPlatformVersion>10.0.22621.0</WindowsTargetPlatformVersion>

<!-- When upgrading the SDK — change to the new version -->
<!-- Verify in: Project Properties → General → Windows SDK Version -->
```

---

## Lock files

### vcpkg-lock.json — ALWAYS commit it

```bash
# .gitignore must NOT contain:
# vcpkg-lock.json   ← WRONG!

# vcpkg-lock.json MUST be committed
git add vcpkg.json vcpkg-lock.json
```

**Why:**
- Guarantees identical versions across all developers
- Guarantees reproducible builds in CI/CD
- Without a lock file `vcpkg install` may install different versions

### third-party/ in git or not?

```
# Current OES approach: some dependencies live in third-party/ in git
# Pros: reproducible builds without extra software
# Cons: large repo size

# Recommendation: use git submodules or vcpkg for large dependencies
# and keep only small header-only libraries in third-party/

# SQLite amalgamation (two files) — fine to keep in git
# wxWidgets (thousands of files) — better as a git submodule or vcpkg
```

---

## Update checklist

- [ ] Branch `chore/update-deps-YYYY-MM` created
- [ ] CVE scan done (osv-scanner or manual)
- [ ] Vulnerable dependencies updated first
- [ ] Patch/minor updates applied
- [ ] `msbuild Debug x64` — builds without errors or warnings
- [ ] `msbuild Release x64` — builds without errors
- [ ] `ctest` — all tests pass
- [ ] Manual check of core features (when UI libraries were updated)
- [ ] Major updates split into separate commits (if any)
- [ ] `vcpkg-lock.json` or equivalent committed
- [ ] PR created with a description of what was updated and which CVEs closed
- [ ] Tested on staging after merge
