# 17. CI/CD

> **Reality check (verify against the repo before copying a snippet):**
> - MSBuild output lands in `bin\<Platform>\<Configuration>\` where `Platform`
>   is `Win32` (x86) or `Win64` (x64) — e.g. `bin\Win64\Release\`, **not**
>   `bin\Release\`.
> - There is **one** solution, `enterprise.sln` (no separate
>   `enterprise_tests.sln`). The gtest suite is the CMake target **`oes_tests`**,
>   built via `cmake -B build -DBUILD_TESTING=ON` and run with `ctest`. Google
>   Test is fetched via `FetchContent`, not vcpkg.
> - The workflows below are illustrative templates; the on-disk
>   `.github/workflows/` may differ.

## Stack

| Component | Purpose |
|-----------|-----------|
| **GitHub Actions** | CI: build, static analysis, tests |
| **MSBuild** | Windows build (Visual Studio 2017+) |
| **CMake** | Cross-platform build (transitional period) |
| **cppcheck** | C++ static analysis |
| **Google Test (gtest)** | Unit tests |
| **Dr. Memory / Valgrind** | Memory leak detection (optional in CI) |
| **Inno Setup** | Windows installer build |

---

## Workflow: PR Check

Every pull request triggers an automatic check — build and tests:

```yaml
# .github/workflows/pr-check.yml
name: PR Check

on:
  pull_request:
    branches: [master, develop]

jobs:
  build-and-test-windows:
    name: Build & Test (Windows / MSBuild)
    runs-on: windows-2022

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Cache vcpkg packages
        uses: actions/cache@v4
        with:
          path: C:\vcpkg\installed
          key: vcpkg-${{ runner.os }}-${{ hashFiles('vcpkg.json') }}
          restore-keys: vcpkg-${{ runner.os }}-

      - name: Setup MSBuild
        uses: microsoft/setup-msbuild@v2

      - name: Install dependencies (vcpkg)
        run: |
          vcpkg install wxwidgets:x64-windows
          vcpkg install gtest:x64-windows
          vcpkg integrate install

      - name: Static analysis (cppcheck)
        run: |
          choco install cppcheck -y
          cppcheck --enable=warning,performance,portability `
            --error-exitcode=1 `
            --std=c++17 `
            --suppress=missingIncludeSystem `
            -I src/ `
            src/

      - name: Build (Release)
        run: |
          msbuild enterprise.sln `
            /p:Configuration=Release `
            /p:Platform=x64 `
            /p:VcpkgEnabled=true `
            /m `
            /nologo

      - name: Build (Tests)
        run: |
          msbuild enterprise_tests.sln `
            /p:Configuration=Release `
            /p:Platform=x64 `
            /m `
            /nologo

      - name: Run tests
        run: |
          .\bin\Release\enterprise_tests.exe `
            --gtest_output=xml:test-results.xml

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-results
          path: test-results.xml
```

---

## Workflow: PR Check (CMake — cross-platform)

```yaml
# .github/workflows/pr-check-cmake.yml
name: PR Check (CMake)

on:
  pull_request:
    branches: [master, develop]

jobs:
  build-windows:
    name: Windows / CMake
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Cache CMake build
        uses: actions/cache@v4
        with:
          path: build
          key: cmake-win-${{ hashFiles('CMakeLists.txt', 'cmake/**') }}

      - name: Configure
        run: |
          cmake -B build -G "Visual Studio 17 2022" -A x64 `
            -DCMAKE_BUILD_TYPE=Release `
            -DOES_BUILD_TESTS=ON `
            -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

      - name: Build
        run: cmake --build build --config Release --parallel

      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure

  build-linux:
    name: Linux / CMake (cross-platform check)
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install base dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libgtest-dev \
            cmake \
            cppcheck \
            ninja-build \
            libgtk-3-dev \
            libgl1-mesa-dev \
            libglu1-mesa-dev

      # ubuntu-22.04 does not ship wxWidgets 3.3.x; build from source or use cache
      - name: Cache wxWidgets 3.3.2 build
        id: cache-wx
        uses: actions/cache@v4
        with:
          path: /opt/wx332
          key: wx-3.3.2-ubuntu-22.04-gtk3

      - name: Build wxWidgets 3.3.2 from source
        if: steps.cache-wx.outputs.cache-hit != 'true'
        run: |
          wget -q https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2
          tar xf wxWidgets-3.3.2.tar.bz2
          cd wxWidgets-3.3.2
          ./configure --prefix=/opt/wx332 \
            --enable-unicode --disable-debug \
            --with-gtk=3 --disable-shared
          make -j$(nproc)
          sudo make install

      - name: Set wxWidgets paths
        run: echo "/opt/wx332/bin" >> $GITHUB_PATH

      - name: Static analysis
        run: |
          cppcheck --enable=warning,performance,portability \
            --error-exitcode=1 \
            --std=c++17 \
            --suppress=missingIncludeSystem \
            -I src/ \
            src/

      - name: Configure
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DOES_BUILD_TESTS=ON \
            -DwxWidgets_CONFIG_EXECUTABLE=/opt/wx332/bin/wx-config

      - name: Build
        run: cmake --build build --parallel

      - name: Test
        run: ctest --test-dir build --output-on-failure
```

---

## Workflow: Release Build (Windows)

On merge into `master` — build the release artifact and the installer:

```yaml
# .github/workflows/release.yml
name: Release Build

on:
  push:
    branches: [master]
  workflow_dispatch:
    inputs:
      version:
        description: 'Release version (e.g. 2.5.1)'
        required: true

jobs:
  build:
    name: Build Release
    runs-on: windows-2022
    environment: production   # requires manual approval

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Determine version
        id: version
        run: |
          $ver = "${{ github.event.inputs.version }}"
          if (-not $ver) { $ver = (Get-Content VERSION).Trim() }
          echo "VERSION=$ver" >> $env:GITHUB_OUTPUT
          echo "Build version: $ver"

      - name: Setup MSBuild
        uses: microsoft/setup-msbuild@v2

      - name: Cache vcpkg
        uses: actions/cache@v4
        with:
          path: C:\vcpkg\installed
          key: vcpkg-${{ runner.os }}-${{ hashFiles('vcpkg.json') }}

      - name: Build Release
        run: |
          msbuild enterprise.sln `
            /p:Configuration=Release `
            /p:Platform=x64 `
            /p:Version=${{ steps.version.outputs.VERSION }} `
            /m /nologo

      - name: Run tests
        run: .\bin\Release\enterprise_tests.exe

      - name: Build installer (Inno Setup)
        run: |
          iscc /DAppVersion=${{ steps.version.outputs.VERSION }} `
            installer\setup.iss

      - name: Upload installer artifact
        uses: actions/upload-artifact@v4
        with:
          name: OES-installer-${{ steps.version.outputs.VERSION }}
          path: installer\Output\OES_Setup_*.exe

      - name: Create GitHub Release
        if: github.event.inputs.version != ''
        uses: softprops/action-gh-release@v2
        with:
          tag_name: v${{ steps.version.outputs.VERSION }}
          name: OES v${{ steps.version.outputs.VERSION }}
          files: installer\Output\OES_Setup_*.exe
          generate_release_notes: true
```

---

## Quality Gates

A PR cannot be merged if:

- **Build fails** — code does not compile
- **Tests fail** — unit tests did not pass
- **cppcheck errors** — critical analyzer warnings
- **Test coverage drops** — (if a coverage gate is configured)

Configuration in GitHub: Settings → Branches → Branch protection rules:

```
Branch name pattern: master
[x] Require status checks to pass before merging
  - Build & Test (Windows / MSBuild)        [required]
  - Windows / CMake                         [required]
  - Linux / CMake (cross-platform)          [required]
[x] Require pull request reviews before merging
  - Required approving reviews: 1
[x] Require branches to be up to date before merging
```

---

## Test layout (Google Test)

```
tests/
├── CMakeLists.txt
├── database/
│   ├── test_document_repository.cpp
│   ├── test_migration_manager.cpp
│   └── mock_database_layer.h
├── report/
│   ├── test_report_engine.cpp
│   └── mock_report_engine.h
├── core/
│   ├── test_document_model.cpp
│   └── test_validation.cpp
└── main_test.cpp
```

### Example test

```cpp
// tests/database/test_document_repository.cpp
#include <gtest/gtest.h>
#include "mock_database_layer.h"
#include "../../src/database/document_repository.h"

class DocumentRepositoryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_mockDb = std::make_unique<MockDatabaseLayer>();
        m_repo   = std::make_unique<OesDocumentRepository>(m_mockDb.get());
    }

    std::unique_ptr<MockDatabaseLayer>    m_mockDb;
    std::unique_ptr<OesDocumentRepository> m_repo;
};

TEST_F(DocumentRepositoryTest, GetById_ReturnsDocument_WhenExists)
{
    m_mockDb->SetNextResult({{1, "Test Document", "draft"}});

    DocumentData doc;
    wxString err;
    bool ok = m_repo->GetById(1, doc, err);

    EXPECT_TRUE(ok);
    EXPECT_EQ(doc.id, 1);
    EXPECT_EQ(doc.name, "Test Document");
    EXPECT_TRUE(err.IsEmpty());
}

TEST_F(DocumentRepositoryTest, GetById_ReturnsFalse_WhenNotFound)
{
    m_mockDb->SetNextResult({});  // empty result

    DocumentData doc;
    wxString err;
    bool ok = m_repo->GetById(999, doc, err);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.IsEmpty());
}

TEST_F(DocumentRepositoryTest, Create_UsesParameterizedStatement)
{
    DocumentData data;
    data.name   = "New Doc'; DROP TABLE documents; --";  // SQL injection attempt
    data.status = "draft";
    int newId = 0;
    wxString err;

    // Mock verifies the query is parameterized, not concatenated
    m_mockDb->SetNextInsertId(42);
    auto result = m_repo->Create(data, newId);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(newId, 42);
    EXPECT_TRUE(m_mockDb->LastStatementWasPrepared());
}
```

---

## vcpkg manifest (vcpkg.json)

Pin dependency versions in the vcpkg manifest — that guarantees reproducible builds on Windows and serves as the cache key in CI:

```json
{
  "name": "oes",
  "version": "2.5.0",
  "dependencies": [
    {
      "name": "wxwidgets",
      "version>=": "3.3.2"
    },
    {
      "name": "gtest",
      "version>=": "1.14.0"
    }
  ]
}
```

`vcpkg.json` sits at the repository root. With a manifest present, `vcpkg install` (no arguments) installs exactly these versions.

---

## CMake project layout

```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.22)
project(OES VERSION 2.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(OES_BUILD_TESTS "Build tests" OFF)
option(OES_ENABLE_ASAN "Address Sanitizer (Debug)" OFF)

# Dependencies
find_package(wxWidgets 3.3 REQUIRED COMPONENTS core base adv)
include(${wxWidgets_USE_FILE})

# Main target
add_subdirectory(src)

# Tests
if(OES_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
```

---

## CI caching

```yaml
# vcpkg cache (slow dependency install)
- name: Cache vcpkg
  uses: actions/cache@v4
  with:
    path: |
      C:\vcpkg\installed
      C:\vcpkg\buildtrees
    key: vcpkg-${{ runner.os }}-${{ hashFiles('vcpkg.json') }}
    restore-keys: |
      vcpkg-${{ runner.os }}-

# CMake build cache
- name: Cache CMake
  uses: actions/cache@v4
  with:
    path: build
    key: cmake-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'src/**/*.h') }}
    restore-keys: cmake-${{ runner.os }}-
```

---

## Secrets

Stored in GitHub: Settings → Secrets and variables → Actions:

```
CODE_SIGN_CERTIFICATE   — PFX signing certificate (base64)
CODE_SIGN_PASSWORD      — Certificate password
INNO_SETUP_KEY          — Inno Setup license key (if commercial)
TELEGRAM_BOT_TOKEN      — For build notifications
TELEGRAM_CHAT_ID        — Team chat ID
```

Never commit certificates, `.pfx` files, or licenses directly into the repository.

---

## Build notifications

```yaml
- name: Notify Telegram on failure
  if: failure()
  uses: appleboy/telegram-action@master
  with:
    to: ${{ secrets.TELEGRAM_CHAT_ID }}
    token: ${{ secrets.TELEGRAM_BOT_TOKEN }}
    message: |
      BUILD FAILED: ${{ github.repository }}
      Branch: ${{ github.ref_name }}
      Commit: ${{ github.event.head_commit.message }}
      Author: ${{ github.actor }}
      Log: ${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}

- name: Notify Telegram on release
  if: success() && github.ref == 'refs/heads/master'
  uses: appleboy/telegram-action@master
  with:
    to: ${{ secrets.TELEGRAM_CHAT_ID }}
    token: ${{ secrets.TELEGRAM_BOT_TOKEN }}
    message: |
      RELEASE BUILT: OES v${{ steps.version.outputs.VERSION }}
      Artifact available in GitHub Releases
```

---

## Signing the installer

```yaml
- name: Sign installer
  if: github.ref == 'refs/heads/master'
  run: |
    # Decode the certificate from the secret
    $certBytes = [Convert]::FromBase64String("${{ secrets.CODE_SIGN_CERTIFICATE }}")
    [IO.File]::WriteAllBytes("signing.pfx", $certBytes)

    # Sign
    & "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe" sign `
      /f signing.pfx `
      /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
      /t http://timestamp.digicert.com `
      /fd sha256 `
      "installer\Output\OES_Setup_*.exe"

    # Remove the certificate
    Remove-Item signing.pfx
```

---

## Checklist before merge into master

1. All CI checks green (build, tests, cppcheck)
2. Code review from at least one developer
3. New functionality is covered by tests
4. `CHANGELOG.md` or `VERSION` updated
5. No commented-out code and no debug output (`printf`, `wxLogDebug` not for release)
6. DB migrations included in the PR (if there are schema changes)
