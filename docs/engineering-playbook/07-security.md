# 07. Security

## The main rule

> **If in doubt — assume it's insecure.** Better to over-protect than to explain to customers why their data leaked.

---

## Secrets and credentials

### NEVER commit

| What | Examples |
|-----|---------|
| Configuration files with passwords | `config.ini`, `settings.cfg`, `*.conf` with real data |
| Keys and certificates | `*.pem`, `*.key`, `*.crt`, `*.p12`, `*.pfx` |
| SSH keys | `id_rsa`, `id_ed25519` |
| DB connection strings | DSN with login/password |
| Passwords | Any password in any form (including hardcoded in .cpp/.h) |
| DB dumps | `*.sql`, `*.fdb`, `*.fbk` with real data |
| License keys | Serial numbers, activation codes |

### .gitignore — required minimum

```gitignore
# Configuration files with real data — NEVER in git
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

### What to do if a secret ended up in git

Panic is justified. Act immediately:

1. **Rotate the secret** — change the compromised password/key/connection string RIGHT NOW
2. **Remove from history** (if the repo is public):
```bash
# Install git-filter-repo
pip install git-filter-repo

# Remove the file from all history
git filter-repo --path config.ini --invert-paths

# Force push (the only acceptable case)
git push --force --all
```
3. **Verify** — make sure the secret is not present in any commit
4. **Notify** — inform the team about the compromise

**Important:** Even if you delete the file with a regular `git rm`, it stays in git history. Use `git filter-repo` for full removal.

---

## Storing configuration and secrets

### Configuration files

OES reads DB connection parameters and application settings from INI-like files. The main principle: the file with real data is outside git, the template is in git.

```ini
; config.ini.example — in git (template without real values)
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
# Permissions on the config file (Linux/macOS)
chmod 600 config.ini

# Verify
ls -la config.ini
# -rw------- 1 deploy deploy 512 Jan 15 10:30 config.ini
```

### Environment variables as an alternative

For CI/CD and containerized environments use environment variables:

```cpp
// Reading secrets from environment variables
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

// Usage
std::string dbPassword = getRequiredEnv("OES_DB_PASSWORD");
```

### Configuration file hierarchy

| File | Environment | In git? |
|------|-------|--------|
| `config.ini.example` | Template | Yes |
| `config.ini` | Local development | No |
| `config.staging.ini` | Staging server | No |
| `config.production.ini` | Production | No |

### Generating random passwords

```bash
# DB password (24 characters)
openssl rand -base64 24

# Encryption key (32 bytes hex)
openssl rand -hex 32
```

### Secret rotation

- **Compromised passwords** — rotate immediately (within an hour)
- **Scheduled rotation** — once per quarter for critical DB passwords
- **When an employee leaves** — rotate every password they had access to

---

## SSH access to servers

### Keys only, never passwords

```bash
# On the server: disable password authentication
# /etc/ssh/sshd_config
PasswordAuthentication no
PubkeyAuthentication yes
PermitRootLogin no
```

### Each participant — their own key

```bash
# Create a key (on your own machine)
ssh-keygen -t ed25519 -C "name@company.com"

# Add the public key to the server
ssh-copy-id -i ~/.ssh/id_ed25519.pub user@server
```

### Deploy keys for CI/CD

```bash
# On the server: create a deploy key
ssh-keygen -t ed25519 -f ~/.ssh/deploy_key -N "" -C "deploy@oes-server"

# Add to GitHub: Settings → Deploy keys → Add deploy key
# Important: do NOT enable "Allow write access" (read-only)
```

---

## Server hardening

### Baseline configuration for every server

```bash
# 1. System updates
sudo apt update && sudo apt upgrade -y

# 2. Install fail2ban
sudo apt install fail2ban -y
sudo systemctl enable fail2ban

# /etc/fail2ban/jail.local
[sshd]
enabled = true
port = 22
maxretry = 5
bantime = 3600
findtime = 600

# 3. Configure UFW
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp    # SSH
sudo ufw allow 80/tcp    # HTTP (if needed)
sudo ufw enable

# 4. Do NOT expose DB ports (3050 Firebird, 5432 PostgreSQL) to the outside
```

### What NOT to do on servers

- Don't work as root (create a deploy user)
- Don't expose Firebird (3050) or PostgreSQL (5432) ports externally
- Don't keep backups on the same server
- Don't use weak DB passwords
- Don't leave default SYSDBA test accounts with the default password (`masterkey`)

---

## Database

### NEVER use SYSDBA in the application

```sql
-- Firebird: create a dedicated user for the application
-- (via isql or IBExpert)
CREATE USER OES_APP PASSWORD 'strong_random_password';
GRANT ALL ON TABLE users TO OES_APP;
GRANT ALL ON TABLE documents TO OES_APP;
-- Do not grant rights on the RDB$... system tables
```

```sql
-- PostgreSQL: separate user
CREATE USER oes_app WITH PASSWORD 'strong_random_password';
CREATE DATABASE oes_prod OWNER oes_app;
GRANT ALL PRIVILEGES ON DATABASE oes_prod TO oes_app;
-- Do NOT grant SUPERUSER
```

### Parameterized queries — MANDATORY

**This is a critical issue in OES.** String concatenation in SQL is SQL injection.

```cpp
// BAD — SQL injection (current practice, needs fixing)
wxString sql = wxString::Format(
    "SELECT * FROM users WHERE login = '%s' AND password = '%s'",
    login, password
);
query->ExecQuery(sql);

// GOOD — ibPreparedStatement with SetParamString/SetParamInt (the OES way)
// ibDatabaseLayer* db — obtained through ibApplicationData or passed in the constructor
ibPreparedStatement* stmt = db->PrepareStatement(
    "SELECT * FROM USERS WHERE LOGIN = ? AND PASSWORD = ?"
);
stmt->SetParamString(1, login);
stmt->SetParamString(2, hashedPassword);
ibDatabaseResultSet* rs = stmt->RunQuery();
// rs is processed and released through RAII or explicitly

// GOOD — SetParamInt for numeric parameters
ibPreparedStatement* stmtById = db->PrepareStatement(
    "SELECT * FROM USERS WHERE USER_ID = ?"
);
stmtById->SetParamInt(1, userId);
```

```cpp
// GOOD — PostgreSQL via libpq (parameterized queries)
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

### Password hashing — migrating from MD5 to bcrypt/Argon2

**Current issue:** OES uses MD5 for password hashing. MD5 has long been considered insecure.

**Migration plan:**

> **Note:** libsodium is a **planned** OES dependency and has not yet been added to the project. Before using the code below it must be added to the build system (vcpkg: `vcpkg install libsodium`).

```cpp
// BAD — current practice (MD5)
// wxString hash = MD5(password);

// GOOD — bcrypt via OpenSSL or a bcrypt library
// Option 1: libsodium (recommended, planned to be added)
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

**Migrating existing MD5 passwords:**
1. On the next user login, verify with MD5 (legacy path)
2. If it matches — store the new bcrypt hash
3. After a grace period — force a password reset

### Firebird backups

```bash
#!/bin/bash
# Daily Firebird backup (gbak)
TIMESTAMP=$(date +%Y%m%d_%H%M)
gbak -backup -user SYSDBA -password "$SYSDBA_PASSWORD" \
    /path/to/oes.fdb \
    /backups/oes_${TIMESTAMP}.fbk

# Compression
gzip /backups/oes_${TIMESTAMP}.fbk

# Delete backups older than 30 days
find /backups -name "*.fbk.gz" -mtime +30 -delete

# Cron (every day at 03:00)
# 0 3 * * * /opt/scripts/backup-db.sh
```

---

## C++ code security

### Buffer overflow — the main threat

```cpp
// BAD — fixed buffer, no length check
char buf[256];
strcpy(buf, userInput);    // Buffer overflow!
sprintf(buf, userInput);   // Format string vulnerability!

// GOOD — std::string, std::vector
std::string buf = userInput;  // Automatic memory management

// GOOD — if a C buffer is required
char buf[256];
strncpy(buf, userInput, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = '\0';  // Guarantee null terminator
```

### RAII and smart pointers instead of raw ones

```cpp
// BAD — memory leaks and exceptions without RAII
void loadDocument() {
    Document* doc = new Document();
    doc->load(path);   // If it throws — leak!
    processDoc(doc);
    delete doc;        // If processDoc throws — leak!
}

// GOOD — RAII through smart pointers
void loadDocument() {
    auto doc = std::make_unique<Document>();
    doc->load(path);
    processDoc(*doc);
    // doc is destroyed automatically when leaving scope
}

// GOOD — shared_ptr for shared ownership
std::shared_ptr<DatabaseConnection> conn = getConnection();
```

### Input validation

```cpp
// Validate every user input
bool validateInput(const wxString& input, size_t maxLen = 1024) {
    if (input.empty()) return false;
    if (input.length() > maxLen) return false;
    // Check for disallowed characters (if applicable)
    return true;
}

// Validate numeric parameters
int parsePort(const wxString& portStr) {
    long port;
    if (!portStr.ToLong(&port) || port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid port number");
    }
    return static_cast<int>(port);
}
```

### Empty catch blocks — FORBIDDEN

```cpp
// BAD — hides errors (common problem in OES)
try {
    connectToDatabase();
} catch (...) {
    // empty — error swallowed!
}

// GOOD — log at minimum
try {
    connectToDatabase();
} catch (const std::exception& e) {
    wxLogError("Database connection failed: %s", e.what());
    throw;  // or handle properly
} catch (...) {
    wxLogError("Database connection failed: unknown error");
    throw;
}
```

---

## Static analysis and security tooling

### cppcheck

```bash
# Install
sudo apt install cppcheck   # Linux
winget install Cppcheck     # Windows

# Basic analysis
cppcheck --enable=all --std=c++17 src/

# Analysis with false-positive suppression
cppcheck --enable=all --std=c++17 \
    --suppress=missingIncludeSystem \
    --suppress=unmatchedSuppression \
    --error-exitcode=1 \
    src/

# XML report
cppcheck --enable=all --xml src/ 2> cppcheck-report.xml
```

### clang-tidy

```bash
# Install (via LLVM)
sudo apt install clang-tidy

# Run on a file
clang-tidy src/engine/backend/databaseLayer/databaseLayer.cpp -- -std=c++17 -Isrc/

# .clang-tidy configuration (in the project root)
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

### Sanitizers (in Debug/Test builds)

```cmake
# CMakeLists.txt — AddressSanitizer for detecting memory corruption
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
# Run with AddressSanitizer
ASAN_OPTIONS=detect_leaks=1 ./oes_debug

# Run with UndefinedBehaviorSanitizer
UBSAN_OPTIONS=print_stacktrace=1 ./oes_debug

# Run with ThreadSanitizer (data race detection)
./oes_tsan
```

---

## OWASP Top 10 — adapted for desktop C++

### 1. SQL Injection (critical for OES)

String concatenation in SQL queries is the main vulnerability. Every DB connection string and query construction must use parameters (see the "Database" section above).

### 2. Insecure data storage

```cpp
// BAD — passwords in logs
wxLogMessage("Connecting as user %s with password %s", user, password);

// GOOD — no passwords in logs
wxLogMessage("Connecting as user %s", user);

// BAD — passwords sit in memory as plain text longer than necessary
std::string password = getPassword();
// ... use password ...
// password remains in memory and may be read from a core dump

// GOOD — wipe memory after use
std::string password = getPassword();
// ... use password ...
std::fill(password.begin(), password.end(), '\0');
password.clear();
```

### 3. Using components with known vulnerabilities

- Regularly update wxWidgets, IBPP, OpenSSL, zlib, and other dependencies
- Subscribe to CVE notifications for the libraries you use
- Use only versions still receiving security support

### 4. Insecure deserialization

```cpp
// When reading data from files/network — always validate
bool loadConfig(const wxString& path) {
    // Check the file isn't too large (zip-bomb protection)
    // wxFileName::GetSize() is not an instance method — use wxFile::Length()
    wxFile file(path);
    if (!file.IsOpened() || file.Length() > MAX_CONFIG_SIZE) {
        wxLogError("Config file too large or inaccessible: %s", path);
        return false;
    }
    file.Close();

    // Validate all values read
    wxFileConfig config(wxEmptyString, wxEmptyString, path);
    wxString host = config.Read("database/host", "localhost");
    // Validate host for allowed characters (hostname/IP only)
    // ...
}
```

### 5. Insufficient logging

```cpp
// Log all login attempts (successful and failed)
void onLoginAttempt(const wxString& user, bool success) {
    wxLogMessage("[AUDIT] Login %s for user '%s'",
        success ? "SUCCESS" : "FAILED", user);
}

// Log changes to critical data
void onRecordDeleted(const wxString& table, long id, const wxString& user) {
    wxLogMessage("[AUDIT] DELETE from %s, id=%ld, by user '%s'",
        table, id, user);
}
```

---

## Security checklist for every PR

- [ ] No hardcoded passwords/connection strings in code
- [ ] Input data is validated before use
- [ ] SQL queries use parameters, not string concatenation
- [ ] Empty catch blocks replaced with logging
- [ ] No new `strcpy`/`sprintf` calls without length checks
- [ ] New pointers use `std::unique_ptr`/`std::shared_ptr`
- [ ] Passwords don't end up in logs
- [ ] New configuration parameters added to `config.ini.example`
- [ ] cppcheck shows no new warnings
