# 05. Управление задачами

## Трекер задач

Каждый проект использует **один** трекер задач. Выбор зависит от проекта:

| Трекер | Когда использовать |
|--------|--------------------|
| **GitHub Issues** | Основной трекер для OES. Технические задачи рядом с кодом, интеграция с PR |
| **Jira** | Для проектов с полноценным Scrum, спринтами и story points |
| **Linear** | Альтернатива Jira для команд предпочитающих минимализм |

### GitHub Issues — основной трекер

**Структура задач в GitHub Issues:**
- **Milestones:** крупные релизы / направления работ (v1.1.0, CMake Migration, Google Test Integration)
- **Labels:** тип задачи, приоритет, компонент
- **Assignees:** ответственный разработчик

**Связь Issues → GitHub:**
- Ветка содержит номер задачи: `feature/42-pdf-export`
- PR title содержит ссылку: `feat: add PDF export (#42)`
- В описании PR: `Closes #42` — GitHub автоматически закроет issue при мердже

**Правило:** один проект = один трекер. Не размазывайте задачи по нескольким инструментам. Все участники проекта должны знать, где искать задачи.

---

## Жизненный цикл задачи

```
Backlog → To Do → In Progress → Review → Done
```

### Статусы

| Статус | Значение | Кто переводит |
|--------|----------|---------------|
| **Backlog** | Задача создана, но не запланирована | Автор задачи |
| **To Do** | Запланирована на текущую неделю/спринт | Тимлид |
| **In Progress** | Разработчик работает над задачей | Разработчик (берёт задачу) |
| **Review** | PR создан, ждёт ревью | Разработчик (создаёт PR) |
| **Done** | PR смерджен, задача выполнена | Ревьюер (после мерджа) |

### Правила перехода

- Разработчик берёт задачу из **To Do** → переводит в **In Progress**
- Не больше **2 задач In Progress** на одного разработчика одновременно
- Если задача заблокирована — пометить лейблом `blocked` с описанием причины
- Задача считается **Done** только когда PR смерджен (не когда код написан)

---

## Формат задачи

### Заголовок

Формат: **глагол + объект**

Хорошо:
```
Add undo/redo support in form designer
Fix null pointer crash when opening report with empty dataset
Implement PDF export for reports
Refactor database layer to support connection pooling
Migrate core module build system to CMake
```

Плохо:
```
Registration          — нет глагола, неясно что делать
Bug fix               — не описывает проблему
New feature           — не описывает что за фича
Task #42              — бессмысленное название
Crash                 — нет контекста
```

### Тело задачи

```markdown
## Description
При открытии отчёта, в датасете которого нет ни одной записи, приложение
падает с Access Violation в `ReportRenderer::RenderRow()`. Происходит при
любом типе СУБД (Firebird, PostgreSQL).

## Expected behavior
Отчёт должен открываться и отображать пустую страницу с заголовком,
но без строк данных.

## Steps to reproduce
1. Открыть любой отчёт
2. Убедиться что датасет пустой (нет ни одной записи)
3. Нажать "Просмотр отчёта"
4. Наблюдается: Access Violation crash

## Environment
- OS: Windows 10 x64
- Build: Debug x64, commit abc1234
- Database: Firebird 4.0.1

## Acceptance criteria
- [ ] Отчёт открывается без краша при пустом датасете
- [ ] Отображается корректная пустая страница
- [ ] Добавлен unit-тест в tests/unit/runtime/ для этого сценария
- [ ] Нет регрессий в остальных тестах

## Technical notes (опционально)
Возможная причина: `ReportRenderer::RenderRow()` обращается к `m_dataset[0]`
без проверки `m_dataset.empty()`. Проверить `ReportRenderer.cpp` ~строка 234.
```

---

## Связь задача → ветка → PR

Каждая задача должна быть связана с веткой и PR:

```
Задача #42: "Add PDF export for reports"
    ↓
Ветка: feature/42-pdf-export
    ↓
PR: "feat: add PDF export for reports (#42)" → Closes #42
```

### Как связать

В описании PR используйте ключевые слова:
```
Closes #42
Fixes #42
Resolves #42
```

GitHub автоматически закроет issue при мердже PR.

В имени ветки добавляйте номер задачи:
```
feature/42-add-pdf-export
fix/108-null-pointer-empty-dataset
chore/55-migrate-core-to-cmake
```

---

## Приоритеты

| Приоритет | Значение | SLA (время реакции) | Пример |
|-----------|----------|---------------------|--------|
| **Critical** | Краш у клиента, потеря данных, невозможно работать | Немедленно | Access violation при старте, потеря документов |
| **High** | Важная функциональность сломана, обходного пути нет | В течение дня | Невозможно сохранить форму, отчёт не генерируется |
| **Medium** | Функциональность работает с ограничениями | В текущем спринте | Медленная загрузка больших отчётов |
| **Low** | Косметика, улучшения, tech debt | Когда будет время | Иконка отображается со сдвигом на HiDPI |

### Правила приоритизации

1. **Critical** и **High** — берутся в работу без ожидания следующего спринта
2. **Medium** — планируются на текущую или следующую неделю
3. **Low** — делаются когда нет задач выше приоритетом
4. Тимлид расставляет приоритеты, разработчик может предложить изменение с аргументацией

---

## Лейблы / теги

Используйте лейблы для быстрой фильтрации:

| Лейбл | Цвет | Значение |
|-------|------|----------|
| `bug` | Красный | Баг в существующей функциональности |
| `feature` | Зелёный | Новая функциональность |
| `enhancement` | Голубой | Улучшение существующего |
| `refactor` | Жёлтый | Рефакторинг без изменения поведения |
| `docs` | Серый | Документация |
| `security` | Тёмно-красный | Вопросы безопасности |
| `blocked` | Оранжевый | Задача заблокирована |
| `good first issue` | Фиолетовый | Подходит для нового участника |
| `build` | Светло-серый | Система сборки (CMake, MSBuild, CI) |
| `database` | Тёмно-синий | Работа с СУБД или схемой |
| `performance` | Светло-оранжевый | Проблемы производительности |

---

## Milestones — планирование релизов

Milestones в GitHub соответствуют версиям продукта:

```
Milestone: v1.2.0
Due date: 2026-06-01
Description: Google Test integration, CMake migration for core module, PostgreSQL improvements

Issues:
  #55 Migrate core module to CMake
  #56 Add Google Test to build system
  #57 Write unit tests for ibDatabaseLayer (ibDatabaseLayerFirebird)
  #58 Fix PostgreSQL connection pool leak in ibDatabaseLayerPostgres
  #59 Add connection retry logic
```

Каждый новый issue должен быть привязан к milestone или помечен как Backlog.

---

## AI и задачи

### AI может создавать задачи

Claude Code и другие AI-ассистенты могут создавать задачи (через GitHub Issues API или MCP). Это полезно когда AI находит баг или видит улучшение в процессе работы.

**Но:** AI-созданная задача — это **черновик**. Человек должен:
1. Проверить релевантность задачи
2. Уточнить описание и критерии приёмки
3. Выставить приоритет
4. Привязать к milestone или спринту

### AI как помощник при декомпозиции

AI хорошо помогает разбить большую задачу на подзадачи:

```
Промпт: "Разбей задачу 'Integrate Google Test into OES build system' на подзадачи.
Проект: C++17, MSBuild + CMake transition, на данный момент тестов нет."

AI предложит:
1. Add Google Test as git submodule or vcpkg dependency
2. Create CMakeLists.txt for tests/ directory
3. Set up test runner configuration (CTest)
4. Write first smoke test to validate setup
5. Add unit tests for ibDatabaseLayer (connection, ibPreparedStatement execution)
6. Add unit tests for ibDatabaseResultSet (data access, type conversion)
7. Configure GitHub Actions to run tests on PR
8. Document how to run tests locally in README
```

Человек проверяет, корректирует, создаёт задачи.

### AI как помощник при написании acceptance criteria

```
Промпт: "Напиши acceptance criteria для задачи:
'Fix crash when opening report with empty dataset in ReportRenderer'"

AI предложит:
- [ ] No crash (Access Violation) when dataset has zero rows
- [ ] Empty report displays header/footer but no data rows
- [ ] No crash with NULL dataset pointer
- [ ] Handles case where dataset exists but all rows are filtered out
- [ ] Unit test added: ReportRendererTest.EmptyDataset
- [ ] No regression in existing report tests
- [ ] Verified on Firebird and PostgreSQL backends
```

---

## Ретроспектива задач

Раз в 2 недели (или в конце спринта/milestone):
1. Посмотреть что было сделано (Done)
2. Что застряло и почему (Blocked, долго в Review)
3. Что не успели (осталось в To Do / In Progress)
4. Что можно улучшить в процессе

Цель — не обвинять, а улучшать процесс. Если задачи постоянно не успевают — значит планируем слишком много или задачи слишком крупные.

---

## Работа с техническим долгом

Технический долг — отдельная категория задач. Правила:

1. **Фиксируйте** — при обнаружении создайте issue с лейблом `refactor` или `enhancement`
2. **Не игнорируйте** — Tech debt накапливается и замедляет разработку
3. **Планируйте** — выделяйте 10-20% каждого спринта на tech debt
4. **Не рефакторите молча** — рефакторинг идёт через PR и ревью как любой другой код

Примеры задач tech debt для OES:
```
refactor: Replace raw owning pointers in ibDatabaseLayer subclasses with unique_ptr
chore: Migrate designer module from MSBuild to CMake
test: Add unit tests for ibDatabaseResultSet class (currently 0% coverage)
refactor: Extract SQL query strings from business logic into constants
build: Enable /W4 warnings and fix all warning-as-errors
```
