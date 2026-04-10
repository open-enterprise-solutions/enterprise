# 02. Git-воркфлоу

## Модель ветвления

Мы используем упрощённую модель Git Flow с двумя основными ветками и короткоживущими фича-ветками.

```
master (production / stable release)
 │
 ├── develop (staging / integration)
 │    │
 │    ├── feature/form-designer-undo-redo
 │    ├── feature/postgresql-support
 │    ├── fix/firebird-connection-leak
 │    └── hotfix/critical-crash-on-open
 │
 └── hotfix/security-patch (прямо от master, в критических случаях)
```

### Основные ветки

| Ветка | Назначение | Деплой | Защита |
|-------|-----------|--------|--------|
| `master` | Стабильный код, релизные теги | Production сборка | Protected: нет прямых push, только merge из develop |
| `develop` | Интеграционная ветка | CI сборка + тесты | Protected: нет прямых push, только merge из feature/* |

### Рабочие ветки

| Тип | Формат | Создаётся от | Мерджится в | Пример |
|-----|--------|-------------|-------------|--------|
| `feature/*` | feature/описание | develop | develop | `feature/add-grid-sorting` |
| `fix/*` | fix/описание | develop | develop | `fix/firebird-null-pointer` |
| `hotfix/*` | hotfix/описание | master | master + develop | `hotfix/crash-on-report-open` |
| `refactor/*` | refactor/описание | develop | develop | `refactor/database-layer-cleanup` |
| `chore/*` | chore/описание | develop | develop | `chore/migrate-to-cmake` |

### Именование веток

Правила:
- **Только lowercase**
- **Kebab-case** (слова через дефис)
- **Описательно** — из названия понятно, что делает ветка
- **Английский язык**

Хорошо:
```
feature/form-designer-grid-snap
fix/firebird-transaction-rollback
hotfix/wx-assert-on-startup
refactor/extract-query-builder
chore/add-gtest-framework
```

Плохо:
```
feature/task-123          — непонятно что делает
fix_something             — подчёркивание вместо дефиса
Feature/NewForm           — не lowercase
my-branch                 — нет типа, непонятно
```

---

## Формат коммитов

### Конвенция

```
type: краткое описание на английском
```

### Типы коммитов

| Тип | Когда использовать | Пример |
|-----|--------------------|--------|
| `feat` | Новая функциональность | `feat: add undo/redo support in form designer` |
| `fix` | Исправление бага | `fix: resolve null pointer in firebird connection pool` |
| `refactor` | Рефакторинг (без изменения поведения) | `refactor: extract query builder into separate class` |
| `docs` | Документация | `docs: update build instructions for CMake` |
| `style` | Форматирование (без изменения логики) | `style: fix indentation in databaseLayer.cpp` |
| `test` | Тесты | `test: add unit tests for ibDatabaseLayerFirebird` |
| `chore` | Рутина (зависимости, конфиги, сборка) | `chore: add CMakeLists.txt for core module` |
| `perf` | Оптимизация производительности | `perf: cache prepared statements in query executor` |
| `build` | Изменения системы сборки | `build: migrate database module to CMake` |

### Правила

1. **Язык коммитов: English** — всегда, без исключений
2. **Первая буква после типа — строчная**: `feat: add ...`, не `feat: Add ...`
3. **Без точки в конце**: `feat: add grid control`, не `feat: add grid control.`
4. **Императивное наклонение**: `add`, `fix`, `update`, не `added`, `fixed`, `updated`
5. **Краткость**: до 72 символов в первой строке
6. **БЕЗ строк Co-Authored-By от AI** — мы не указываем AI как соавторов

### Тело коммита (опционально)

Если нужно объяснение — добавьте тело через пустую строку:

```
fix: resolve memory leak in Firebird connection pool

Connections were not returned to the pool when a query threw
an exception. Wrapped connection acquisition in RAII guard
(ConnectionGuard) to ensure release in all code paths.

Closes #87
```

### Что НЕ допускается

```bash
# Плохие коммиты:
"fix"                                    — непонятно что
"WIP"                                    — не коммитьте незавершённое
"fixed stuff"                            — неинформативно
"feat: add feature"                      — тавтология
"update"                                 — что обновлено?
"Co-Authored-By: Claude <...>"           — запрещено
"fix: Исправлен краш при открытии"      — русский язык
```

---

## Pull Requests

### Создание PR

1. **Всегда из рабочей ветки в `develop`** (кроме hotfix → master)
2. **Описание обязательно** — что сделано, зачем, как проверить
3. **Один PR = одна задача/фича** — не комбинируйте несвязанные изменения

### Шаблон PR

```markdown
## What
Краткое описание изменений (1-2 предложения).

## Why
Зачем нужны эти изменения (ссылка на задачу/issue).

## How to test
1. Собрать проект в конфигурации Debug (x64)
2. Открыть форму с [описание]
3. Выполнить [действие]
4. Ожидаемый результат: [описание]

## Screenshots (if UI changes)
До/После скриншоты если изменился интерфейс.

## Checklist
- [ ] Код компилируется без ошибок и предупреждений (Debug + Release, x64)
- [ ] Тесты проходят (ctest)
- [ ] Нет утечек памяти (проверено Valgrind/Dr.Memory или ASAN)
- [ ] Нет сырых owning указателей (new без smart pointer)
- [ ] Конфиги с credentials не попали в коммит
- [ ] CLAUDE.md обновлён если изменилась архитектура
```

### Процесс мерджа

1. Автор создаёт PR
2. Ревьюер проверяет (см. [03-code-review.md](./03-code-review.md))
3. Автор исправляет замечания
4. Ревьюер одобряет (Approve)
5. **Squash merge** — все коммиты из ветки сжимаются в один
6. **Удаление ветки** после мерджа — автоматически или вручную

### Почему squash merge

- Чистая история в `develop` и `master`
- Каждый коммит в основных ветках = одна завершённая фича/фикс
- Легко откатить одним `git revert`

Настройка в GitHub: Settings → General → Pull Requests → Allow squash merging (только)

---

## Процесс релиза

### Develop → Master (релиз)

```
1. Убедиться что develop стабилен (CI зелёный, тесты проходят)
2. Проверить Release сборку вручную на чистой машине
3. Создать PR: develop → master
4. Описание: список изменений с прошлого релиза (CHANGELOG)
5. Ревью и approve
6. Merge (обычный merge, не squash — сохраняем историю)
7. Создать тег: vX.Y.Z
8. Собрать инсталлятор и загрузить в GitHub Releases
```

### Версионирование (Semantic Versioning)

```
vMAJOR.MINOR.PATCH

v1.0.0 — первый стабильный релиз
v1.1.0 — новые фичи (backwards compatible)
v1.1.1 — багфиксы
v2.0.0 — breaking changes (изменение публичного API плагинов)
```

### Создание тега

```bash
git checkout master
git pull origin master
git tag -a v1.2.0 -m "Release v1.2.0: add PostgreSQL support, fix Firebird connection leak"
git push origin v1.2.0
```

---

## Hotfix процесс

Для критических багов в production, которые нельзя ждать:

```
1. Создать ветку hotfix/описание от master
2. Исправить баг
3. PR: hotfix → master
4. После мерджа в master — создать PR: master → develop (чтобы фикс попал в develop)
5. Собрать hotfix-релиз
6. Создать тег vX.Y.Z+1
```

---

## Запрещённые действия

### Категорически запрещено:

| Действие | Почему |
|----------|--------|
| `git push --force` в master или develop | Ломает историю для всей команды |
| Коммит файлов с credentials (пароли БД, API-ключи) | Утечка секретов |
| Коммит бинарных артефактов сборки (`.obj`, `.exe`, `.dll`) | Огромный размер, не нужны в git |
| Коммит `build/` и `Debug/`/`Release/` директорий | Build artifacts не хранятся в git |
| Прямой push в master или develop | Обход код-ревью |
| Коммит `.vs/` директории Visual Studio | Локальные IDE-настройки |

### Как защитить ветки в GitHub

Settings → Branches → Branch protection rules:

Для `master`:
- Require pull request reviews (1 approval)
- Require status checks to pass (CI build + tests)
- Do not allow force pushes
- Do not allow deletions

Для `develop`:
- Require pull request reviews (1 approval)
- Do not allow force pushes

---

## Типичный рабочий день

```bash
# 1. Начало работы: обновить develop
git checkout develop
git pull origin develop

# 2. Создать ветку для задачи
git checkout -b feature/add-report-export-pdf

# 3. Работать, коммитить (небольшими логическими шагами)
git add src/engine/backend/report/ibValueReportExporterPdf.h
git add src/engine/backend/report/ibValueReportExporterPdf.cpp
git commit -m "feat: add PDF report exporter class skeleton"

git add src/engine/backend/metaCollection/partial/commonObjectQuery.cpp
git commit -m "feat: integrate PDF exporter into report metadata query"

# 4. Пушнуть ветку
git push -u origin feature/add-report-export-pdf

# 5. Создать PR в GitHub: feature/add-report-export-pdf → develop

# 6. После ревью и approve — squash merge в develop

# 7. Локально обновить develop
git checkout develop
git pull origin develop

# 8. Удалить локальную ветку
git branch -d feature/add-report-export-pdf
```

---

## Решение конфликтов

Если ваша ветка конфликтует с `develop`:

```bash
# Вариант 1: Rebase (предпочтительно для маленьких веток)
git checkout feature/my-branch
git fetch origin
git rebase origin/develop
# Решить конфликты в файлах .h/.cpp
git add .
git rebase --continue
git push --force-with-lease  # force-with-lease, не --force!

# Вариант 2: Merge develop в ветку (для больших веток)
git checkout feature/my-branch
git fetch origin
git merge origin/develop
# Решить конфликты
git add .
git commit -m "chore: merge develop into feature branch"
git push
```

**Важно:** `--force-with-lease` безопаснее чем `--force` — он проверяет, что никто не пушил в ветку после вас.

### Конфликты в .vcxproj / CMakeLists.txt

Конфликты в файлах проекта Visual Studio (`.vcxproj`) случаются часто при параллельной работе. Правило:

1. Открыть конфликтующий `.vcxproj` в текстовом редакторе
2. В секции `<ItemGroup>` принять **оба** набора файлов (взять изменения из обеих сторон)
3. Отсортировать записи в алфавитном порядке для минимизации будущих конфликтов
4. Для CMakeLists.txt — аналогично: добавить все файлы из обеих сторон
