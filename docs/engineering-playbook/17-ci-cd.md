# 17. CI/CD

## Стек

| Компонент | Назначение |
|-----------|-----------|
| **GitHub Actions** | CI: сборка, статический анализ, тесты |
| **MSBuild** | Сборка под Windows (Visual Studio 2017+) |
| **CMake** | Кросс-платформенная сборка (переходный период) |
| **cppcheck** | Статический анализ C++ |
| **Google Test (gtest)** | Юнит-тесты |
| **Dr. Memory / Valgrind** | Проверка утечек памяти (опционально в CI) |
| **Inno Setup** | Сборка установщика Windows |

---

## Workflow: PR Check

На каждый pull request автоматически запускается проверка — сборка и тесты:

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

## Workflow: PR Check (CMake — кросс-платформенный)

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
    name: Linux / CMake (кросс-платформенная проверка)
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

При мердже в `master` — сборка release-артефакта и установщика:

```yaml
# .github/workflows/release.yml
name: Release Build

on:
  push:
    branches: [master]
  workflow_dispatch:
    inputs:
      version:
        description: 'Версия релиза (например: 2.5.1)'
        required: true

jobs:
  build:
    name: Build Release
    runs-on: windows-2022
    environment: production   # требует ручного approve

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
          echo "Версия сборки: $ver"

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

PR не мерджится, если:

- **Build fails** — код не компилируется
- **Tests fail** — юнит-тесты не прошли
- **cppcheck errors** — критические предупреждения анализатора
- **Test coverage падает** — (если настроен coverage gate)

Настройка в GitHub: Settings → Branches → Branch protection rules:

```
Branch name pattern: master
☑ Require status checks to pass before merging
  - Build & Test (Windows / MSBuild)        [required]
  - Windows / CMake                         [required]
  - Linux / CMake (кросс-платформенная)     [required]
☑ Require pull request reviews before merging
  - Required approving reviews: 1
☑ Require branches to be up to date before merging
```

---

## Структура тестов (Google Test)

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

### Пример теста

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
    m_mockDb->SetNextResult({});  // пустой результат

    DocumentData doc;
    wxString err;
    bool ok = m_repo->GetById(999, doc, err);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.IsEmpty());
}

TEST_F(DocumentRepositoryTest, Create_UsesParameterizedStatement)
{
    DocumentData data;
    data.name   = "New Doc'; DROP TABLE documents; --";  // SQL-инъекция попытка
    data.status = "draft";
    int newId = 0;
    wxString err;

    // Mock проверяет, что запрос параметризован, не конкатенирован
    m_mockDb->SetNextInsertId(42);
    auto result = m_repo->Create(data, newId);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(newId, 42);
    EXPECT_TRUE(m_mockDb->LastStatementWasPrepared());
}
```

---

## vcpkg манифест (vcpkg.json)

Зафиксируйте версии зависимостей vcpkg через манифест — это обеспечивает воспроизводимые сборки на Windows и используется как ключ кэша в CI:

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

Файл `vcpkg.json` размещается в корне репозитория. При наличии манифеста `vcpkg install` (без аргументов) устанавливает именно эти версии.

---

## CMake структура проекта

```cmake
# CMakeLists.txt (корень)
cmake_minimum_required(VERSION 3.22)
project(OES VERSION 2.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опции сборки
option(OES_BUILD_TESTS "Собирать тесты" OFF)
option(OES_ENABLE_ASAN "Address Sanitizer (Debug)" OFF)

# Зависимости
find_package(wxWidgets 3.3 REQUIRED COMPONENTS core base adv)
include(${wxWidgets_USE_FILE})

# Основная цель
add_subdirectory(src)

# Тесты
if(OES_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
```

---

## Кэширование в CI

```yaml
# Кэш vcpkg (медленная установка зависимостей)
- name: Cache vcpkg
  uses: actions/cache@v4
  with:
    path: |
      C:\vcpkg\installed
      C:\vcpkg\buildtrees
    key: vcpkg-${{ runner.os }}-${{ hashFiles('vcpkg.json') }}
    restore-keys: |
      vcpkg-${{ runner.os }}-

# Кэш CMake build directory
- name: Cache CMake
  uses: actions/cache@v4
  with:
    path: build
    key: cmake-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'src/**/*.h') }}
    restore-keys: cmake-${{ runner.os }}-
```

---

## Secrets

Хранить в GitHub: Settings → Secrets and variables → Actions:

```
CODE_SIGN_CERTIFICATE   — PFX-сертификат для подписи (base64)
CODE_SIGN_PASSWORD      — Пароль к сертификату
INNO_SETUP_KEY          — Лицензионный ключ Inno Setup (если commercial)
TELEGRAM_BOT_TOKEN      — Для уведомлений о сборке
TELEGRAM_CHAT_ID        — ID чата команды
```

Никогда не коммитить сертификаты, `.pfx` файлы, лицензии прямо в репозиторий.

---

## Уведомления о сборке

```yaml
- name: Notify Telegram on failure
  if: failure()
  uses: appleboy/telegram-action@master
  with:
    to: ${{ secrets.TELEGRAM_CHAT_ID }}
    token: ${{ secrets.TELEGRAM_BOT_TOKEN }}
    message: |
      СБОРКА УПАЛА: ${{ github.repository }}
      Ветка: ${{ github.ref_name }}
      Коммит: ${{ github.event.head_commit.message }}
      Автор: ${{ github.actor }}
      Лог: ${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}

- name: Notify Telegram on release
  if: success() && github.ref == 'refs/heads/master'
  uses: appleboy/telegram-action@master
  with:
    to: ${{ secrets.TELEGRAM_CHAT_ID }}
    token: ${{ secrets.TELEGRAM_BOT_TOKEN }}
    message: |
      РЕЛИЗ СОБРАН: OES v${{ steps.version.outputs.VERSION }}
      Артефакт доступен в GitHub Releases
```

---

## Подписание установщика

```yaml
- name: Sign installer
  if: github.ref == 'refs/heads/master'
  run: |
    # Расшифровать сертификат из secret
    $certBytes = [Convert]::FromBase64String("${{ secrets.CODE_SIGN_CERTIFICATE }}")
    [IO.File]::WriteAllBytes("signing.pfx", $certBytes)

    # Подписать
    & "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe" sign `
      /f signing.pfx `
      /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
      /t http://timestamp.digicert.com `
      /fd sha256 `
      "installer\Output\OES_Setup_*.exe"

    # Удалить сертификат
    Remove-Item signing.pfx
```

---

## Чеклист перед мерджем в master

1. Все проверки CI зелёные (build, tests, cppcheck)
2. Code review от минимум одного разработчика
3. Новый функционал покрыт тестами
4. Файл `CHANGELOG.md` или `VERSION` обновлён
5. Нет закомментированного кода и debug-вывода (`printf`, `wxLogDebug` не для release)
6. Миграции БД включены в PR (если есть изменения схемы)
