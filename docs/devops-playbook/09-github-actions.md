# 09. CI/CD с GitHub Actions

> Автоматизация для C++ проекта OES: сборка MSBuild/CMake, статический анализ cppcheck, тесты Google Test, упаковка инсталлятора NSIS/WiX, публикация артефактов, автоматизация релизов.

---

## Базовый CI workflow: сборка + анализ + тесты

```yaml
# .github/workflows/ci.yml
name: CI

on:
  pull_request:
    branches: [master, develop]
  push:
    branches: [master, develop]

jobs:
  build-and-test:
    runs-on: windows-latest
    strategy:
      matrix:
        configuration: [Debug, Release]
        platform: [x64]

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Add MSBuild to PATH
        uses: microsoft/setup-msbuild@v2

      - name: Cache NuGet packages
        uses: actions/cache@v4
        with:
          path: ~/.nuget/packages
          key: ${{ runner.os }}-nuget-${{ hashFiles('**/*.vcxproj') }}
          restore-keys: |
            ${{ runner.os }}-nuget-

      - name: Restore NuGet packages
        run: nuget restore enterprise.sln

      - name: Build solution (Windows/MSBuild)
        run: |
          msbuild enterprise.sln `
            /p:Configuration=${{ matrix.configuration }} `
            /p:Platform=${{ matrix.platform }} `
            /p:TreatWarningsAsErrors=true `
            /m `
            /v:minimal

      - name: Run cppcheck (static analysis)
        run: |
          choco install cppcheck --no-progress -y
          cppcheck `
            --enable=warning,performance,portability `
            --suppress=missingIncludeSystem `
            --error-exitcode=1 `
            --xml `
            --output-file=cppcheck-report.xml `
            src/engine/

      - name: Upload cppcheck report
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: cppcheck-report-${{ matrix.configuration }}
          path: cppcheck-report.xml

      - name: Run Google Tests
        run: |
          .\bin\${{ matrix.platform }}\${{ matrix.configuration }}\OES.Tests.exe `
            --gtest_output=xml:test-results.xml
        continue-on-error: false

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-results-${{ matrix.configuration }}-${{ matrix.platform }}
          path: test-results.xml
```

---

## CMake workflow (macOS и Linux; Windows — MSBuild)

```yaml
# .github/workflows/ci-cmake.yml
name: CI (CMake — macOS/Linux)

on:
  pull_request:
    branches: [master, develop]

jobs:
  cmake-build:
    strategy:
      matrix:
        os: [windows-latest, ubuntu-latest, macos-latest]
        build_type: [Release, Debug]
        include:
          - os: windows-latest
            cmake_generator: "Visual Studio 17 2022"
            cmake_arch: "-A x64"
          - os: ubuntu-latest
            cmake_generator: "Unix Makefiles"
            cmake_arch: ""
          - os: macos-latest
            cmake_generator: "Unix Makefiles"
            cmake_arch: ""

    runs-on: ${{ matrix.os }}

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      # Windows: установить зависимости через vcpkg
      - name: Setup vcpkg (Windows)
        if: runner.os == 'Windows'
        run: |
          git clone https://github.com/microsoft/vcpkg.git C:/vcpkg
          C:/vcpkg/bootstrap-vcpkg.bat
          C:/vcpkg/vcpkg install wxwidgets:x64-windows firebird:x64-windows

      # macOS: установить зависимости через Homebrew
      - name: Install dependencies (macOS)
        if: runner.os == 'macOS'
        run: |
          brew install cmake ninja pkg-config
          brew install wxwidgets
          brew install firebird
          brew install libpq sqlite mysql-client
          brew install cppcheck

      # Linux: установить зависимости через apt + wxWidgets из исходников
      - name: Install dependencies (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            firebird3.0-dev \
            libpq-dev \
            libsqlite3-dev \
            libmysqlclient-dev \
            cppcheck \
            ninja-build \
            libgtk-3-dev libgl1-mesa-dev libglu1-mesa-dev \
            libpng-dev libjpeg-dev libtiff-dev libexpat1-dev

      # wxWidgets 3.3.2 недоступен в apt Ubuntu 22.04/24.04 — собираем из исходников.
      # Используем кэш чтобы не пересобирать при каждом запуске.
      - name: Cache wxWidgets build
        id: wx-cache
        uses: actions/cache@v4
        with:
          path: /opt/wxwidgets
          key: wxwidgets-3.3.2-linux-${{ runner.os }}

      - name: Build wxWidgets 3.3.2 from source (Linux)
        if: runner.os == 'Linux' && steps.wx-cache.outputs.cache-hit != 'true'
        run: |
          curl -fsSL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2 \
            | tar xj
          cd wxWidgets-3.3.2
          mkdir build-release && cd build-release
          ../configure --enable-unicode --disable-shared --with-gtk=3 \
            --prefix=/opt/wxwidgets CXX=g++ CXXFLAGS="-std=c++17 -O2"
          make -j$(nproc)
          sudo make install

      - name: Configure CMake
        run: |
          cmake -B build \
            -G "${{ matrix.cmake_generator }}" ${{ matrix.cmake_arch }} \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DOES_BUILD_TESTS=ON \
            -DOES_ENABLE_FIREBIRD=ON \
            -DOES_ENABLE_POSTGRESQL=ON

      - name: Build
        run: cmake --build build --config ${{ matrix.build_type }} --parallel

      - name: Run tests
        working-directory: build
        run: ctest --config ${{ matrix.build_type }} --output-on-failure

      - name: Static analysis (cppcheck)
        run: |
          cppcheck \
            --enable=warning,performance,portability \
            --suppress=missingIncludeSystem \
            --error-exitcode=1 \
            src/engine/

      - name: Upload build artifacts
        uses: actions/upload-artifact@v4
        with:
          name: oes-${{ matrix.os }}-${{ matrix.build_type }}
          path: |
            build/Release/**/*.exe
            build/Release/**/*.dll
            build/**/*.so
            build/**/*.dylib
          retention-days: 7
```

---

## Release workflow: сборка инсталлятора + публикация

```yaml
# .github/workflows/release.yml
name: Release

on:
  push:
    tags:
      - 'v*.*.*'

jobs:
  build-installer:
    runs-on: windows-latest
    permissions:
      contents: write

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Add MSBuild to PATH
        uses: microsoft/setup-msbuild@v2

      - name: Extract version from tag
        id: version
        run: |
          $tag = "${{ github.ref_name }}"
          $version = $tag.TrimStart('v')
          echo "version=$version" >> $env:GITHUB_OUTPUT
          echo "tag=$tag" >> $env:GITHUB_OUTPUT

      - name: Restore NuGet packages
        run: nuget restore enterprise.sln

      - name: Build Release (Windows/MSBuild)
        run: |
          msbuild enterprise.sln `
            /p:Configuration=Release `
            /p:Platform=x64 `
            /p:Version=${{ steps.version.outputs.version }} `
            /m

      - name: Run tests before packaging
        run: |
          .\bin\x64\Release\OES.Tests.exe --gtest_output=xml:test-results.xml
        continue-on-error: false

      # Подписать бинарники (требует сертификат в Secrets)
      - name: Code sign binaries
        if: ${{ secrets.CODE_SIGN_CERT != '' }}
        run: |
          $pfxPath = "$env:TEMP\codesign.pfx"
          [System.IO.File]::WriteAllBytes($pfxPath, [System.Convert]::FromBase64String("${{ secrets.CODE_SIGN_CERT }}"))
          
          $binaries = Get-ChildItem -Path .\bin\x64\Release -Filter "*.exe","*.dll" -Recurse
          foreach ($file in $binaries) {
            signtool sign `
              /f $pfxPath `
              /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
              /t http://timestamp.sectigo.com `
              /fd sha256 `
              $file.FullName
            Write-Host "Signed: $($file.Name)"
          }
          Remove-Item $pfxPath

      # Собрать NSIS инсталлятор
      - name: Build NSIS installer
        run: |
          choco install nsis --no-progress -y
          makensis `
            /DVERSION=${{ steps.version.outputs.version }} `
            /DOUTDIR=dist `
            installer/oes-setup.nsi

      # Альтернативно — WiX инсталлятор
      # - name: Build WiX installer
      #   run: |
      #     dotnet tool install --global wix
      #     wix build installer/oes.wxs `
      #       -d Version=${{ steps.version.outputs.version }} `
      #       -o dist/OES-${{ steps.version.outputs.version }}-Setup.msi

      # Подписать инсталлятор
      - name: Sign installer
        if: ${{ secrets.CODE_SIGN_CERT != '' }}
        run: |
          $pfxPath = "$env:TEMP\codesign.pfx"
          [System.IO.File]::WriteAllBytes($pfxPath, [System.Convert]::FromBase64String("${{ secrets.CODE_SIGN_CERT }}"))
          signtool sign `
            /f $pfxPath `
            /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
            /t http://timestamp.sectigo.com `
            /fd sha256 `
            dist\OES-${{ steps.version.outputs.version }}-Setup.exe
          Remove-Item $pfxPath

      - name: Calculate checksums
        run: |
          $installer = Get-Item "dist\OES-${{ steps.version.outputs.version }}-Setup.exe"
          $hash = (Get-FileHash $installer.FullName -Algorithm SHA256).Hash
          "$hash  $($installer.Name)" | Out-File -FilePath "dist\SHA256SUMS.txt"
          Get-Content "dist\SHA256SUMS.txt"

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ github.ref_name }}
          name: "OES ${{ steps.version.outputs.version }}"
          body: |
            ## Open Enterprise Solutions ${{ steps.version.outputs.version }}
            
            ### Установка
            1. Скачайте `OES-${{ steps.version.outputs.version }}-Setup.exe`
            2. Проверьте SHA256: см. `SHA256SUMS.txt`
            3. Запустите инсталлятор от имени администратора
            
            ### Изменения
            _Список изменений см. в CHANGELOG.md_
          files: |
            dist/OES-${{ steps.version.outputs.version }}-Setup.exe
            dist/SHA256SUMS.txt
          draft: false
          prerelease: ${{ contains(github.ref_name, '-beta') || contains(github.ref_name, '-rc') }}

      - name: Notify Telegram
        if: always()
        run: |
          $status = "${{ job.status }}"
          $emoji = if ($status -eq "success") { "✅" } else { "❌" }
          $message = "$emoji Release OES ${{ steps.version.outputs.version }}`nСтатус: $status"
          
          Invoke-RestMethod `
            -Uri "https://api.telegram.org/bot${{ secrets.TELEGRAM_BOT_TOKEN }}/sendMessage" `
            -Method POST `
            -ContentType "application/json" `
            -Body (ConvertTo-Json @{
              chat_id = "${{ secrets.TELEGRAM_CHAT_ID }}"
              text = $message
            })
```

---

## Self-hosted runner (Windows)

### Установка на Windows-машине сборки

```powershell
# Создать директорию для runner
mkdir C:\actions-runner
cd C:\actions-runner

# Скачать runner (версию взять из GitHub: Settings → Actions → Runners → New)
Invoke-WebRequest -Uri "https://github.com/actions/runner/releases/download/v2.311.0/actions-runner-win-x64-2.311.0.zip" -OutFile "runner.zip"
Expand-Archive -Path "runner.zip" -DestinationPath .

# Настроить (токен из GitHub: Settings → Actions → Runners → New)
.\config.cmd `
  --url https://github.com/org/oes `
  --token ТОКЕН_ИЗ_GITHUB `
  --name "windows-build-machine" `
  --labels "self-hosted,windows,x64,msbuild" `
  --unattended

# Установить как сервис Windows
.\svc.cmd install
.\svc.cmd start
.\svc.cmd status
```

### Преимущества self-hosted для OES

```
✅ Нет ограничения на время сборки (github-hosted = 6 часов max)
✅ Локальный кэш зависимостей (wxWidgets, Firebird SDK)
✅ Лицензированный Visual Studio уже установлен
✅ Подписание кода с локальным HSM/USB-токеном
✅ Нет оплаты за минуты Windows runner (они платные на github.com)
```

### Использование self-hosted runner в workflow

```yaml
jobs:
  build:
    runs-on: [self-hosted, windows, msbuild]
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build (Windows/MSBuild)
        run: |
          msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m
```

---

## Кэширование зависимостей C++

```yaml
      # Кэш wxWidgets
      - name: Cache wxWidgets
        uses: actions/cache@v4
        with:
          path: C:\wxWidgets
          key: wxwidgets-3.3.2-x64
          restore-keys: wxwidgets-3.3.2-

      # Кэш Firebird SDK
      - name: Cache Firebird SDK
        uses: actions/cache@v4
        with:
          path: C:\Firebird
          key: firebird-sdk-4.0-x64

      # Кэш vcpkg (если используется)
      - name: Cache vcpkg
        uses: actions/cache@v4
        with:
          path: C:\vcpkg\installed
          key: vcpkg-${{ runner.os }}-${{ hashFiles('vcpkg.json') }}
          restore-keys: vcpkg-${{ runner.os }}-

      # Кэш компиляции (ccache для Linux, clcache для Windows)
      - name: Cache clcache (Windows)
        uses: actions/cache@v4
        with:
          path: C:\clcache
          key: clcache-${{ runner.os }}-${{ github.sha }}
          restore-keys: clcache-${{ runner.os }}-
```

---

## GitHub Secrets для OES

```
Repository → Settings → Secrets and variables → Actions → New repository secret

Необходимые секреты:
  CODE_SIGN_CERT          — сертификат подписи кода (base64 .pfx)
  CODE_SIGN_PASSWORD      — пароль от сертификата
  TELEGRAM_BOT_TOKEN      — Telegram Bot Token для уведомлений
  TELEGRAM_CHAT_ID        — Telegram Chat ID команды

Для деплоя daemon/service:
  DEPLOY_SSH_KEY          — SSH ключ для деплоя на Linux-сервер (daemon режим)
  DEPLOY_SERVER_HOST      — хост сервера
  DEPLOY_SERVER_USER      — пользователь для деплоя

Для обновлений:
  UPDATE_SERVER_API_KEY   — ключ API сервера обновлений (если используется)
```

---

## Уведомления

### Telegram (PowerShell)

```yaml
      - name: Telegram Notification
        if: always()
        run: |
          $status = "${{ job.status }}"
          $emoji = if ($status -eq "success") { "✅" } else { "❌" }
          
          $body = @{
            chat_id = "${{ secrets.TELEGRAM_CHAT_ID }}"
            text = "$emoji ${{ github.workflow }}`nБранч: ${{ github.ref_name }}`nКоммит: ${{ github.event.head_commit.message }}`nАвтор: ${{ github.actor }}"
            parse_mode = "HTML"
          } | ConvertTo-Json
          
          Invoke-RestMethod `
            -Uri "https://api.telegram.org/bot${{ secrets.TELEGRAM_BOT_TOKEN }}/sendMessage" `
            -Method POST `
            -ContentType "application/json" `
            -Body $body
```

---

## Пример полного pipeline для OES

```
Пуш в master/develop:
  1. Checkout + cache restore
  2. Windows: MSBuild enterprise.sln (Debug + Release, x64)
     macOS/Linux: CMake + Ninja (src/engine/backend/, src/engine/frontend/)
  3. cppcheck (статический анализ src/engine/)
  4. Google Test (unit-тесты)
  5. Upload test results

Тег v*.*.*:
  1. Всё из CI pipeline
  2. Code signing (бинарники — только Windows)
  3. NSIS installer build (Windows)
  4. Sign installer
  5. SHA256 checksums
  6. GitHub Release + upload assets
  7. Telegram уведомление команды

PR (в ветку master или develop):
  1. CI pipeline (быстрая проверка — все 3 платформы)
  2. cppcheck report как комментарий к PR
  3. Test results в PR status checks
```

## Entry points проекта OES

```
src/engine/enterprise/mainApp.cpp  — основное desktop-приложение OES
src/engine/designer/mainApp.cpp    — дизайнер форм и отчётов
src/engine/daemon/daemon.cpp       — daemon/server режим (oesd)

Ключевые классы:
  ibDatabaseLayer                  — базовый слой работы с БД
  ibDatabaseLayerFirebird          — Firebird backend
  ibDatabaseLayerPostgres          — PostgreSQL backend
  ibPreparedStatement              — параметризованные запросы
  ibApplicationData                — данные приложения / сессия
  ibApplicationData::AuthenticationAndSetUser()  — аутентификация
  ibApplicationDataSessionUpdater  — обновление сессии
  ibMetaDataConfiguration          — конфигурация метаданных
  CreateAndUpdateTableDB()         — создание/обновление схемы БД
```
