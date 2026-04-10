# 06. Администрирование баз данных — OES

> OES поддерживает несколько СУБД. Основная — Firebird (embedded и server).
> PostgreSQL, MySQL, SQLite, ODBC — как альтернативные backend'ы.

---

## Firebird — основная СУБД OES

### Установка

#### Linux (Ubuntu 22.04/24.04)

```bash
# Клиент и dev-пакеты
sudo apt install -y firebird3.0-dev libfbclient2 firebird3.0-utils

# Firebird server (если не embedded режим)
sudo apt install -y firebird3.0-server firebird3.0-server-core

# Проверить статус
sudo systemctl status firebird3.0-guardian
sudo systemctl enable firebird3.0-guardian

# Версия
isql-fb -z
# Firebird ISQL version LI-V3.0.x
```

#### macOS (Homebrew)

```bash
# Установить Firebird через Homebrew
brew install firebird

# Запустить сервер
brew services start firebird

# Проверить
isql-fb -z
# или: /usr/local/opt/firebird/bin/isql-fb -z
```

### Управление пользователями Firebird

```bash
# Через gsec (устаревший, но работает)
gsec -user SYSDBA -password masterkey

# Команды внутри gsec:
# display             — показать всех пользователей
# add OES_USER -pw пароль        — добавить пользователя
# modify OES_USER -pw новый_пароль  — сменить пароль
# delete OES_USER                — удалить пользователя
# quit

# Через isql-fb (SQL-команды)
isql-fb -user SYSDBA -password masterkey

# Внутри isql-fb:
# CREATE OR ALTER USER OES_APP PASSWORD 'СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ';
# GRANT RDB$ADMIN TO OES_APP;
# QUIT;
```

### Создание базы данных

```bash
# Создать новую базу данных
isql-fb -user SYSDBA -password masterkey
```

```sql
-- Создать базу данных
CREATE DATABASE 'localhost:/var/lib/oes/databases/oes_main.fdb'
  USER 'SYSDBA' PASSWORD 'masterkey'
  DEFAULT CHARACTER SET UTF8
  COLLATION UTF8;

-- Подключиться к базе
CONNECT 'localhost:/var/lib/oes/databases/oes_main.fdb'
  USER 'OES_APP' PASSWORD 'app_password';

-- Проверить
SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG = 0;

QUIT;
```

### OES классы для работы с БД

```cpp
// Ключевые классы (src/engine/backend/):
//
// ibDatabaseLayer              — абстрактный слой БД (базовый класс)
// ibDatabaseLayerFirebird      — Firebird backend (ibDatabaseLayer → Firebird)
// ibDatabaseLayerPostgres      — PostgreSQL backend
// ibPreparedStatement          — параметризованные запросы ко всем backend'ам
// ibApplicationData            — данные приложения / управление соединением
// ibApplicationData::AuthenticationAndSetUser()  — аутентификация пользователя в БД
// ibApplicationDataSessionUpdater  — обновление и контроль сессии пользователя
// ibMetaDataConfiguration      — конфигурация метаданных и схемы
// CreateAndUpdateTableDB()     — создание / обновление схемы таблиц при старте

// Пример параметризованного запроса через ibPreparedStatement:
// ibPreparedStatement stmt(db, "SELECT id, name FROM users WHERE section = ?");
// stmt.SetParam(1, sectionId);
// stmt.Execute();
```

### Firebird Embedded (desktop режим)

В embedded режиме не нужен сервер — библиотека `libfbembed.so` (Linux/macOS) / `fbembed.dll` (Windows) поставляется вместе с OES.

```
Windows (Desktop):
  Путь к БД: C:\ProgramData\OES\databases\oes_main.fdb
  Библиотека: fbembed.dll (рядом с oes.exe)
  Пользователь: не требуется (embedded = нет аутентификации по умолчанию)

macOS (Desktop):
  Путь к БД: ~/Library/Application Support/OES/databases/oes_main.fdb
  Библиотека: /usr/local/opt/oes/lib/libfbembed.dylib
  Права на файл: 640

Linux (Desktop / Daemon embedded):
  Путь к БД: /var/lib/oes/databases/oes_main.fdb
  Библиотека: /opt/oes/lib/libfbembed.so
  Права на файл: oes:oes, chmod 640
```

### Ротация пароля SYSDBA / пользователя приложения

```bash
# 1. Сгенерировать новый пароль
NEW_PASS=$(openssl rand -base64 24 | tr -dc 'a-zA-Z0-9' | head -c 24)
echo "Новый пароль: $NEW_PASS"
# Сохранить в 1Password!

# 2. Сменить пароль через gsec
gsec -user SYSDBA -password OLD_MASTERKEY -mo SYSDBA -pw "$NEW_PASS"
# или для прикладного пользователя:
gsec -user SYSDBA -password masterkey -mo OES_APP -pw "$NEW_PASS"

# 3. Проверить
isql-fb -user OES_APP -password "$NEW_PASS" \
  'localhost:/var/lib/oes/databases/oes_main.fdb'

# 4. Обновить oes.conf
sudo nano /etc/oes/oes.conf
# [Database.Firebird]
# Password=НОВЫЙ_ПАРОЛЬ

# 5. Перезапустить OES daemon
sudo systemctl restart oes-daemon
```

### Бэкап Firebird (gbak)

```bash
#!/bin/bash
# /opt/scripts/firebird-backup.sh
set -e

FB_HOST="localhost"
FB_PORT="3050"
FB_DATABASE="/var/lib/oes/databases/oes_main.fdb"
FB_USER="SYSDBA"
FB_PASS="${FIREBIRD_SYSDBA_PASSWORD}"   # Из переменной окружения или 1Password
BACKUP_DIR="/var/backups/firebird"
RETENTION_DAYS=30
DATE=$(date +%Y-%m-%d_%H-%M-%S)
BACKUP_FILE="${BACKUP_DIR}/oes_main_${DATE}.fbk"

mkdir -p "$BACKUP_DIR"

# Создать бэкап через gbak
gbak \
  -backup \
  -user "$FB_USER" -password "$FB_PASS" \
  "${FB_HOST}/${FB_PORT}:${FB_DATABASE}" \
  "$BACKUP_FILE"

# Сжать
gzip "$BACKUP_FILE"
BACKUP_FILE="${BACKUP_FILE}.gz"

# Удалить старые бэкапы
find "$BACKUP_DIR" -name "*.fbk.gz" -mtime +$RETENTION_DAYS -delete

echo "Бэкап Firebird создан: $(basename $BACKUP_FILE)"
echo "Размер: $(du -h $BACKUP_FILE | cut -f1)"

# Опционально: копировать в S3
# aws s3 cp "$BACKUP_FILE" s3://oes-backups/firebird/
```

```bash
# Cron: ежедневно в 2:00
0 2 * * * FIREBIRD_SYSDBA_PASSWORD=masterkey /opt/scripts/firebird-backup.sh >> /var/log/firebird-backup.log 2>&1
```

### Восстановление Firebird

```bash
# Из gbak бэкапа
gunzip /var/backups/firebird/oes_main_2025-01-15_02-00-00.fbk.gz

gbak \
  -restore \
  -user SYSDBA -password masterkey \
  /var/backups/firebird/oes_main_2025-01-15_02-00-00.fbk \
  /var/lib/oes/databases/oes_main_restored.fdb

# Остановить OES daemon, заменить файл БД, запустить
sudo systemctl stop oes-daemon
sudo cp /var/lib/oes/databases/oes_main.fdb \
        /var/lib/oes/databases/oes_main.fdb.old
sudo cp /var/lib/oes/databases/oes_main_restored.fdb \
        /var/lib/oes/databases/oes_main.fdb
sudo chown oes:oes /var/lib/oes/databases/oes_main.fdb
sudo systemctl start oes-daemon
```

### Мониторинг Firebird

```bash
# Проверить что сервер отвечает
isql-fb -user SYSDBA -password masterkey \
  -q -i /dev/null \
  'localhost:/var/lib/oes/databases/oes_main.fdb' 2>&1 | head -5

# Активные соединения
isql-fb -user SYSDBA -password masterkey <<'EOF'
CONNECT 'localhost:/var/lib/oes/databases/oes_main.fdb'
  USER 'SYSDBA' PASSWORD 'masterkey';
SELECT MON$ATTACHMENT_ID, MON$USER, MON$REMOTE_ADDRESS, MON$STATE
FROM MON$ATTACHMENTS;
QUIT;
EOF

# Размер базы данных
ls -lh /var/lib/oes/databases/oes_main.fdb

# Validate / repair
gfix -validate -full \
  -user SYSDBA -password masterkey \
  'localhost:/var/lib/oes/databases/oes_main.fdb'
```

### Полезные команды isql-fb

```sql
-- Список таблиц (пользовательских)
SELECT RDB$RELATION_NAME FROM RDB$RELATIONS
WHERE RDB$SYSTEM_FLAG = 0
ORDER BY RDB$RELATION_NAME;

-- Версия сервера
SELECT RDB$GET_CONTEXT('SYSTEM', 'ENGINE_VERSION') FROM RDB$DATABASE;

-- Активные транзакции
SELECT MON$TRANSACTION_ID, MON$ATTACHMENT_ID, MON$STATE, MON$TIMESTAMP
FROM MON$TRANSACTIONS;

-- Длинные транзакции (потенциальная утечка)
SELECT MON$TRANSACTION_ID, DATEDIFF(MINUTE, MON$TIMESTAMP, CURRENT_TIMESTAMP) AS minutes_open
FROM MON$TRANSACTIONS
WHERE DATEDIFF(MINUTE, MON$TIMESTAMP, CURRENT_TIMESTAMP) > 60
ORDER BY minutes_open DESC;

-- Размер таблиц (приблизительно)
SELECT T.RDB$RELATION_NAME, COUNT(*) AS PAGES
FROM RDB$RELATIONS T
JOIN RDB$PAGES P ON P.RDB$RELATION_ID = T.RDB$RELATION_ID
WHERE T.RDB$SYSTEM_FLAG = 0
GROUP BY T.RDB$RELATION_NAME
ORDER BY PAGES DESC;

-- Принудительно завершить зависший attachment
DELETE FROM MON$ATTACHMENTS WHERE MON$ATTACHMENT_ID = 123;
```

---

## PostgreSQL — альтернативный backend OES

> Полная документация по PostgreSQL администрированию. Для OES PostgreSQL используется как альтернативный backend вместо Firebird (особенно в enterprise инсталляциях с высокой нагрузкой).

### Установка

#### Linux (Ubuntu 22.04/24.04)

```bash
# Ubuntu 22.04/24.04
sudo apt install -y postgresql postgresql-contrib libpq-dev

# Проверить статус
sudo systemctl status postgresql
sudo systemctl enable postgresql

# Версия
psql --version

# Войти как postgres
sudo -u postgres psql
```

#### macOS (Homebrew)

```bash
brew install postgresql@16

# Запустить сервер
brew services start postgresql@16

# Войти
psql postgres
```

### Создание пользователя и базы для OES

```sql
-- Создать пользователя
CREATE USER oes_user WITH PASSWORD 'СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ';

-- Создать базу данных
CREATE DATABASE oes_db OWNER oes_user;

-- Дать права
GRANT ALL PRIVILEGES ON DATABASE oes_db TO oes_user;

-- Подключиться и создать расширения
\c oes_db

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_stat_statements";

-- Ограничить права (production)
REVOKE ALL ON SCHEMA public FROM PUBLIC;
GRANT USAGE ON SCHEMA public TO oes_user;
GRANT CREATE ON SCHEMA public TO oes_user;

\q
```

### Безопасность (pg_hba.conf)

```bash
sudo -u postgres psql -c "SHOW hba_file;"
# /etc/postgresql/16/main/pg_hba.conf

sudo nano /etc/postgresql/16/main/pg_hba.conf
```

```
# Только локальные подключения для OES daemon
local   all       postgres                  peer
local   all       all                       peer
host    all       all        127.0.0.1/32   scram-sha-256
host    all       all        ::1/128        scram-sha-256
```

```bash
# postgresql.conf — слушать только localhost
sudo nano /etc/postgresql/16/main/postgresql.conf
# listen_addresses = 'localhost'

sudo systemctl restart postgresql
```

### Мониторинг PostgreSQL

```sql
-- Активные подключения (OES daemon пул)
SELECT pid, usename, datname, client_addr, state, query_start,
       round(EXTRACT(EPOCH FROM (now() - query_start))::numeric, 2) AS query_sec,
       left(query, 80) AS query_preview
FROM pg_stat_activity
WHERE state != 'idle' AND datname = 'oes_db'
ORDER BY query_start;

-- Количество подключений
SELECT count(*) FROM pg_stat_activity WHERE datname = 'oes_db';
SHOW max_connections;

-- Медленные запросы OES
SELECT
  calls,
  round(total_exec_time::numeric, 2) AS total_ms,
  round(mean_exec_time::numeric, 2) AS mean_ms,
  round(max_exec_time::numeric, 2) AS max_ms,
  left(query, 100) AS query
FROM pg_stat_statements
WHERE dbid = (SELECT oid FROM pg_database WHERE datname = 'oes_db')
ORDER BY mean_exec_time DESC
LIMIT 20;

-- Размер базы OES
SELECT pg_size_pretty(pg_database_size('oes_db'));

-- Размер таблиц
SELECT
  tablename AS table,
  pg_size_pretty(pg_total_relation_size('public.' || tablename)) AS size
FROM pg_tables
WHERE schemaname = 'public'
ORDER BY pg_total_relation_size('public.' || tablename) DESC;
```

### Бэкапы PostgreSQL (pg_dump)

```bash
#!/bin/bash
# /opt/scripts/pg-backup.sh
set -e

DB_NAME="oes_db"
DB_USER="oes_user"
BACKUP_DIR="/var/backups/postgresql"
RETENTION_DAYS=30
DATE=$(date +%Y-%m-%d_%H-%M-%S)

mkdir -p "$BACKUP_DIR"

# Бэкап в custom формате (рекомендуется)
PGPASSWORD="${PG_OES_PASSWORD}" pg_dump \
  -h localhost \
  -U "$DB_USER" \
  -d "$DB_NAME" \
  --no-owner \
  --no-privileges \
  --format=custom \
  --compress=9 \
  -f "${BACKUP_DIR}/${DB_NAME}_${DATE}.dump"

# Удалить старые бэкапы
find "$BACKUP_DIR" -name "*.dump" -mtime +$RETENTION_DAYS -delete

echo "PostgreSQL бэкап: ${DB_NAME}_${DATE}.dump"
echo "Размер: $(du -h ${BACKUP_DIR}/${DB_NAME}_${DATE}.dump | cut -f1)"
```

```bash
# Cron: ежедневно в 2:30
30 2 * * * PG_OES_PASSWORD=пароль /opt/scripts/pg-backup.sh >> /var/log/pg-backup.log 2>&1
```

### Восстановление PostgreSQL

```bash
# Из custom формата (.dump)
pg_restore \
  -h localhost \
  -U oes_user \
  -d oes_db \
  --clean \
  --if-exists \
  --no-owner \
  /var/backups/postgresql/oes_db_2025-01-15.dump

# В новую базу
sudo -u postgres createdb oes_db_restored
pg_restore -h localhost -U oes_user -d oes_db_restored \
  /var/backups/postgresql/oes_db.dump
```

### Оптимизация PostgreSQL для OES

```bash
sudo nano /etc/postgresql/16/main/postgresql.conf
```

```
# === Память (для сервера с 8GB RAM) ===
shared_buffers = 2GB              # 25% от RAM
work_mem = 32MB
maintenance_work_mem = 512MB
effective_cache_size = 6GB        # 75% от RAM

# === WAL ===
wal_buffers = 64MB
checkpoint_completion_target = 0.9
max_wal_size = 4GB

# === Планировщик (SSD) ===
random_page_cost = 1.1
effective_io_concurrency = 200

# === Подключения (OES connection pool) ===
max_connections = 100

# === Логирование ===
log_min_duration_statement = 1000  # Логировать запросы > 1 сек
log_checkpoints = on
log_connections = on
log_disconnections = on
shared_preload_libraries = 'pg_stat_statements'
```

```bash
sudo systemctl restart postgresql
```

---

## SQLite (лёгкий режим OES)

```bash
# SQLite файл — просто файл с правами
ls -la /var/lib/oes/databases/oes.sqlite3
# -rw-r----- 1 oes oes 52428800 Jan 15 10:30 oes.sqlite3

# Права
sudo chown oes:oes /var/lib/oes/databases/oes.sqlite3
sudo chmod 640 /var/lib/oes/databases/oes.sqlite3

# Консоль SQLite
sqlite3 /var/lib/oes/databases/oes.sqlite3

# Полезные команды SQLite
.tables                            -- список таблиц
.schema имя_таблицы                -- структура таблицы
SELECT count(*) FROM sqlite_master WHERE type='table';
PRAGMA integrity_check;            -- проверка целостности
PRAGMA page_count;                 -- количество страниц
PRAGMA page_size;                  -- размер страницы (байт)
VACUUM;                            -- дефрагментация
```

### Бэкап SQLite

```bash
#!/bin/bash
# /opt/scripts/sqlite-backup.sh
set -e

DB_FILE="/var/lib/oes/databases/oes.sqlite3"
BACKUP_DIR="/var/backups/sqlite"
DATE=$(date +%Y-%m-%d_%H-%M-%S)
RETENTION_DAYS=30

mkdir -p "$BACKUP_DIR"

# Онлайн бэкап через .backup команду
sqlite3 "$DB_FILE" ".backup '${BACKUP_DIR}/oes_${DATE}.sqlite3'"

# Сжать
gzip "${BACKUP_DIR}/oes_${DATE}.sqlite3"

# Удалить старые
find "$BACKUP_DIR" -name "*.sqlite3.gz" -mtime +$RETENTION_DAYS -delete

echo "SQLite бэкап: oes_${DATE}.sqlite3.gz"
```

---

## MySQL/MariaDB (опционально)

```bash
# Установка
sudo apt install -y mysql-server libmysqlclient-dev

# Создать пользователя и базу для OES
sudo mysql
```

```sql
CREATE USER 'oes_user'@'localhost' IDENTIFIED BY 'СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ';
CREATE DATABASE oes_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
GRANT ALL PRIVILEGES ON oes_db.* TO 'oes_user'@'localhost';
FLUSH PRIVILEGES;
QUIT;
```

```bash
# Бэкап
mysqldump -u oes_user -p oes_db | gzip > /var/backups/mysql/oes_$(date +%Y%m%d).sql.gz

# Восстановление
gunzip -c /var/backups/mysql/oes_20250115.sql.gz | mysql -u oes_user -p oes_db
```

---

## Скрипт мониторинга всех баз

```bash
#!/bin/bash
# /opt/scripts/db-monitor.sh

echo "========================================="
echo " OES Database Monitor — $(date)"
echo "========================================="

# --- Firebird ---
if systemctl is-active --quiet firebird3.0-guardian; then
  echo ""
  echo "=== Firebird ==="
  echo "Статус: RUNNING"
  DB="/var/lib/oes/databases/oes_main.fdb"
  if [ -f "$DB" ]; then
    echo "Размер БД: $(du -h $DB | cut -f1)"
  fi
  # Активные соединения
  CONNS=$(isql-fb -user SYSDBA -password "$FIREBIRD_SYSDBA_PASSWORD" \
    -q 2>/dev/null <<< "
CONNECT 'localhost:$DB' USER 'SYSDBA' PASSWORD '$FIREBIRD_SYSDBA_PASSWORD';
SELECT COUNT(*) FROM MON\$ATTACHMENTS;
QUIT;" 2>/dev/null | grep -E '^\s+[0-9]+' | tr -d ' ' || echo "N/A")
  echo "Соединений: $CONNS"
else
  echo ""
  echo "=== Firebird === STOPPED"
fi

# --- PostgreSQL ---
if systemctl is-active --quiet postgresql; then
  echo ""
  echo "=== PostgreSQL ==="
  echo "Статус: RUNNING"
  echo "Версия: $(psql --version | head -1)"
  sudo -u postgres psql -c "SELECT pg_size_pretty(pg_database_size('oes_db')) AS size;" -t 2>/dev/null | \
    xargs echo "Размер oes_db:"
  sudo -u postgres psql -c "SELECT count(*) AS connections FROM pg_stat_activity WHERE datname='oes_db';" -t 2>/dev/null | \
    xargs echo "Соединений oes_db:"
else
  echo ""
  echo "=== PostgreSQL === STOPPED"
fi

# --- SQLite ---
SQLITE_DB="/var/lib/oes/databases/oes.sqlite3"
if [ -f "$SQLITE_DB" ]; then
  echo ""
  echo "=== SQLite ==="
  echo "Размер файла: $(du -h $SQLITE_DB | cut -f1)"
  echo "Проверка целостности: $(sqlite3 $SQLITE_DB 'PRAGMA integrity_check;' 2>/dev/null)"
fi

echo ""
echo "========================================="
```

---

## Чеклист администрирования баз данных

```
[ ] Firebird: пароль SYSDBA сменён (НЕ masterkey в production)
[ ] Firebird: создан отдельный пользователь приложения (OES_APP)
[ ] Firebird: файл .fdb доступен только пользователю oes
[ ] PostgreSQL: пользователь oes_user с ограниченными правами
[ ] PostgreSQL: listen_addresses = 'localhost'
[ ] PostgreSQL: pg_hba.conf — только localhost
[ ] Все пароли БД в 1Password / Bitwarden
[ ] Бэкапы Firebird: cron настроен, тест восстановления выполнен
[ ] Бэкапы PostgreSQL: pg_dump cron настроен
[ ] Логrotation для логов БД настроен
[ ] Мониторинг активных соединений настроен
[ ] Размер баз данных отслеживается (алерт при > 80% диска)
```
