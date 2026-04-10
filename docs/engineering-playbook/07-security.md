# 07. Безопасность

## Главное правило

> **Если сомневаешься — это небезопасно.** Лучше перестраховаться, чем объяснять клиентам почему утекли их данные.

---

## Секреты и credentials

### НИКОГДА не коммитить

| Что | Примеры |
|-----|---------|
| Файлы конфигурации с паролями | `config.ini`, `settings.cfg`, `*.conf` с реальными данными |
| Ключи и сертификаты | `*.pem`, `*.key`, `*.crt`, `*.p12`, `*.pfx` |
| SSH ключи | `id_rsa`, `id_ed25519` |
| Строки подключения к БД | DSN с логином/паролем |
| Пароли | Любые пароли в любом виде (включая хардкод в .cpp/.h) |
| Дампы БД | `*.sql`, `*.fdb`, `*.fbk` с реальными данными |
| Лицензионные ключи | Серийные номера, коды активации |

### .gitignore — обязательный минимум

```gitignore
# Файлы конфигурации с реальными данными — НИКОГДА в git
config.ini
settings.ini
*.local.ini
*.local.cfg

# Keys and certificates
*.pem
*.key
*.crt
*.p12
*.pfx
*.cer

# Build output
build/
Release/
Debug/
x64/
x86/
*.obj
*.pch
*.ilk
*.pdb
*.exp
*.lib

# IDE
.vs/
*.user
*.suo
*.aps
*.ncb
*.sdf
*.opensdf
.idea/

# OS
.DS_Store
Thumbs.db
desktop.ini

# Logs
*.log
logs/

# Database dumps and files
*.sql
*.fdb
*.fbk
*.dump

# Temp / cache
tmp/
.cache/
ipch/
```

### Что делать если секрет попал в git

Паника обоснована. Действуйте немедленно:

1. **Ротация секрета** — измените скомпрометированный пароль/ключ/строку подключения ПРЯМО СЕЙЧАС
2. **Удаление из истории** (если репозиторий публичный):
```bash
# Установите git-filter-repo
pip install git-filter-repo

# Удалите файл из всей истории
git filter-repo --path config.ini --invert-paths

# Force push (единственный допустимый случай)
git push --force --all
```
3. **Проверка** — убедитесь что секрет недоступен ни в одном коммите
4. **Уведомление** — сообщите команде о компрометации

**Важно:** Даже если вы удалите файл обычным `git rm`, он останется в истории git. Используйте `git filter-repo` для полного удаления.

---

## Хранение конфигурации и секретов

### Конфигурационные файлы

OES читает параметры подключения к БД и настройки приложения из INI-подобных файлов. Основной принцип: файл с реальными данными — вне git, шаблон — в git.

```ini
; config.ini.example — в git (шаблон без реальных значений)
[database]
type=firebird
host=localhost
port=3050
database=/path/to/database.fdb
user=SYSDBA
password=

[app]
log_level=info
```

```bash
# Права доступа на файл конфигурации (Linux/macOS)
chmod 600 config.ini

# Проверить
ls -la config.ini
# -rw------- 1 deploy deploy 512 Jan 15 10:30 config.ini
```

### Переменные окружения как альтернатива

Для CI/CD и контейнеризованных сред используйте переменные окружения:

```cpp
// Чтение секретов из переменных окружения
#include <cstdlib>
#include <stdexcept>

std::string getRequiredEnv(const char* name) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        throw std::runtime_error(
            std::string("Required environment variable not set: ") + name
        );
    }
    return value;
}

// Использование
std::string dbPassword = getRequiredEnv("OES_DB_PASSWORD");
```

### Иерархия конфигурационных файлов

| Файл | Среда | В git? |
|------|-------|--------|
| `config.ini.example` | Шаблон | Да |
| `config.ini` | Локальная разработка | Нет |
| `config.staging.ini` | Staging сервер | Нет |
| `config.production.ini` | Production | Нет |

### Генерация случайных паролей

```bash
# Пароль БД (24 символа)
openssl rand -base64 24

# Ключ шифрования (32 байта hex)
openssl rand -hex 32
```

### Ротация секретов

- **Скомпрометированные пароли** — менять немедленно (в течение часа)
- **Плановая ротация** — раз в квартал для критичных паролей БД
- **При увольнении сотрудника** — менять все пароли, к которым он имел доступ

---

## SSH доступ к серверам

### Только ключи, никогда пароли

```bash
# На сервере: отключить аутентификацию по паролю
# /etc/ssh/sshd_config
PasswordAuthentication no
PubkeyAuthentication yes
PermitRootLogin no
```

### Каждый участник — свой ключ

```bash
# Создание ключа (на своей машине)
ssh-keygen -t ed25519 -C "name@company.com"

# Добавить публичный ключ на сервер
ssh-copy-id -i ~/.ssh/id_ed25519.pub user@server
```

### Deploy keys для CI/CD

```bash
# На сервере: создать deploy key
ssh-keygen -t ed25519 -f ~/.ssh/deploy_key -N "" -C "deploy@oes-server"

# Добавить в GitHub: Settings → Deploy keys → Add deploy key
# Важно: НЕ ставить "Allow write access" (read-only)
```

---

## Защита серверов

### Базовая настройка каждого сервера

```bash
# 1. Обновление системы
sudo apt update && sudo apt upgrade -y

# 2. Установить fail2ban
sudo apt install fail2ban -y
sudo systemctl enable fail2ban

# /etc/fail2ban/jail.local
[sshd]
enabled = true
port = 22
maxretry = 5
bantime = 3600
findtime = 600

# 3. Настроить UFW
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp    # SSH
sudo ufw allow 80/tcp    # HTTP (если нужен)
sudo ufw enable

# 4. НЕ открывать порты БД (3050 Firebird, 5432 PostgreSQL) наружу
```

### Что НЕ делать на серверах

- Не работать от root (создать deploy пользователя)
- Не открывать порты Firebird (3050), PostgreSQL (5432) наружу
- Не хранить бэкапы на том же сервере
- Не использовать слабые пароли для БД
- Не оставлять тестовые учётные записи SYSDBA с паролем по умолчанию (`masterkey`)

---

## База данных

### НИКОГДА не использовать SYSDBA в приложении

```sql
-- Firebird: создать отдельного пользователя для приложения
-- (через isql или IBExpert)
CREATE USER OES_APP PASSWORD 'strong_random_password';
GRANT ALL ON TABLE users TO OES_APP;
GRANT ALL ON TABLE documents TO OES_APP;
-- Не давать права на системные таблицы RDB$...
```

```sql
-- PostgreSQL: отдельный пользователь
CREATE USER oes_app WITH PASSWORD 'strong_random_password';
CREATE DATABASE oes_prod OWNER oes_app;
GRANT ALL PRIVILEGES ON DATABASE oes_prod TO oes_app;
-- НЕ давать SUPERUSER права
```

### Параметризованные запросы — ОБЯЗАТЕЛЬНО

**Это критичная проблема в OES.** Конкатенация строк в SQL-запросах — SQL-инъекция.

```cpp
// ПЛОХО — SQL-инъекция (текущая практика, требует исправления)
wxString sql = wxString::Format(
    "SELECT * FROM users WHERE login = '%s' AND password = '%s'",
    login, password
);
query->ExecQuery(sql);

// ХОРОШО — ibPreparedStatement с SetParamString/SetParamInt (OES-способ)
// ibDatabaseLayer* db — получен через ibApplicationData или передан в конструктор
ibPreparedStatement* stmt = db->PrepareStatement(
    "SELECT * FROM USERS WHERE LOGIN = ? AND PASSWORD = ?"
);
stmt->SetParamString(1, login);
stmt->SetParamString(2, hashedPassword);
ibDatabaseResultSet* rs = stmt->RunQuery();
// rs обрабатывается и освобождается через RAII или явно

// ХОРОШО — SetParamInt для числовых параметров
ibPreparedStatement* stmtById = db->PrepareStatement(
    "SELECT * FROM USERS WHERE USER_ID = ?"
);
stmtById->SetParamInt(1, userId);
```

```cpp
// ХОРОШО — PostgreSQL через libpq (параметризованные запросы)
const char* paramValues[2] = { loginStr.c_str(), hashedPwd.c_str() };
PGresult* res = PQexecParams(
    conn,
    "SELECT * FROM users WHERE login = $1 AND password = $2",
    2,          // nParams
    nullptr,    // paramTypes
    paramValues,
    nullptr,    // paramLengths
    nullptr,    // paramFormats
    0           // resultFormat (text)
);
```

### Хеширование паролей — переход с MD5 на bcrypt/Argon2

**Текущая проблема:** OES использует MD5 для хеширования паролей. MD5 давно считается небезопасным.

**Плановый переход:**

> **Примечание:** libsodium является **запланированной** зависимостью OES и ещё не добавлена в проект. Перед использованием кода ниже необходимо добавить libsodium в систему сборки (vcpkg: `vcpkg install libsodium`).

```cpp
// ПЛОХО — текущая практика (MD5)
// wxString hash = MD5(password);

// ХОРОШО — bcrypt через OpenSSL или библиотеку bcrypt
// Вариант 1: libsodium (рекомендуется, планируется к добавлению)
#include <sodium.h>

std::string hashPassword(const std::string& password) {
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(
            hash, password.c_str(), password.size(),
            crypto_pwhash_OPSLIMIT_MODERATE,
            crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
        throw std::runtime_error("Password hashing failed (out of memory)");
    }
    return std::string(hash);
}

bool verifyPassword(const std::string& password, const std::string& hash) {
    return crypto_pwhash_str_verify(
        hash.c_str(), password.c_str(), password.size()) == 0;
}
```

**Миграция существующих MD5 паролей:**
1. При следующем входе пользователя проверить MD5 (legacy path)
2. Если совпадает — сохранить новый bcrypt хеш
3. После истечения срока — принудительная смена пароля

### Бэкапы Firebird

```bash
#!/bin/bash
# Ежедневный бэкап Firebird (gbak)
TIMESTAMP=$(date +%Y%m%d_%H%M)
gbak -backup -user SYSDBA -password "$SYSDBA_PASSWORD" \
    /path/to/oes.fdb \
    /backups/oes_${TIMESTAMP}.fbk

# Компрессия
gzip /backups/oes_${TIMESTAMP}.fbk

# Удалить бэкапы старше 30 дней
find /backups -name "*.fbk.gz" -mtime +30 -delete

# Cron (каждый день в 3:00)
# 0 3 * * * /opt/scripts/backup-db.sh
```

---

## Безопасность C++-кода

### Переполнение буфера — основная угроза

```cpp
// ПЛОХО — фиксированный буфер, нет проверки длины
char buf[256];
strcpy(buf, userInput);    // Переполнение буфера!
sprintf(buf, userInput);   // Уязвимость форматной строки!

// ХОРОШО — std::string, std::vector
std::string buf = userInput;  // Автоматическое управление памятью

// ХОРОШО — если нужен C-буфер
char buf[256];
strncpy(buf, userInput, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = '\0';  // Гарантия нуль-терминатора
```

### RAII и умные указатели вместо сырых

```cpp
// ПЛОХО — утечки памяти и исключения без RAII
void loadDocument() {
    Document* doc = new Document();
    doc->load(path);   // Если бросит исключение — утечка!
    processDoc(doc);
    delete doc;        // Если processDoc бросит исключение — утечка!
}

// ХОРОШО — RAII через умные указатели
void loadDocument() {
    auto doc = std::make_unique<Document>();
    doc->load(path);
    processDoc(*doc);
    // doc автоматически удалится при выходе из scope
}

// ХОРОШО — shared_ptr для разделяемого владения
std::shared_ptr<DatabaseConnection> conn = getConnection();
```

### Проверка входных данных

```cpp
// Валидация всех пользовательских вводов
bool validateInput(const wxString& input, size_t maxLen = 1024) {
    if (input.empty()) return false;
    if (input.length() > maxLen) return false;
    // Проверка на недопустимые символы (если применимо)
    return true;
}

// Валидация числовых параметров
int parsePort(const wxString& portStr) {
    long port;
    if (!portStr.ToLong(&port) || port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid port number");
    }
    return static_cast<int>(port);
}
```

### Пустые блоки catch — ЗАПРЕЩЕНЫ

```cpp
// ПЛОХО — скрывает ошибки (частая проблема в OES)
try {
    connectToDatabase();
} catch (...) {
    // пусто — ошибка проглочена!
}

// ХОРОШО — логировать минимум
try {
    connectToDatabase();
} catch (const std::exception& e) {
    wxLogError("Database connection failed: %s", e.what());
    throw;  // или корректная обработка
} catch (...) {
    wxLogError("Database connection failed: unknown error");
    throw;
}
```

---

## Статический анализ и инструменты безопасности

### cppcheck

```bash
# Установка
sudo apt install cppcheck   # Linux
winget install Cppcheck     # Windows

# Базовый анализ
cppcheck --enable=all --std=c++17 src/

# Анализ с подавлением ложных срабатываний
cppcheck --enable=all --std=c++17 \
    --suppress=missingIncludeSystem \
    --suppress=unmatchedSuppression \
    --error-exitcode=1 \
    src/

# Генерация XML-отчёта
cppcheck --enable=all --xml src/ 2> cppcheck-report.xml
```

### clang-tidy

```bash
# Установка (через LLVM)
sudo apt install clang-tidy

# Запуск на файле
clang-tidy src/engine/backend/databaseLayer/databaseLayer.cpp -- -std=c++17 -Isrc/

# .clang-tidy конфигурация (в корне проекта)
cat > .clang-tidy << 'EOF'
Checks: >
  clang-diagnostic-*,
  clang-analyzer-*,
  cppcoreguidelines-*,
  modernize-*,
  bugprone-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -cppcoreguidelines-pro-bounds-array-to-pointer-decay
WarningsAsErrors: ''
HeaderFilterRegex: '.*'
EOF
```

### Sanitizers (при сборке Debug/Test)

```cmake
# CMakeLists.txt — AddressSanitizer для выявления memory corruption
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(oes PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(oes PRIVATE
        -fsanitize=address,undefined
    )
endif()
```

```bash
# Запуск с AddressSanitizer
ASAN_OPTIONS=detect_leaks=1 ./oes_debug

# Запуск с UndefinedBehaviorSanitizer
UBSAN_OPTIONS=print_stacktrace=1 ./oes_debug

# Запуск с ThreadSanitizer (проверка гонок данных)
./oes_tsan
```

---

## OWASP Top 10 — адаптация для desktop C++

### 1. SQL Injection (критично для OES)

Конкатенация строк в SQL-запросах — главная уязвимость. Все строки подключения к БД и формирование запросов должны использовать параметры (см. раздел «База данных» выше).

### 2. Небезопасное хранение данных

```cpp
// ПЛОХО — пароли в логах
wxLogMessage("Connecting as user %s with password %s", user, password);

// ХОРОШО — никаких паролей в логах
wxLogMessage("Connecting as user %s", user);

// ПЛОХО — пароли в памяти как plain text дольше необходимого
std::string password = getPassword();
// ... использовать password ...
// password остаётся в памяти и может быть считана из core dump

// ХОРОШО — очистить память после использования
std::string password = getPassword();
// ... использовать password ...
std::fill(password.begin(), password.end(), '\0');
password.clear();
```

### 3. Использование компонентов с известными уязвимостями

- Регулярно обновляйте wxWidgets, IBPP, OpenSSL, zlib и другие зависимости
- Подпишитесь на CVE-уведомления для используемых библиотек
- Используйте только актуальные версии с поддержкой безопасности

### 4. Небезопасная десериализация

```cpp
// При чтении данных из файлов/сети — всегда валидировать
bool loadConfig(const wxString& path) {
    // Проверить что файл не слишком большой (защита от zip-bomb)
    // wxFileName::GetSize() не является методом экземпляра — используйте wxFile::Length()
    wxFile file(path);
    if (!file.IsOpened() || file.Length() > MAX_CONFIG_SIZE) {
        wxLogError("Config file too large or inaccessible: %s", path);
        return false;
    }
    file.Close();

    // Валидировать все прочитанные значения
    wxFileConfig config(wxEmptyString, wxEmptyString, path);
    wxString host = config.Read("database/host", "localhost");
    // Проверить host на допустимые символы (только hostname/IP)
    // ...
}
```

### 5. Недостаточное логирование

```cpp
// Логировать все попытки входа (успешные и неуспешные)
void onLoginAttempt(const wxString& user, bool success) {
    wxLogMessage("[AUDIT] Login %s for user '%s'",
        success ? "SUCCESS" : "FAILED", user);
}

// Логировать изменения критичных данных
void onRecordDeleted(const wxString& table, long id, const wxString& user) {
    wxLogMessage("[AUDIT] DELETE from %s, id=%ld, by user '%s'",
        table, id, user);
}
```

---

## Чеклист безопасности для каждого PR

- [ ] Нет хардкод паролей/строк подключения в коде
- [ ] Входные данные валидируются перед использованием
- [ ] SQL-запросы используют параметры, не конкатенацию строк
- [ ] Пустые блоки catch заменены логированием
- [ ] Нет новых вызовов `strcpy`/`sprintf` без проверок длины
- [ ] Новые указатели используют `std::unique_ptr`/`std::shared_ptr`
- [ ] Пароли не попадают в логи
- [ ] Новые конфигурационные параметры добавлены в `config.ini.example`
- [ ] cppcheck не выдаёт новых предупреждений
