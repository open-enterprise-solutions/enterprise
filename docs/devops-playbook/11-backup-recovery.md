# 11. Backups and recovery

> 3-2-1 strategy for OES: protecting user data, Firebird/PostgreSQL databases, configuration files. Disaster recovery for desktop and daemon modes.

---

## 3-2-1 strategy for OES

```
3 copies of data:
  1. Original (working DB / user project files)
  2. Local backup (on the same machine or local server)
  3. Remote backup (network drive, NAS, cloud)

2 different media:
  - HDD/SSD on the user's machine or server
  - NAS / external drive / S3-compatible storage

1 offsite copy:
  - Another physical location or a cloud service

What to back up in OES:
  - Firebird databases (.fdb files) - CRITICAL
  - PostgreSQL databases - CRITICAL (if used)
  - SQLite project files - CRITICAL
  - Configuration files:
      Windows:  %APPDATA%\OES\ and C:\ProgramData\OES\
      macOS:    ~/Library/Application Support/OES/  (desktop)
                /etc/oes/                            (daemon)
      Linux:    /etc/oes/ and /var/lib/oes/
  - License keys and certificates
  - User templates and settings
```

---

## Firebird backups

### gbak — standard Firebird backup tool

```bash
#!/bin/bash
# scripts/backup-firebird.sh
set -euo pipefail

# === Configuration ===
FB_DB="/var/lib/oes/data/oes.fdb"
FB_USER="SYSDBA"
FB_PASSWORD="${FB_SYSDBA_PASSWORD:-masterkey}"
BACKUP_DIR="/var/backups/oes/firebird"
RETENTION_DAYS=30
DATE=$(date +%Y-%m-%d_%H-%M-%S)
BACKUP_FILE="${BACKUP_DIR}/oes_${DATE}.fbk"
LOG_FILE="/var/log/oes/backup.log"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"; }

mkdir -p "$BACKUP_DIR"

log "Starting Firebird backup: $FB_DB"

# Backup via gbak (portable format - portable between versions)
gbak \
    -backup \
    -user "$FB_USER" \
    -password "$FB_PASSWORD" \
    -garbage \
    -statistics \
    "$FB_DB" \
    "$BACKUP_FILE"

BACKUP_SIZE=$(du -h "$BACKUP_FILE" | cut -f1)
log "Backup created: $BACKUP_FILE ($BACKUP_SIZE)"

# === Compress the backup ===
gzip "$BACKUP_FILE"
BACKUP_FILE="${BACKUP_FILE}.gz"
log "Compressed: $BACKUP_FILE"

# === Verify backup integrity ===
# gbak has no -verify flag. Verify via test restore + gfix -v -full.
log "Verifying backup integrity (test restore)..."
VERIFY_DB="/tmp/oes_verify_${DATE}.fdb"
TEMP_FBK="${BACKUP_FILE%.gz}"

gunzip -c "$BACKUP_FILE" > "$TEMP_FBK"

gbak \
    -restore \
    -user "$FB_USER" \
    -password "$FB_PASSWORD" \
    -replace_database \
    "$TEMP_FBK" \
    "$VERIFY_DB"
RESTORE_RC=$?

if [ $RESTORE_RC -eq 0 ] && [ -f "$VERIFY_DB" ]; then
    # Additional structural check of the restored DB
    gfix \
        -user "$FB_USER" \
        -password "$FB_PASSWORD" \
        -v -full \
        "$VERIFY_DB" 2>&1
    if [ $? -eq 0 ]; then
        log "Integrity check: OK"
    else
        log "WARNING: gfix found issues in the restored DB"
    fi
    rm -f "$TEMP_FBK" "$VERIFY_DB"
else
    log "ERROR: backup corrupted (gbak -restore exited with code $RESTORE_RC)!"
    rm -f "$TEMP_FBK" "$VERIFY_DB"
    curl -s -X POST \
        "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "text=CRITICAL: OES Firebird backup corrupted: $BACKUP_FILE"
    exit 1
fi

# === Remove old local backups ===
find "$BACKUP_DIR" -name "*.fbk.gz" -mtime +$RETENTION_DAYS -delete
log "Removed local backups older than ${RETENTION_DAYS} days"

# === Copy to network storage (optional) ===
if [ -n "${BACKUP_REMOTE_PATH:-}" ]; then
    rsync -az "$BACKUP_FILE" "${BACKUP_REMOTE_PATH}/"
    log "Copied to: $BACKUP_REMOTE_PATH"
fi

log "Backup completed successfully"
```

### Cron for Firebird backups

```bash
# Edit as oes user or root:
crontab -e

# Full backup daily at 02:00
0 2 * * * FB_SYSDBA_PASSWORD="password" TELEGRAM_BOT_TOKEN="token" TELEGRAM_CHAT_ID="chatid" /opt/oes/scripts/backup-firebird.sh

# Backup every 6 hours for critical data
0 */6 * * * FB_SYSDBA_PASSWORD="password" /opt/oes/scripts/backup-firebird.sh
```

### Restoring Firebird

```bash
# === Full restore from gbak ===

# 1. Stop OES daemon
sudo systemctl stop oes-daemon

# 2. Rename the current DB (in case it is needed)
sudo mv /var/lib/oes/data/oes.fdb /var/lib/oes/data/oes.fdb.broken

# 3. Decompress the backup
gunzip -c /var/backups/oes/firebird/oes_2025-01-15.fbk.gz > /tmp/oes_restore.fbk

# 4. Restore
gbak \
    -restore \
    -user SYSDBA \
    -password "$FB_SYSDBA_PASSWORD" \
    /tmp/oes_restore.fbk \
    /var/lib/oes/data/oes.fdb

# 5. Set permissions
chown oes:oes /var/lib/oes/data/oes.fdb
chmod 600 /var/lib/oes/data/oes.fdb

# 6. Start
sudo systemctl start oes-daemon

# 7. Verify
gfix -user SYSDBA -password "$FB_SYSDBA_PASSWORD" -validate /var/lib/oes/data/oes.fdb
rm -f /tmp/oes_restore.fbk
```

### Windows: Firebird backup via PowerShell

```powershell
# scripts\backup-firebird.ps1
param(
    [string]$DbPath = "C:\ProgramData\OES\data\oes.fdb",
    [string]$BackupDir = "C:\Backups\OES\Firebird",
    [string]$FbUser = "SYSDBA",
    [string]$FbPassword = $env:FB_SYSDBA_PASSWORD,
    [int]$RetentionDays = 30
)

$date = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$backupFile = Join-Path $BackupDir "oes_$date.fbk"

# Create directory
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

Write-Host "Backing up Firebird: $DbPath"

# gbak is usually at C:\Program Files\Firebird\Firebird_X_X\bin\
$gbak = "C:\Program Files\Firebird\Firebird_4_0\bin\gbak.exe"

& $gbak `
    -backup `
    -user $FbUser `
    -password $FbPassword `
    -garbage `
    $DbPath `
    $backupFile

if ($LASTEXITCODE -ne 0) {
    Write-Error "gbak failed with $LASTEXITCODE"
    exit 1
}

# Compress
Compress-Archive -Path $backupFile -DestinationPath "$backupFile.zip" -CompressionLevel Optimal
Remove-Item $backupFile
Write-Host "Backup created: $backupFile.zip ($(((Get-Item "$backupFile.zip").Length / 1MB).ToString('F1')) MB)"

# Remove old backups
Get-ChildItem -Path $BackupDir -Filter "*.fbk.zip" |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-$RetentionDays) } |
    Remove-Item
Write-Host "Old backup cleanup completed"
```

```powershell
# Task Scheduler entry:
$action = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument "-NonInteractive -File C:\Scripts\backup-firebird.ps1"

$trigger = New-ScheduledTaskTrigger -Daily -At "02:00"
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -RunLevel Highest

Register-ScheduledTask `
    -TaskName "OES-BackupFirebird" `
    -Action $action `
    -Trigger $trigger `
    -Principal $principal `
    -Force
```

---

## PostgreSQL backups (if used in OES)

```bash
#!/bin/bash
# scripts/backup-postgres.sh
set -euo pipefail

DB_NAME="oes_db"
DB_USER="oes_user"
DB_HOST="localhost"
BACKUP_DIR="/var/backups/oes/postgresql"
RETENTION_DAYS=30
DATE=$(date +%Y-%m-%d_%H-%M-%S)
BACKUP_FILE="${BACKUP_DIR}/${DB_NAME}_${DATE}.dump"

mkdir -p "$BACKUP_DIR"

echo "[$(date)] PostgreSQL backup $DB_NAME"

PGPASSWORD="${PG_PASSWORD}" pg_dump \
    -h "$DB_HOST" \
    -U "$DB_USER" \
    -d "$DB_NAME" \
    --format=custom \
    --compress=9 \
    --no-owner \
    -f "$BACKUP_FILE"

# Integrity check
pg_restore --list "$BACKUP_FILE" > /dev/null
echo "[$(date)] Backup OK: $BACKUP_FILE ($(du -h "$BACKUP_FILE" | cut -f1))"

# Remove old backups
find "$BACKUP_DIR" -name "*.dump" -mtime +$RETENTION_DAYS -delete
```

### PostgreSQL restore

```bash
# 1. Stop OES daemon
sudo systemctl stop oes-daemon

# 2. Recreate the DB
sudo -u postgres psql -c "DROP DATABASE IF EXISTS oes_db;"
sudo -u postgres psql -c "CREATE DATABASE oes_db OWNER oes_user;"

# 3. Restore
pg_restore \
    -h localhost \
    -U oes_user \
    -d oes_db \
    --no-owner \
    --verbose \
    /var/backups/oes/postgresql/oes_db_2025-01-15.dump

# 4. Start
sudo systemctl start oes-daemon
```

---

## Backing up user data and configurations

### Daemon configuration

```bash
#!/bin/bash
# scripts/backup-oes-config.sh

BACKUP_DIR="/var/backups/oes/config"
DATE=$(date +%Y-%m-%d)

mkdir -p "$BACKUP_DIR"

# Configuration files
tar czf "${BACKUP_DIR}/oes-config_${DATE}.tar.gz" \
    /etc/oes/ \
    /var/lib/oes/templates/ \
    --exclude='*.tmp'

# License files (if stored on the server)
if [ -d "/var/lib/oes/licenses" ]; then
    cp -r /var/lib/oes/licenses "${BACKUP_DIR}/licenses_${DATE}"
fi

echo "[$(date)] OES configuration archived: ${BACKUP_DIR}/oes-config_${DATE}.tar.gz"

# Keep for 90 days
find "$BACKUP_DIR" -name "*.tar.gz" -mtime +90 -delete
```

### Windows: user config backup

```powershell
# scripts\backup-user-config.ps1
# Run on application exit or on a schedule

$appData = "$env:APPDATA\OES"
$backupDir = "$env:USERPROFILE\Documents\OES-Backups\Config"
$date = Get-Date -Format "yyyy-MM-dd"
$backupFile = Join-Path $backupDir "oes-config_$date.zip"

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

# Archive the configuration
Compress-Archive -Path @(
    "$appData\*.ini",
    "$appData\*.conf",
    "$appData\templates\",
    "$appData\settings\"
) -DestinationPath $backupFile -Force

Write-Host "Configuration saved: $backupFile"

# Keep the last 10 backups
Get-ChildItem -Path $backupDir -Filter "oes-config_*.zip" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -Skip 10 |
    Remove-Item
```

---

## Backup testing (monthly!)

```bash
#!/bin/bash
# scripts/test-backup.sh
set -euo pipefail

LOG="/var/log/oes/backup-test.log"
ERRORS=""

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG"; }

# === Test 1: Firebird backup ===
log "Testing Firebird backup..."
LATEST_FB=$(ls -t /var/backups/oes/firebird/*.fbk.gz 2>/dev/null | head -1)

if [ -z "$LATEST_FB" ]; then
    ERRORS="${ERRORS}\n- No Firebird backups"
else
    # Verify the file is not corrupted (can be decompressed)
    if gunzip -t "$LATEST_FB" 2>/dev/null; then
        SIZE=$(du -h "$LATEST_FB" | cut -f1)
        log "Firebird OK: $LATEST_FB ($SIZE)"
        
        # Attempt restore into a test DB
        TEMP_FBK="/tmp/test_restore.fbk"
        TEMP_FDB="/tmp/test_restore.fdb"
        gunzip -c "$LATEST_FB" > "$TEMP_FBK"
        
        gbak \
            -restore \
            -user SYSDBA \
            -password "${FB_SYSDBA_PASSWORD:-masterkey}" \
            "$TEMP_FBK" \
            "$TEMP_FDB" 2>/dev/null
        
        if [ $? -eq 0 ] && [ -f "$TEMP_FDB" ]; then
            log "Firebird RESTORE test: OK"
        else
            ERRORS="${ERRORS}\n- Firebird: test restore error"
        fi
        
        rm -f "$TEMP_FBK" "$TEMP_FDB"
    else
        ERRORS="${ERRORS}\n- Firebird: backup corrupted ($LATEST_FB)"
    fi
fi

# === Test 2: PostgreSQL backup (if used) ===
if ls /var/backups/oes/postgresql/*.dump 2>/dev/null | head -1 | grep -q .; then
    log "Testing PostgreSQL backup..."
    LATEST_PG=$(ls -t /var/backups/oes/postgresql/*.dump 2>/dev/null | head -1)
    
    if pg_restore --list "$LATEST_PG" > /dev/null 2>&1; then
        SIZE=$(du -h "$LATEST_PG" | cut -f1)
        log "PostgreSQL OK: $LATEST_PG ($SIZE)"
    else
        ERRORS="${ERRORS}\n- PostgreSQL: backup corrupted"
    fi
fi

# === Test 3: Configuration backup ===
log "Testing configuration backup..."
LATEST_CFG=$(ls -t /var/backups/oes/config/*.tar.gz 2>/dev/null | head -1)
if [ -n "$LATEST_CFG" ]; then
    if tar tzf "$LATEST_CFG" > /dev/null 2>&1; then
        log "Configuration OK: $LATEST_CFG"
    else
        ERRORS="${ERRORS}\n- Configuration: archive corrupted"
    fi
else
    ERRORS="${ERRORS}\n- No configuration backups"
fi

# === Result ===
if [ -z "$ERRORS" ]; then
    log "All backup tests passed"
else
    log "ERRORS:${ERRORS}"
    curl -s -X POST \
        "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "text=CRITICAL: OES backup test failed: $ERRORS"
fi
```

```bash
# Cron: 1st day of each month at 04:00
0 4 1 * * /opt/oes/scripts/test-backup.sh
```

---

## Disaster Recovery Plan

### Scenario: Firebird database corruption

```
STEP 1: Diagnosis (5 min)
  - gfix -validate: determine the extent of damage
  - Check the date of the last successful backup
  - Estimate data loss (difference between backup and now)

STEP 2: Stop the service (1 min)
  sudo systemctl stop oes-daemon   # or stop the application
  # Do NOT attempt to write anything to a corrupted DB!

STEP 3: Restore from the last backup (10-20 min)
  - Find the last clean backup:
    ls -lt /var/backups/oes/firebird/
  
  - Restore:
    gunzip -c /var/backups/oes/firebird/oes_DATE.fbk.gz > /tmp/restore.fbk
    gbak -restore -user SYSDBA -password PASS /tmp/restore.fbk /var/lib/oes/data/oes.fdb
  
  - Verify:
    gfix -user SYSDBA -password PASS -validate /var/lib/oes/data/oes.fdb

STEP 4: Start (2 min)
  sudo systemctl start oes-daemon

STEP 5: Verification (5 min)
  - Verify opening key documents
  - Check logs for errors
  - Notify users about potential data loss

TOTAL: 20-35 minutes
Data loss: data since the last backup (up to 6 hours)
```

### Scenario: Windows reinstall (desktop mode)

```
STEP 1: Before reinstall (prevention)
  - Export OES configuration (menu -> File -> Export settings)
  - Copy %APPDATA%\OES\ to an external drive
  - Copy project files (.fdb, .sqlite)
  - Save the license key

STEP 2: Restore on the new system
  - Install OES (download the installer from GitHub Releases)
  - Copy %APPDATA%\OES\ from the backup drive
  - Copy project files
  - Enter the license key
  - Import settings (File -> Import settings)

TOTAL: 15-30 minutes
```

### Scenario: full server loss (daemon mode)

```
STEP 1: New server / VM (10 min)
  - Deploy Ubuntu 22.04 LTS
  - Or restore a VM snapshot

STEP 2: Install OES daemon (15 min)
  - Follow the installation guide
  - Install Firebird Server
  - Configure /etc/oes/daemon.conf

STEP 3: Restore data (15-30 min)
  - Firebird DB:
    rsync user@backup:/backups/oes/firebird/LATEST.fbk.gz /tmp/
    gunzip /tmp/LATEST.fbk.gz
    gbak -restore -user SYSDBA -password PASS /tmp/LATEST.fbk /var/lib/oes/data/oes.fdb
  
  - Configuration:
    tar xzf /backup/oes-config_DATE.tar.gz -C /

STEP 4: Start and verify (5 min)
  sudo systemctl start oes-daemon
  sudo systemctl status oes-daemon

TOTAL: 40-60 minutes
```

---

## OES backup summary table

```
What                   | Frequency | Retention (local) | Where to store
-----------------------|-----------|-------------------|-------------------
Firebird DB (gbak)     | 6h / 24h  | 30 days           | /var/backups + NAS
PostgreSQL (pg_dump)   | 6h / 24h  | 30 days           | /var/backups + NAS
SQLite project files   | on change | 30 days           | Next to the file
Configuration /etc/oes | on change | 90 days           | /var/backups + NAS
Licenses               | on change | indefinite        | Multiple locations
%APPDATA%\OES\ / ~/Library/Application Support/OES/ | 24h | 30 days | External drive/NAS
Backup test            | monthly   | -                 | -
```

---

## Backup freshness monitoring

```bash
#!/bin/bash
# scripts/check-backup-freshness.sh
# Run hourly

BACKUP_DIR="/var/backups/oes/firebird"
MAX_AGE_HOURS=25

LATEST=$(find "$BACKUP_DIR" -name "*.fbk.gz" -type f -printf '%T@\n' 2>/dev/null | sort -rn | head -1)

if [ -z "$LATEST" ]; then
    MSG="CRITICAL: No Firebird backups in $BACKUP_DIR"
else
    AGE_SECONDS=$(echo "$(date +%s) - ${LATEST%.*}" | bc)
    AGE_HOURS=$((AGE_SECONDS / 3600))
    
    if [ "$AGE_HOURS" -gt "$MAX_AGE_HOURS" ]; then
        MSG="WARNING: Last Firebird backup ${AGE_HOURS}h ago (max: ${MAX_AGE_HOURS}h)"
    else
        exit 0  # All good
    fi
fi

curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
    -d chat_id="$TELEGRAM_CHAT_ID" \
    -d text="$MSG" > /dev/null
```
