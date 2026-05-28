# 06. Database administration — OES

> OES supports several DBMSs. The primary one is Firebird (embedded and server).
> PostgreSQL, MySQL, SQLite, ODBC are alternative backends.

---

## Firebird — primary OES DBMS

### Installation

#### Linux (Ubuntu 22.04/24.04)

```bash
# Client and dev packages
sudo apt install -y firebird3.0-dev libfbclient2 firebird3.0-utils

# Firebird server (if not in embedded mode)
sudo apt install -y firebird3.0-server firebird3.0-server-core

# Check status
sudo systemctl status firebird3.0-guardian
sudo systemctl enable firebird3.0-guardian

# Version
isql-fb -z
# Firebird ISQL version LI-V3.0.x
```

#### macOS (Homebrew)

```bash
# Install Firebird via Homebrew
brew install firebird

# Start the server
brew services start firebird

# Check
isql-fb -z
# or: /usr/local/opt/firebird/bin/isql-fb -z
```

### Firebird user management

```bash
# Via gsec (deprecated, but works)
gsec -user SYSDBA -password masterkey

# Commands inside gsec:
# display             - show all users
# add OES_USER -pw password           - add a user
# modify OES_USER -pw new_password    - change password
# delete OES_USER                     - remove the user
# quit

# Via isql-fb (SQL commands)
isql-fb -user SYSDBA -password masterkey

# Inside isql-fb:
# CREATE OR ALTER USER OES_APP PASSWORD 'GENERATED_PASSWORD';
# GRANT RDB$ADMIN TO OES_APP;
# QUIT;
```

### Creating a database

```bash
# Create a new database
isql-fb -user SYSDBA -password masterkey
```

```sql
-- Create the database
CREATE DATABASE 'localhost:/var/lib/oes/databases/oes_main.fdb'
  USER 'SYSDBA' PASSWORD 'masterkey'
  DEFAULT CHARACTER SET UTF8
  COLLATION UTF8;

-- Connect to the database
CONNECT 'localhost:/var/lib/oes/databases/oes_main.fdb'
  USER 'OES_APP' PASSWORD 'app_password';

-- Verify
SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG = 0;

QUIT;
```

### OES classes for working with the DB

```cpp
// Key classes (src/engine/backend/):
//
// ibDatabaseLayer              - abstract DB layer (base class)
// ibDatabaseLayerFirebird      - Firebird backend (ibDatabaseLayer -> Firebird)
// ibDatabaseLayerPostgres      - PostgreSQL backend
// ibPreparedStatement          - parameterized queries for all backends
// ibApplicationData            - application data / connection management
// ibApplicationData::AuthenticationAndSetUser()  - user authentication against the DB
// ibApplicationDataSessionUpdater  - user session update and control
// ibMetaDataConfiguration      - metadata and schema configuration
// CreateAndUpdateTableDB()     - create / update table schemas at startup

// Example of a parameterized query via ibPreparedStatement:
// ibPreparedStatement stmt(db, "SELECT id, name FROM users WHERE section = ?");
// stmt.SetParam(1, sectionId);
// stmt.Execute();
```

### Firebird Embedded (desktop mode)

In embedded mode no server is needed — the `libfbembed.so` (Linux/macOS) / `fbembed.dll` (Windows) library is shipped with OES.

```
Windows (Desktop):
  DB path: C:\ProgramData\OES\databases\oes_main.fdb
  Library: fbembed.dll (next to oes.exe)
  User: not required (embedded = no authentication by default)

macOS (Desktop):
  DB path: ~/Library/Application Support/OES/databases/oes_main.fdb
  Library: /usr/local/opt/oes/lib/libfbembed.dylib
  File permissions: 640

Linux (Desktop / Daemon embedded):
  DB path: /var/lib/oes/databases/oes_main.fdb
  Library: /opt/oes/lib/libfbembed.so
  File permissions: oes:oes, chmod 640
```

### Rotating the SYSDBA / application user password

```bash
# 1. Generate a new password
NEW_PASS=$(openssl rand -base64 24 | tr -dc 'a-zA-Z0-9' | head -c 24)
echo "New password: $NEW_PASS"
# Save in 1Password!

# 2. Change the password via gsec
gsec -user SYSDBA -password OLD_MASTERKEY -mo SYSDBA -pw "$NEW_PASS"
# or for the application user:
gsec -user SYSDBA -password masterkey -mo OES_APP -pw "$NEW_PASS"

# 3. Verify
isql-fb -user OES_APP -password "$NEW_PASS" \
  'localhost:/var/lib/oes/databases/oes_main.fdb'

# 4. Update oes.conf
sudo nano /etc/oes/oes.conf
# [Database.Firebird]
# Password=NEW_PASSWORD

# 5. Restart OES daemon
sudo systemctl restart oes-daemon
```

### Firebird backup (gbak)

```bash
#!/bin/bash
# /opt/scripts/firebird-backup.sh
set -e

FB_HOST="localhost"
FB_PORT="3050"
FB_DATABASE="/var/lib/oes/databases/oes_main.fdb"
FB_USER="SYSDBA"
FB_PASS="${FIREBIRD_SYSDBA_PASSWORD}"   # From environment variable or 1Password
BACKUP_DIR="/var/backups/firebird"
RETENTION_DAYS=30
DATE=$(date +%Y-%m-%d_%H-%M-%S)
BACKUP_FILE="${BACKUP_DIR}/oes_main_${DATE}.fbk"

mkdir -p "$BACKUP_DIR"

# Create the backup via gbak
gbak \
  -backup \
  -user "$FB_USER" -password "$FB_PASS" \
  "${FB_HOST}/${FB_PORT}:${FB_DATABASE}" \
  "$BACKUP_FILE"

# Compress
gzip "$BACKUP_FILE"
BACKUP_FILE="${BACKUP_FILE}.gz"

# Remove old backups
find "$BACKUP_DIR" -name "*.fbk.gz" -mtime +$RETENTION_DAYS -delete

echo "Firebird backup created: $(basename $BACKUP_FILE)"
echo "Size: $(du -h $BACKUP_FILE | cut -f1)"

# Optional: upload to S3
# aws s3 cp "$BACKUP_FILE" s3://oes-backups/firebird/
```

```bash
# Cron: daily at 2:00
0 2 * * * FIREBIRD_SYSDBA_PASSWORD=masterkey /opt/scripts/firebird-backup.sh >> /var/log/firebird-backup.log 2>&1
```

### Firebird restore

```bash
# From a gbak backup
gunzip /var/backups/firebird/oes_main_2025-01-15_02-00-00.fbk.gz

gbak \
  -restore \
  -user SYSDBA -password masterkey \
  /var/backups/firebird/oes_main_2025-01-15_02-00-00.fbk \
  /var/lib/oes/databases/oes_main_restored.fdb

# Stop OES daemon, swap the DB file, start
sudo systemctl stop oes-daemon
sudo cp /var/lib/oes/databases/oes_main.fdb \
        /var/lib/oes/databases/oes_main.fdb.old
sudo cp /var/lib/oes/databases/oes_main_restored.fdb \
        /var/lib/oes/databases/oes_main.fdb
sudo chown oes:oes /var/lib/oes/databases/oes_main.fdb
sudo systemctl start oes-daemon
```

### Firebird monitoring

```bash
# Verify the server responds
isql-fb -user SYSDBA -password masterkey \
  -q -i /dev/null \
  'localhost:/var/lib/oes/databases/oes_main.fdb' 2>&1 | head -5

# Active connections
isql-fb -user SYSDBA -password masterkey <<'EOF'
CONNECT 'localhost:/var/lib/oes/databases/oes_main.fdb'
  USER 'SYSDBA' PASSWORD 'masterkey';
SELECT MON$ATTACHMENT_ID, MON$USER, MON$REMOTE_ADDRESS, MON$STATE
FROM MON$ATTACHMENTS;
QUIT;
EOF

# Database size
ls -lh /var/lib/oes/databases/oes_main.fdb

# Validate / repair
gfix -validate -full \
  -user SYSDBA -password masterkey \
  'localhost:/var/lib/oes/databases/oes_main.fdb'
```

### Useful isql-fb commands

```sql
-- List of tables (user)
SELECT RDB$RELATION_NAME FROM RDB$RELATIONS
WHERE RDB$SYSTEM_FLAG = 0
ORDER BY RDB$RELATION_NAME;

-- Server version
SELECT RDB$GET_CONTEXT('SYSTEM', 'ENGINE_VERSION') FROM RDB$DATABASE;

-- Active transactions
SELECT MON$TRANSACTION_ID, MON$ATTACHMENT_ID, MON$STATE, MON$TIMESTAMP
FROM MON$TRANSACTIONS;

-- Long-running transactions (potential leak)
SELECT MON$TRANSACTION_ID, DATEDIFF(MINUTE, MON$TIMESTAMP, CURRENT_TIMESTAMP) AS minutes_open
FROM MON$TRANSACTIONS
WHERE DATEDIFF(MINUTE, MON$TIMESTAMP, CURRENT_TIMESTAMP) > 60
ORDER BY minutes_open DESC;

-- Table sizes (approximate)
SELECT T.RDB$RELATION_NAME, COUNT(*) AS PAGES
FROM RDB$RELATIONS T
JOIN RDB$PAGES P ON P.RDB$RELATION_ID = T.RDB$RELATION_ID
WHERE T.RDB$SYSTEM_FLAG = 0
GROUP BY T.RDB$RELATION_NAME
ORDER BY PAGES DESC;

-- Forcibly terminate a stuck attachment
DELETE FROM MON$ATTACHMENTS WHERE MON$ATTACHMENT_ID = 123;
```

---

## PostgreSQL — alternative OES backend

> Full PostgreSQL administration documentation. For OES, PostgreSQL is used as an alternative backend instead of Firebird (especially in enterprise installations with high load).

### Installation

#### Linux (Ubuntu 22.04/24.04)

```bash
# Ubuntu 22.04/24.04
sudo apt install -y postgresql postgresql-contrib libpq-dev

# Check status
sudo systemctl status postgresql
sudo systemctl enable postgresql

# Version
psql --version

# Log in as postgres
sudo -u postgres psql
```

#### macOS (Homebrew)

```bash
brew install postgresql@16

# Start the server
brew services start postgresql@16

# Log in
psql postgres
```

### Creating a user and database for OES

```sql
-- Create user
CREATE USER oes_user WITH PASSWORD 'GENERATED_PASSWORD';

-- Create database
CREATE DATABASE oes_db OWNER oes_user;

-- Grant privileges
GRANT ALL PRIVILEGES ON DATABASE oes_db TO oes_user;

-- Connect and create extensions
\c oes_db

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_stat_statements";

-- Restrict privileges (production)
REVOKE ALL ON SCHEMA public FROM PUBLIC;
GRANT USAGE ON SCHEMA public TO oes_user;
GRANT CREATE ON SCHEMA public TO oes_user;

\q
```

### Security (pg_hba.conf)

```bash
sudo -u postgres psql -c "SHOW hba_file;"
# /etc/postgresql/16/main/pg_hba.conf

sudo nano /etc/postgresql/16/main/pg_hba.conf
```

```
# Only local connections for OES daemon
local   all       postgres                  peer
local   all       all                       peer
host    all       all        127.0.0.1/32   scram-sha-256
host    all       all        ::1/128        scram-sha-256
```

```bash
# postgresql.conf - listen on localhost only
sudo nano /etc/postgresql/16/main/postgresql.conf
# listen_addresses = 'localhost'

sudo systemctl restart postgresql
```

### PostgreSQL monitoring

```sql
-- Active connections (OES daemon pool)
SELECT pid, usename, datname, client_addr, state, query_start,
       round(EXTRACT(EPOCH FROM (now() - query_start))::numeric, 2) AS query_sec,
       left(query, 80) AS query_preview
FROM pg_stat_activity
WHERE state != 'idle' AND datname = 'oes_db'
ORDER BY query_start;

-- Connection count
SELECT count(*) FROM pg_stat_activity WHERE datname = 'oes_db';
SHOW max_connections;

-- Slow OES queries
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

-- OES database size
SELECT pg_size_pretty(pg_database_size('oes_db'));

-- Table sizes
SELECT
  tablename AS table,
  pg_size_pretty(pg_total_relation_size('public.' || tablename)) AS size
FROM pg_tables
WHERE schemaname = 'public'
ORDER BY pg_total_relation_size('public.' || tablename) DESC;
```

### PostgreSQL backups (pg_dump)

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

# Backup in custom format (recommended)
PGPASSWORD="${PG_OES_PASSWORD}" pg_dump \
  -h localhost \
  -U "$DB_USER" \
  -d "$DB_NAME" \
  --no-owner \
  --no-privileges \
  --format=custom \
  --compress=9 \
  -f "${BACKUP_DIR}/${DB_NAME}_${DATE}.dump"

# Remove old backups
find "$BACKUP_DIR" -name "*.dump" -mtime +$RETENTION_DAYS -delete

echo "PostgreSQL backup: ${DB_NAME}_${DATE}.dump"
echo "Size: $(du -h ${BACKUP_DIR}/${DB_NAME}_${DATE}.dump | cut -f1)"
```

```bash
# Cron: daily at 2:30
30 2 * * * PG_OES_PASSWORD=password /opt/scripts/pg-backup.sh >> /var/log/pg-backup.log 2>&1
```

### PostgreSQL restore

```bash
# From custom format (.dump)
pg_restore \
  -h localhost \
  -U oes_user \
  -d oes_db \
  --clean \
  --if-exists \
  --no-owner \
  /var/backups/postgresql/oes_db_2025-01-15.dump

# Into a new database
sudo -u postgres createdb oes_db_restored
pg_restore -h localhost -U oes_user -d oes_db_restored \
  /var/backups/postgresql/oes_db.dump
```

### PostgreSQL tuning for OES

```bash
sudo nano /etc/postgresql/16/main/postgresql.conf
```

```
# === Memory (for a server with 8GB RAM) ===
shared_buffers = 2GB              # 25% of RAM
work_mem = 32MB
maintenance_work_mem = 512MB
effective_cache_size = 6GB        # 75% of RAM

# === WAL ===
wal_buffers = 64MB
checkpoint_completion_target = 0.9
max_wal_size = 4GB

# === Planner (SSD) ===
random_page_cost = 1.1
effective_io_concurrency = 200

# === Connections (OES connection pool) ===
max_connections = 100

# === Logging ===
log_min_duration_statement = 1000  # Log queries > 1 sec
log_checkpoints = on
log_connections = on
log_disconnections = on
shared_preload_libraries = 'pg_stat_statements'
```

```bash
sudo systemctl restart postgresql
```

---

## SQLite (lightweight OES mode)

```bash
# SQLite file - just a file with permissions
ls -la /var/lib/oes/databases/oes.sqlite3
# -rw-r----- 1 oes oes 52428800 Jan 15 10:30 oes.sqlite3

# Permissions
sudo chown oes:oes /var/lib/oes/databases/oes.sqlite3
sudo chmod 640 /var/lib/oes/databases/oes.sqlite3

# SQLite console
sqlite3 /var/lib/oes/databases/oes.sqlite3

# Useful SQLite commands
.tables                            -- list tables
.schema table_name                 -- table structure
SELECT count(*) FROM sqlite_master WHERE type='table';
PRAGMA integrity_check;            -- integrity check
PRAGMA page_count;                 -- page count
PRAGMA page_size;                  -- page size (bytes)
VACUUM;                            -- defragmentation
```

### SQLite backup

```bash
#!/bin/bash
# /opt/scripts/sqlite-backup.sh
set -e

DB_FILE="/var/lib/oes/databases/oes.sqlite3"
BACKUP_DIR="/var/backups/sqlite"
DATE=$(date +%Y-%m-%d_%H-%M-%S)
RETENTION_DAYS=30

mkdir -p "$BACKUP_DIR"

# Online backup via the .backup command
sqlite3 "$DB_FILE" ".backup '${BACKUP_DIR}/oes_${DATE}.sqlite3'"

# Compress
gzip "${BACKUP_DIR}/oes_${DATE}.sqlite3"

# Remove old
find "$BACKUP_DIR" -name "*.sqlite3.gz" -mtime +$RETENTION_DAYS -delete

echo "SQLite backup: oes_${DATE}.sqlite3.gz"
```

---

## MySQL/MariaDB (optional)

```bash
# Installation
sudo apt install -y mysql-server libmysqlclient-dev

# Create user and database for OES
sudo mysql
```

```sql
CREATE USER 'oes_user'@'localhost' IDENTIFIED BY 'GENERATED_PASSWORD';
CREATE DATABASE oes_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
GRANT ALL PRIVILEGES ON oes_db.* TO 'oes_user'@'localhost';
FLUSH PRIVILEGES;
QUIT;
```

```bash
# Backup
mysqldump -u oes_user -p oes_db | gzip > /var/backups/mysql/oes_$(date +%Y%m%d).sql.gz

# Restore
gunzip -c /var/backups/mysql/oes_20250115.sql.gz | mysql -u oes_user -p oes_db
```

---

## Monitoring script for all databases

```bash
#!/bin/bash
# /opt/scripts/db-monitor.sh

echo "========================================="
echo " OES Database Monitor - $(date)"
echo "========================================="

# --- Firebird ---
if systemctl is-active --quiet firebird3.0-guardian; then
  echo ""
  echo "=== Firebird ==="
  echo "Status: RUNNING"
  DB="/var/lib/oes/databases/oes_main.fdb"
  if [ -f "$DB" ]; then
    echo "DB size: $(du -h $DB | cut -f1)"
  fi
  # Active connections
  CONNS=$(isql-fb -user SYSDBA -password "$FIREBIRD_SYSDBA_PASSWORD" \
    -q 2>/dev/null <<< "
CONNECT 'localhost:$DB' USER 'SYSDBA' PASSWORD '$FIREBIRD_SYSDBA_PASSWORD';
SELECT COUNT(*) FROM MON\$ATTACHMENTS;
QUIT;" 2>/dev/null | grep -E '^\s+[0-9]+' | tr -d ' ' || echo "N/A")
  echo "Connections: $CONNS"
else
  echo ""
  echo "=== Firebird === STOPPED"
fi

# --- PostgreSQL ---
if systemctl is-active --quiet postgresql; then
  echo ""
  echo "=== PostgreSQL ==="
  echo "Status: RUNNING"
  echo "Version: $(psql --version | head -1)"
  sudo -u postgres psql -c "SELECT pg_size_pretty(pg_database_size('oes_db')) AS size;" -t 2>/dev/null | \
    xargs echo "oes_db size:"
  sudo -u postgres psql -c "SELECT count(*) AS connections FROM pg_stat_activity WHERE datname='oes_db';" -t 2>/dev/null | \
    xargs echo "oes_db connections:"
else
  echo ""
  echo "=== PostgreSQL === STOPPED"
fi

# --- SQLite ---
SQLITE_DB="/var/lib/oes/databases/oes.sqlite3"
if [ -f "$SQLITE_DB" ]; then
  echo ""
  echo "=== SQLite ==="
  echo "File size: $(du -h $SQLITE_DB | cut -f1)"
  echo "Integrity check: $(sqlite3 $SQLITE_DB 'PRAGMA integrity_check;' 2>/dev/null)"
fi

echo ""
echo "========================================="
```

---

## Database administration checklist

```
[ ] Firebird: SYSDBA password changed (NOT masterkey in production)
[ ] Firebird: separate application user (OES_APP) created
[ ] Firebird: .fdb file accessible only to the oes user
[ ] PostgreSQL: oes_user has restricted privileges
[ ] PostgreSQL: listen_addresses = 'localhost'
[ ] PostgreSQL: pg_hba.conf - localhost only
[ ] All DB passwords stored in 1Password / Bitwarden
[ ] Firebird backups: cron configured, restore test performed
[ ] PostgreSQL backups: pg_dump cron configured
[ ] Log rotation for DB logs configured
[ ] Active connection monitoring configured
[ ] Database size tracked (alert at > 80% disk)
```
