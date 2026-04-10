# 06. AI-агенты

## Философия

AI (Claude Code, Claude Desktop, ChatGPT) — это **партнёр по разработке**, а не автономный разработчик. AI пишет черновик, человек доводит до продакшн-качества. AI ускоряет работу, но не заменяет инженерное мышление.

Мы активно используем AI в повседневной работе и считаем это конкурентным преимуществом. Но у этого подхода есть чёткие правила.

---

## 10 правил работы с AI

### 1. Человек всегда ревьюит код AI

Никакой AI-код не попадает в `develop` или `master` без ревью человеком. AI галлюцинирует — уверенно использует несуществующие API, создаёт логические ошибки, игнорирует контекст проекта. Человек — последний рубеж перед продакшном.

**Практика:** Прежде чем коммитить AI-код, прочитайте каждую строку. Не просто "выглядит нормально", а реально проверьте: существуют ли заголовочные файлы? Правильная ли логика управления памятью? Нет ли UB?

### 2. Не доверять слепо — проверять API, логику, управление памятью

Типичные ошибки AI в C++ коде, которые нужно ловить:

```cpp
// AI может написать несуществующий API:
std::string::trim();                    // Не существует в стандарте
std::filesystem::read_all_text(path);   // Не существует

// AI может написать небезопасный код:
std::string query = "SELECT * FROM users WHERE id = " + userId; // SQL injection!
char buf[256]; strcpy(buf, userInput.c_str());                  // Buffer overflow!

// AI может использовать устаревшие паттерны:
std::auto_ptr<MyClass> ptr(new MyClass());  // auto_ptr удалён в C++17
if (ptr == NULL) ...                         // Использовать nullptr

// AI может ошибиться с wxWidgets API:
wxGrid::SetCellValue(row, col, value);      // Неверная сигнатура
wxString::Format("%s", str);                // Ошибка: %s требует const char*, не wxString
                                            // Правильно: wxString::Format("%s", str.c_str())
                                            // или используйте wxString::Format("%ls", str.wc_str())
```

**Практика:** Если видите незнакомый метод или класс — откройте cppreference.com или документацию wxWidgets и проверьте. AI очень уверенно "врёт".

### 3. AI не коммитит напрямую — человек создаёт коммит

AI-инструменты (Claude Code) умеют создавать коммиты. Мы этим **не пользуемся**. Человек всегда:
1. Просматривает изменения (`git diff`)
2. Решает что включить в коммит
3. Пишет осмысленное сообщение
4. Создаёт коммит сам

Исключение: AI может предложить текст коммита, но человек его проверяет и создаёт коммит.

### 4. Без Co-Authored-By в коммитах

Мы **не указываем AI как соавтора** в коммитах. Никаких строк вроде:
```
Co-Authored-By: Claude <noreply@anthropic.com>
Co-Authored-By: ChatGPT <noreply@openai.com>
```

Причины:
- Ответственность за код несёт человек, не AI
- Засоряет историю git
- Не несёт полезной информации

### 5. CLAUDE.md — главный файл контекста

CLAUDE.md — самый важный файл для работы с AI. Обновляйте его при:
- Добавлении нового модуля или подсистемы
- Изменении структуры проекта
- Новых бизнес-правилах
- Изменении команд сборки
- Новых зависимостях или конфигурационных файлах

Хороший CLAUDE.md экономит 15-30 минут на каждой сессии с AI. Плохой (или отсутствующий) — заставляет повторно объяснять контекст каждый раз.

Структура CLAUDE.md описана в [01-project-structure.md](./01-project-structure.md).

### 6. .claude.json — MCP подключения

Файл `.claude.json` позволяет Claude Code подключаться к внешним сервисам через MCP (Model Context Protocol):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/enterprise"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_TOKEN": "ghp_your_token"
      }
    }
  }
}
```

Это даёт AI доступ к:
- Файловой системе проекта (для навигации по коду)
- GitHub API (для работы с issues, PR)

**Важно:** Если `.claude.json` содержит реальные credentials — добавьте в `.gitignore`. Можно коммитить `.claude.json.example` без реальных токенов.

### 7. Специализированные агенты

В CLAUDE.md можно определить "роли" для AI — специализированные промпты для конкретных задач:

```markdown
## AI Agents

### Security Scanner
Проверь код на уязвимости: SQL-инъекции (конкатенация строк в SQL),
переполнения буферов (небезопасные функции strcpy/sprintf),
хардкод credentials, незащищённый доступ к файлам.

### Code Reviewer
Проверь PR на соответствие стандартам C++17:
- Нет raw owning pointers (new без smart pointer)
- Все ресурсы управляются через RAII
- const-correctness (методы без побочных эффектов объявлены const)
- explicit для конструкторов с одним параметром
- Используй чеклист из 03-code-review.md

### Test Writer (Google Test)
Напиши тесты для указанного класса/метода.
Используй Google Test (gtest/gtest.h).
Структура: TEST(ClassName, MethodName_Condition_ExpectedResult).
Тестируй публичный интерфейс, изолируй зависимости через mock-объекты.
Для DB-кода: MockDatabaseLayer наследует ibDatabaseLayer, мокируй PrepareStatement.
Параметризованные запросы: SetParamString/SetParamInt — НЕ конкатенация строк.

### CMake Writer
Сгенерируй CMakeLists.txt для указанного модуля.
Требования: C++17, target-based подход (target_sources, target_include_directories),
совместимость с Visual Studio 2017+ и GCC 9+.
```

### 8. AI говорит по-русски с командой, код на English

| Контекст | Язык |
|----------|------|
| Обсуждение задачи с AI | Russian |
| Код, написанный AI | English |
| Комментарии в коде от AI | English |
| Коммиты | English |
| Документация (README, CLAUDE.md) | English |

Промпты AI можно писать на русском — AI понимает и отвечает на том же языке. Но весь код и техническая документация — только English.

### 9. Промпты для AI должны быть конкретными

Плохой промпт:
```
Добавь экспорт в PDF
```

Хороший промпт:
```
Создай класс ibValueReportExporterPdf для экспорта отчётов в PDF в OES Enterprise:
- Файл: src/engine/backend/report/ibValueReportExporterPdf.h + .cpp
- Зависимость: libharu (hpdf.h) — уже подключена в проекте
- Метод: bool Export(ibDatabaseLayer* db, int reportMetaId, const wxString& filePath)
- RAII: unique_ptr для внутренних ресурсов
- Параметризованные запросы: ibPreparedStatement + SetParamString/SetParamInt
- Транзакции: ibTransactionGuard (RAII rollback)
- Обработка ошибок: бросать ibBackendCoreException при ошибке libharu или БД
- Нет C++ namespace (исторический стиль проекта, префикс ib)

Текущая структура: src/engine/backend/metaCollection/partial/commonObjectQuery.cpp
содержит CreateAndUpdateTableDB(). C++17, компилятор MSVC 2017+.
```

**Правило:** Чем больше контекста в промпте, тем лучше результат. Не экономьте на описании.

### 10. Результат AI — черновик, не финальная версия

AI-код — это отправная точка. После получения результата от AI:

1. **Прочитайте** весь код, не бегло, а внимательно
2. **Проверьте** заголовочные файлы (реально ли они существуют?) и API-вызовы
3. **Скомпилируйте** — убедитесь что компилируется без предупреждений
4. **Запустите** — убедитесь что работает на реальных данных
5. **Доработайте** — адаптируйте под специфику проекта
6. **Удалите лишнее** — AI часто добавляет ненужные комментарии и избыточный код

---

## Паттерны использования AI

### Scaffolding: AI создаёт скелет, человек доводит

Лучший способ использовать AI — для создания начальной структуры:

```
Промпт: "Создай скелет нового типа метаобъекта для OES:
- src/engine/backend/metaCollection/ — новый класс ibValueMetaObjectReport
- Базовый класс: ibValueMetaObject (аналогично ibValueMetaObjectCatalog)
- Методы: CreateAndUpdateTableDB() — создание/обновление таблицы в БД
- Параметризованные запросы через ibPreparedStatement + SetParamString/SetParamInt
- Транзакции через ibTransactionGuard (RAII rollback)
- Исключения: ibBackendCoreException при ошибках БД"
```

AI быстро создаст структуру. Человек затем:
- Проверяет совместимость с ibDatabaseLayer API (реальные сигнатуры методов)
- Проверяет что ibPreparedStatement::SetParamString/SetParamInt существуют
- Добавляет реальную логику работы с метаданными
- Пишет unit-тесты с MockDatabaseLayer
- Проверяет что ibTransactionGuard корректно вызывается

### Code Review: AI как первый ревьюер

Перед отправкой PR на ревью человеку — попросите AI проверить:

```
Промпт: "Проверь этот C++ код на:
1. Утечки памяти и ресурсов (RAII, smart pointers)
2. Потенциальные UB (uninitialized variables, out-of-bounds, null deref)
3. SQL injection и другие уязвимости
4. Нарушения const-correctness
5. Соответствие C++17 (нет устаревших конструкций)"
```

AI хорошо находит очевидные проблемы и экономит время ревьюера-человека.

### Debugging: AI анализирует ошибку, предлагает решения

```
Промпт: "Access Violation в ReportRenderer::RenderRow() при пустом датасете.
Стектрейс: [стектрейс из Visual Studio].
Вот код метода: [код].
Вот класс ReportDocument (датасет): [код].
Что не так и как исправить?"
```

AI хорошо анализирует ошибки, особенно если предоставить достаточно контекста (стектрейс, код, тип СУБД).

### Написание тестов: AI генерирует, человек проверяет

```
Промпт: "Напиши Google Test для ibDatabaseLayerFirebird:
- Тест на успешный SELECT через ibPreparedStatement + SetParamString/SetParamInt
- Тест на параметризованный запрос (защита от SQL injection)
- Тест на ошибку при некорректном SQL (ожидаем ibBackendCoreException)
- Тест на поведение при потере соединения
- Транзакции через ibTransactionGuard (проверить rollback при отсутствии Commit)
Используй MockDatabaseLayer (mock ibDatabaseLayer) для unit-тестов.
Для integration-тестов — реальный ibDatabaseLayerFirebird с тестовой БД."
```

### Refactoring: AI предлагает улучшения, человек одобряет

```
Промпт: "commonObjectQuery.cpp вырос до 800 строк. Предложи как его разбить
на несколько классов, сохраняя текущий интерфейс (ibDatabaseLayer + ibPreparedStatement).
Покажи план рефакторинга по шагам с названиями новых классов.
Обрати внимание: транзакции через ibTransactionGuard, запросы через SetParamString/SetParamInt."
```

AI предлагает архитектуру, человек оценивает и принимает решение.

### CMake миграция: AI помогает с синтаксисом

```
Промпт: "Конвертируй эту секцию .vcxproj в CMakeLists.txt:
[содержимое ItemGroup из .vcxproj]
Требования: C++17, MSVC 2017+, target-based CMake, совместимо с vcpkg."
```

---

## Безопасность данных при работе с AI

### Категорически запрещено отправлять в AI:

| Данные | Пример | Что делать |
|--------|--------|------------|
| Строки подключения к БД | `SYSDBA:masterkey@server/database.fdb` | Замените на `<DB_CONNECTION_STRING>` |
| API ключи и токены | GitHub token, лицензионные ключи | Замените на `<REDACTED>` |
| Персональные данные из БД | Имена, email, телефоны реальных пользователей | Замените на фейковые данные |
| Содержимое config.ini | Реальные пароли и хосты | Используйте config.ini.example |
| SSH ключи | Содержимое ~/.ssh/ | Никогда |

### Правила работы с данными

1. **Маскируйте конфиденциальные данные перед отправкой в AI:**
```
# Плохо:
"Ошибка соединения: host=192.168.1.100 user=admin password=secret123"

# Хорошо:
"Ошибка соединения с Firebird, конфиг: host=<HOST> user=<USER>"
```

2. **Используйте фейковые данные в примерах:**
```
# Плохо:
"Вот config.ini: Password=MyRealPassword123"

# Хорошо:
"Вот структура config.ini (значения заменены): Password=<your-password>"
```

3. **Не используйте production БД через MCP:**
`.claude.json` должен указывать только на **локальную** БД разработки, никогда на production.

---

## Настройка рабочего окружения для AI

### Claude Code CLI

```bash
# Предварительное требование: Node.js 18+
# Скачать с https://nodejs.org/ (рекомендуется LTS-версия)
# Проверить установку:
node --version   # должно быть v18.0.0 или выше
npm --version

# Установка
npm install -g @anthropic-ai/claude-code

# Запуск в проекте
cd /path/to/enterprise
claude

# Claude автоматически прочитает CLAUDE.md и .claude.json
```

### Claude Desktop

Добавьте MCP серверы в `~/Library/Application Support/Claude/claude_desktop_config.json` (macOS) или `%APPDATA%\Claude\claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "C:/Projects/enterprise"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_TOKEN": "ghp_your_token"
      }
    }
  }
}
```

### Общие правила для AI-инструментов

1. Начинайте сессию с контекста (CLAUDE.md читается автоматически в Claude Code)
2. Один промпт — одна задача (не просите AI сделать 10 вещей сразу)
3. Проверяйте результат перед следующим промптом
4. Если AI ушёл не туда — остановите и переформулируйте задачу
5. Сохраняйте удачные промпты в `docs/ai-prompts/` для повторного использования
6. При работе с незнакомым API (wxWidgets, Firebird IBPP) — всегда указывайте AI проверять актуальную документацию
