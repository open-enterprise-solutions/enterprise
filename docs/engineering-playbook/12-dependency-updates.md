# 12. Обновление зависимостей

## Зачем обновлять

- **Безопасность** — уязвимости в библиотеках закрываются патчами (OpenSSL CVE — реальная угроза)
- **Совместимость** — устаревшие библиотеки перестают работать с новыми ОС/компиляторами
- **Производительность** — новые версии часто быстрее
- **Функциональность** — новые возможности wxWidgets, исправленные баги
- **Поддержка** — EOL версии не получают фиксов безопасности

Чем дольше откладываете обновление, тем больнее оно будет. Регулярные маленькие обновления лучше, чем одно огромное раз в год.

---

## Периодичность

| Действие | Частота | Кто |
|----------|---------|-----|
| Проверка CVE для используемых библиотек | Раз в неделю (или в CI) | Автоматически / разработчик |
| Проверка новых версий зависимостей | Раз в 2 недели | Разработчик |
| Обновление patch-версий (исправления багов) | Раз в месяц | Разработчик |
| Обновление minor-версий (новые фичи) | Раз в квартал | Разработчик |
| Обновление major-версий (breaking changes) | По необходимости | Тимлид + разработчик |
| Обновление Visual Studio / компилятора | При выходе обновления безопасности | Тимлид |
| Обновление Windows SDK | По необходимости | Тимлид |

---

## Зависимости OES

### Ключевые библиотеки

| Библиотека | Текущее назначение | Как обновлять |
|------------|-------------------|---------------|
| **wxWidgets** | GUI фреймворк | Вручную, см. ниже |
| **IBPP** | Firebird C++ API | Вручную из репозитория IBPP |
| **OpenSSL** | Криптография | vcpkg или вручную |
| **zlib** | Сжатие | vcpkg или вручную |
| **libpq** | PostgreSQL клиент | vcpkg или вместе с PostgreSQL |
| **SQLite** | Встраиваемая БД | amalgamation файл с sqlite.org |
| **Google Test** | Тестирование | vcpkg или FetchContent |
| **nlohmann/json** | JSON (если используется) | vcpkg или single header |

### Где хранятся зависимости

```
third-party/
├── IBPP/           — Firebird C++ API (исходники, собираем сами)
├── wxWidgets/      — wxWidgets (символическая ссылка или submodule)
└── sqlite/         — SQLite amalgamation (sqlite3.c + sqlite3.h)

# Через vcpkg (рекомендуется для новых зависимостей)
vcpkg.json          — Манифест зависимостей (если перешли на vcpkg)
```

---

## Инструменты проверки

### OSV Scanner — проверка CVE

```bash
# Установка (Go required)
go install github.com/google/osv-scanner/cmd/osv-scanner@latest

# Сканировать зависимости
# ВНИМАНИЕ: osv-scanner не распознаёт vcpkg.json как lockfile-формат.
# Используйте --sbom с CycloneDX/SPDX SBOM (можно сгенерировать через vcpkg export --json):
osv-scanner --sbom sbom.json .

# Или сканировать весь каталог (OSV найдёт то, что умеет):
osv-scanner .

# Или через GitHub Actions (см. ниже)
```

### vcpkg — управление зависимостями

```bash
# Показать установленные пакеты
vcpkg list

# Показать доступные обновления (без применения)
vcpkg upgrade               # только показывает список устаревших пакетов

# Применить обновления (ВНИМАНИЕ: реально обновляет пакеты)
vcpkg upgrade --no-dry-run  # --no-dry-run ПРИМЕНЯЕТ обновления, а не только показывает!

# Проверить конкретный пакет
vcpkg search openssl

# Пример vcpkg.json (манифест)
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

### Ручная проверка новых версий

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

## Процесс обновления

### Шаг 1: Создать ветку

```bash
git checkout master
git pull origin master
git checkout -b chore/update-deps-2026-03
```

### Шаг 2: Проверить CVE уязвимости

```bash
# OSV Scanner
osv-scanner --lockfile vcpkg.json .

# Или проверить вручную через NVD (National Vulnerability Database)
# https://nvd.nist.gov/vuln/search
# Поиск по: wxwidgets, openssl, sqlite, firebird

# Проверить GitHub Security Advisories для используемых библиотек
```

### Шаг 3: Проверить что устарело

```bash
# vcpkg — показать список устаревших пакетов (без применения)
vcpkg upgrade

# Вручную сверить версии в third-party/ с GitHub releases:
# wxWidgets:  https://github.com/wxWidgets/wxWidgets/releases
# SQLite:     https://sqlite.org/changes.html
# OpenSSL:    https://github.com/openssl/openssl/releases
```

Классифицируйте обновления:

| Тип обновления | Пример | Риск | Действие |
|----------------|--------|------|----------|
| **Patch** (x.y.Z) | wxWidgets 3.3.2 → 3.3.3 | Низкий | Обновлять смело |
| **Minor** (x.Y.0) | wxWidgets 3.3.2 → 3.4.0 | Средний | Обновлять, проверить changelog |
| **Major** (X.0.0) | wxWidgets 3.x → 4.0 | Высокий | Отдельная задача, тщательное тестирование |

### Шаг 4: Обновить patch/minor зависимости

```bash
# Через vcpkg — применить обновления
# ВНИМАНИЕ: vcpkg install НЕ обновляет уже установленные пакеты;
# для обновления используйте vcpkg upgrade --no-dry-run
vcpkg upgrade --no-dry-run

# Вручную — пример обновления SQLite amalgamation
# 1. Скачать новую версию с https://sqlite.org/download.html
# 2. Заменить third-party/sqlite/sqlite3.c и sqlite3.h
# 3. Обновить VERSION файл или комментарий в файле

# Вручную — пример обновления OpenSSL через vcpkg
vcpkg install openssl:x64-windows

# Проверить что всё собирается
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# Запустить тесты
ctest --test-dir build -C Debug --output-on-failure
```

### Шаг 5: Мажорные обновления — по одному

```bash
# Обновить ОДНУ зависимость
# Пример: обновление wxWidgets до новой minor версии

# 1. Скачать новую версию wxWidgets
# https://github.com/wxWidgets/wxWidgets/releases

# 2. Пересобрать wxWidgets
cd third-party/wxWidgets-3.4.0
./configure --enable-debug --with-gtk=3  # Linux

# Windows — используйте MSBuild через директорию build/msw/:
# (./configure НЕ работает на Windows без MSYS2/Cygwin)
# cd build\msw\
# msbuild wx_vc17.sln /p:Configuration=DLL Debug /p:Platform=x64 /m

# 3. Пересобрать OES
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# 4. Исправить breaking changes (если есть)
# Проверить wxWidgets Migration Guide

# 5. Запустить тесты
ctest --test-dir build --output-on-failure

# 6. Ручное тестирование — все основные функции!
# - Запустить приложение
# - Открыть несколько форм
# - Проверить designer
# - Проверить подключение к Firebird

# Если всё ок — следующая зависимость
```

**Почему по одному:** Если обновить всё сразу и что-то сломается — непонятно какая библиотека виновата. По одному — сразу видно причину.

### Шаг 6: Создать PR

```bash
git add third-party/sqlite/sqlite3.c third-party/sqlite/sqlite3.h
git add vcpkg.json vcpkg-lock.json
git commit -m "chore: update dependencies (March 2026)"
git push -u origin chore/update-deps-2026-03
```

Описание PR:

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

## Мажорные обновления фреймворков

Обновление wxWidgets, Firebird, Visual Studio или Windows SDK — это **отдельная задача**, не часть ежемесячного обновления.

### Процесс

1. **Создать задачу** в трекере: "Upgrade wxWidgets from 3.3 to 4.0"
2. **Изучить changelog и migration guide** — что изменилось, что ломается
3. **Создать ветку** `chore/upgrade-wxwidgets-4`
4. **Обновить** по официальному migration guide
5. **Исправить все compilation errors и warnings**
6. **Тесты** — все должны проходить
7. **Ручное тестирование** — полный обход основных функций
8. **PR** с подробным описанием что изменилось и что нужно проверить
9. **Staging** — задеплоить и протестировать на staging стенде
10. **Release** — только после полного тестирования

### Пример: обновление wxWidgets 3.3 → 3.4

```bash
# 1. Скачать wxWidgets 3.4.0
# https://github.com/wxWidgets/wxWidgets/releases

# 2. Изучить migration guide:
# https://docs.wxwidgets.org/latest/overview_changes.html

# 3. Обновить путь в проекте
# OES.vcxproj: изменить WX_DIR = third-party\wxWidgets-3.3.2
#          на: WX_DIR = third-party\wxWidgets-3.4.0

# 4. Пересобрать
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m
# Исправить все ошибки и предупреждения

# 5. Тесты
ctest --test-dir build -C Debug --output-on-failure

# 6. Ручное тестирование (чеклист)
# [ ] Главное окно открывается
# [ ] Все диалоги открываются корректно
# [ ] wxGrid работает (таблицы данных)
# [ ] Designer работает
# [ ] Файловые диалоги работают
# [ ] DnD (drag-and-drop) работает
# [ ] Печать работает
# [ ] Unicode отображается корректно
```

### Обновление Firebird клиента (fbclient.dll)

```bash
# При обновлении Firebird сервера у клиентов:
# 1. Проверить совместимость fbclient.dll версии с IBPP
# 2. Проверить совместимость wire protocol с новой версией сервера
# 3. Обновить fbclient.dll в дистрибутиве

# ВАЖНО: fbclient.dll должна соответствовать версии Firebird сервера
# Firebird 4.0 server + Firebird 3.0 fbclient.dll — может работать, но не гарантировано
```

---

## Фиксация версий для критичных зависимостей

```json
// vcpkg.json — фиксировать минимальные версии для критичных библиотек
{
    "name": "oes",
    "builtin-baseline": "a14b7c3d...",  // SHA коммита vcpkg — обязательно для воспроизводимых сборок
    "dependencies": [
        {
            "name": "wxwidgets",
            "version>=": "3.3.2"    // Минимальная версия
        },
        {
            "name": "openssl",
            "version>=": "3.2.1"    // Минимум с исправлением CVE-2024-XXXX
        },
        {
            "name": "gtest"         // Без фиксации — подходит любая
        }
    ]
}
```

**Когда фиксировать точную версию (не `>=`):**
- При известной несовместимости с более новой версией
- При критичной зависимости от конкретного поведения API
- В производственном релизе (для воспроизводимых сборок)

**Когда `>=` допустимо:**
- Инструменты разработки (Google Test, clang-tidy)
- Утилитарные библиотеки без ABI-ломающих изменений

---

## Безопасность зависимостей

### Проверка CVE в CI

```yaml
# .github/workflows/security-audit.yml
name: Security Audit

on:
  schedule:
    - cron: '0 9 * * 1'  # Каждый понедельник в 9:00
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
          # Проверить критичные библиотеки вручную
          OPENSSL_VERSION=$(grep '"version"' vcpkg.json | grep openssl | grep -oP '"\d+\.\d+\.\d+"')
          echo "OpenSSL version in vcpkg.json: $OPENSSL_VERSION"
          # Добавить логику проверки против известных CVE
```

### Если обнаружена критическая уязвимость

1. **Немедленно** создать ветку `fix/security-deps-CVE-XXXX`
2. Обновить уязвимую библиотеку до исправленной версии
3. `msbuild && ctest`
4. PR → ревью → мердж → новый release patch-версии
5. Уведомить клиентов о необходимости обновления
6. **Не ждать** ежемесячного обновления!

### Проверка цифровых подписей загружаемых библиотек

```powershell
# Windows: проверить подпись бинарного файла библиотеки
Get-AuthenticodeSignature "third-party\sqlite\sqlite3.dll"
# Status должен быть Valid

# Для загружаемых архивов — проверить SHA256
# Сайты библиотек публикуют хеши для проверки:

# Пример: проверка SQLite amalgamation
$expectedHash = "abc123..."  # Хеш с официального сайта
$actualHash = (Get-FileHash "sqlite-amalgamation-3450100.zip" -Algorithm SHA256).Hash
if ($expectedHash -ne $actualHash) {
    throw "SECURITY: Hash mismatch for SQLite download!"
}
```

---

## Visual Studio и Windows SDK

### Обновление Visual Studio

```
При выходе обновлений безопасности MSVC:
1. Help → Check for Updates → Update Visual Studio
2. После обновления: пересобрать проект
3. Проверить что нет новых предупреждений компилятора
4. Прогнать тесты: ctest --output-on-failure
5. Обновить .github/workflows/ если изменилась версия toolset:
   msbuild-version: 'latest'  или указать конкретную
```

### Обновление Windows SDK

```xml
<!-- OES.vcxproj — версия Windows SDK -->
<WindowsTargetPlatformVersion>10.0.22621.0</WindowsTargetPlatformVersion>

<!-- При обновлении SDK — изменить на новую версию -->
<!-- Проверить в: Project Properties → General → Windows SDK Version -->
```

---

## Lock файлы

### vcpkg-lock.json ВСЕГДА коммитить

```bash
# В .gitignore НЕ должно быть:
# vcpkg-lock.json   ← НЕПРАВИЛЬНО!

# vcpkg-lock.json ОБЯЗАТЕЛЬНО коммитится
git add vcpkg.json vcpkg-lock.json
```

**Почему:**
- Гарантирует одинаковые версии у всех разработчиков
- Гарантирует воспроизводимые сборки в CI/CD
- Без lock файла `vcpkg install` может установить разные версии

### third-party/ в git или нет?

```
# Текущий подход OES: часть зависимостей в third-party/ в git
# Преимущества: воспроизводимые сборки без дополнительного ПО
# Недостатки: большой размер репозитория

# Рекомендация: использовать git submodules или vcpkg для крупных зависимостей
# и оставить в third-party/ только небольшие header-only библиотеки

# SQLite amalgamation (два файла) — OK держать в git
# wxWidgets (тысячи файлов) — лучше git submodule или vcpkg
```

---

## Чеклист обновления

- [ ] Создана ветка `chore/update-deps-YYYY-MM`
- [ ] CVE проверка выполнена (osv-scanner или вручную)
- [ ] Зависимости с уязвимостями обновлены в первую очередь
- [ ] Patch/minor обновления применены
- [ ] `msbuild Debug x64` — собирается без ошибок и предупреждений
- [ ] `msbuild Release x64` — собирается без ошибок
- [ ] `ctest` — все тесты проходят
- [ ] Ручная проверка основных функций (если обновляли UI библиотеки)
- [ ] Major обновления — отдельными коммитами (если есть)
- [ ] `vcpkg-lock.json` или аналог закоммичен
- [ ] PR создан с описанием что обновлено и какие CVE закрыты
- [ ] Протестировано на staging после мерджа
