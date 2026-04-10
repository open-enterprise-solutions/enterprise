# 11. Бэкапы и восстановление

> Стратегия 3-2-1 для OES: защита данных пользователей, баз данных Firebird/PostgreSQL, конфигурационных файлов. Disaster recovery для desktop и daemon-режима.

---

## Стратегия 3-2-1 для OES

```
3 копии данных:
  1. Оригинал (рабочая БД / файлы проекта пользователя)
  2. Локальный бэкап (на той же машине или локальном сервере)
  3. Удалённый бэкап (сетевой диск, NAS, облако)

2 разных носителя:
  — HDD/SSD машины пользователя или сервера
  — NAS / внешний диск / S3-совместимое хранилище

1 копия offsite:
  — Другая физическая локация или облачный сервис

Что бэкапить в OES:
  — Базы данных Firebird (.fdb файлы) — КРИТИЧНО
  — Базы данных PostgreSQL — КРИТИЧНО (если используется)
  — SQLite файлы проектов — КРИТИЧНО
  — Конфигурационные файлы:
      Windows:  %APPDATA%\OES\ и C:\ProgramData\OES\
      macOS:    ~/Library/Application Support/OES/  (desktop)
                /etc/oes/                            (daemon)
      Linux:    /etc/oes/ и /var/lib/oes/
  — Лицензионные ключи и сертификаты
  — Пользовательские шаблоны и настройки
```

---

## Firebird бэкапы

### gbak — стандартный инструмент бэкапа Firebird

```bash
#!/bin/bash
# scripts/backup-firebird.sh
set -euo pipefail

# === Конфигурация ===
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

log "Начало бэкапа Firebird: $FB_DB"

# Бэкап через gbak (portable format — переносимый между версиями)
gbak \
    -backup \
    -user "$FB_USER" \
    -password "$FB_PASSWORD" \
    -garbage \
    -statistics \
    "$FB_DB" \
    "$BACKUP_FILE"

BACKUP_SIZE=$(du -h "$BACKUP_FILE" | cut -f1)
log "Бэкап создан: $BACKUP_FILE ($BACKUP_SIZE)"

# === Сжать бэкап ===
gzip "$BACKUP_FILE"
BACKUP_FILE="${BACKUP_FILE}.gz"
log "Сжат: $BACKUP_FILE"

# === Проверить целостность бэкапа ===
# gbak не имеет флага -verify. Проверяем через тестовое восстановление + gfix -v -full.
log "Проверка целостности бэкапа (тестовое восстановление)..."
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
    # Дополнительная проверка структуры восстановленной БД
    gfix \
        -user "$FB_USER" \
        -password "$FB_PASSWORD" \
        -v -full \
        "$VERIFY_DB" 2>&1
    if [ $? -eq 0 ]; then
        log "Проверка целостности: OK"
    else
        log "ПРЕДУПРЕЖДЕНИЕ: gfix обнаружил проблемы в восстановленной БД"
    fi
    rm -f "$TEMP_FBK" "$VERIFY_DB"
else
    log "ОШИБКА: бэкап повреждён (gbak -restore завершился с кодом $RESTORE_RC)!"
    rm -f "$TEMP_FBK" "$VERIFY_DB"
    curl -s -X POST \
        "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "text=КРИТИЧНО: Бэкап Firebird OES повреждён: $BACKUP_FILE"
    exit 1
fi

# === Удалить старые локальные бэкапы ===
find "$BACKUP_DIR" -name "*.fbk.gz" -mtime +$RETENTION_DAYS -delete
log "Удалены локальные бэкапы старше ${RETENTION_DAYS} дней"

# === Копировать на сетевое хранилище (опционально) ===
if [ -n "${BACKUP_REMOTE_PATH:-}" ]; then
    rsync -az "$BACKUP_FILE" "${BACKUP_REMOTE_PATH}/"
    log "Скопирован на: $BACKUP_REMOTE_PATH"
fi

log "Бэкап завершён успешно"
```

### Cron для Firebird бэкапов

```bash
# Редактировать от пользователя oes или root:
crontab -e

# Полный бэкап каждый день в 02:00
0 2 * * * FB_SYSDBA_PASSWORD="пароль" TELEGRAM_BOT_TOKEN="token" TELEGRAM_CHAT_ID="chatid" /opt/oes/scripts/backup-firebird.sh

# Бэкап каждые 6 часов для критичных данных
0 */6 * * * FB_SYSDBA_PASSWORD="пароль" /opt/oes/scripts/backup-firebird.sh
```

### Восстановление Firebird

```bash
# === Полное восстановление из gbak ===

# 1. Остановить OES daemon
sudo systemctl stop oes-daemon

# 2. Переименовать текущую БД (на случай если понадобится)
sudo mv /var/lib/oes/data/oes.fdb /var/lib/oes/data/oes.fdb.broken

# 3. Разжать бэкап
gunzip -c /var/backups/oes/firebird/oes_2025-01-15.fbk.gz > /tmp/oes_restore.fbk

# 4. Восстановить
gbak \
    -restore \
    -user SYSDBA \
    -password "$FB_SYSDBA_PASSWORD" \
    /tmp/oes_restore.fbk \
    /var/lib/oes/data/oes.fdb

# 5. Установить права
chown oes:oes /var/lib/oes/data/oes.fdb
chmod 600 /var/lib/oes/data/oes.fdb

# 6. Запустить
sudo systemctl start oes-daemon

# 7. Проверить
gfix -user SYSDBA -password "$FB_SYSDBA_PASSWORD" -validate /var/lib/oes/data/oes.fdb
rm -f /tmp/oes_restore.fbk
```

### Windows: Firebird бэкап через PowerShell

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

# Создать директорию
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

Write-Host "Бэкап Firebird: $DbPath"

# gbak обычно в C:\Program Files\Firebird\Firebird_X_X\bin\
$gbak = "C:\Program Files\Firebird\Firebird_4_0\bin\gbak.exe"

& $gbak `
    -backup `
    -user $FbUser `
    -password $FbPassword `
    -garbage `
    $DbPath `
    $backupFile

if ($LASTEXITCODE -ne 0) {
    Write-Error "gbak завершился с ошибкой $LASTEXITCODE"
    exit 1
}

# Сжать
Compress-Archive -Path $backupFile -DestinationPath "$backupFile.zip" -CompressionLevel Optimal
Remove-Item $backupFile
Write-Host "Создан бэкап: $backupFile.zip ($(((Get-Item "$backupFile.zip").Length / 1MB).ToString('F1')) МБ)"

# Удалить старые
Get-ChildItem -Path $BackupDir -Filter "*.fbk.zip" |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-$RetentionDays) } |
    Remove-Item
Write-Host "Очистка старых бэкапов завершена"
```

```powershell
# Задача в Task Scheduler:
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

## PostgreSQL бэкапы (если используется в OES)

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

echo "[$(date)] Бэкап PostgreSQL $DB_NAME"

PGPASSWORD="${PG_PASSWORD}" pg_dump \
    -h "$DB_HOST" \
    -U "$DB_USER" \
    -d "$DB_NAME" \
    --format=custom \
    --compress=9 \
    --no-owner \
    -f "$BACKUP_FILE"

# Проверить целостность
pg_restore --list "$BACKUP_FILE" > /dev/null
echo "[$(date)] Бэкап OK: $BACKUP_FILE ($(du -h "$BACKUP_FILE" | cut -f1))"

# Удалить старые
find "$BACKUP_DIR" -name "*.dump" -mtime +$RETENTION_DAYS -delete
```

### Восстановление PostgreSQL

```bash
# 1. Остановить OES daemon
sudo systemctl stop oes-daemon

# 2. Пересоздать БД
sudo -u postgres psql -c "DROP DATABASE IF EXISTS oes_db;"
sudo -u postgres psql -c "CREATE DATABASE oes_db OWNER oes_user;"

# 3. Восстановить
pg_restore \
    -h localhost \
    -U oes_user \
    -d oes_db \
    --no-owner \
    --verbose \
    /var/backups/oes/postgresql/oes_db_2025-01-15.dump

# 4. Запустить
sudo systemctl start oes-daemon
```

---

## Бэкап пользовательских данных и конфигураций

### Конфигурация daemon

```bash
#!/bin/bash
# scripts/backup-oes-config.sh

BACKUP_DIR="/var/backups/oes/config"
DATE=$(date +%Y-%m-%d)

mkdir -p "$BACKUP_DIR"

# Конфигурационные файлы
tar czf "${BACKUP_DIR}/oes-config_${DATE}.tar.gz" \
    /etc/oes/ \
    /var/lib/oes/templates/ \
    --exclude='*.tmp'

# Лицензионные файлы (если хранятся на сервере)
if [ -d "/var/lib/oes/licenses" ]; then
    cp -r /var/lib/oes/licenses "${BACKUP_DIR}/licenses_${DATE}"
fi

echo "[$(date)] Конфигурация OES заархивирована: ${BACKUP_DIR}/oes-config_${DATE}.tar.gz"

# Хранить 90 дней
find "$BACKUP_DIR" -name "*.tar.gz" -mtime +90 -delete
```

### Windows: бэкап конфигурации пользователя

```powershell
# scripts\backup-user-config.ps1
# Запускать при выходе из приложения или по расписанию

$appData = "$env:APPDATA\OES"
$backupDir = "$env:USERPROFILE\Documents\OES-Backups\Config"
$date = Get-Date -Format "yyyy-MM-dd"
$backupFile = Join-Path $backupDir "oes-config_$date.zip"

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

# Архивировать конфигурацию
Compress-Archive -Path @(
    "$appData\*.ini",
    "$appData\*.conf",
    "$appData\templates\",
    "$appData\settings\"
) -DestinationPath $backupFile -Force

Write-Host "Конфигурация сохранена: $backupFile"

# Хранить последние 10 бэкапов
Get-ChildItem -Path $backupDir -Filter "oes-config_*.zip" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -Skip 10 |
    Remove-Item
```

---

## Тестирование бэкапов (ежемесячно!)

```bash
#!/bin/bash
# scripts/test-backup.sh
set -euo pipefail

LOG="/var/log/oes/backup-test.log"
ERRORS=""

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG"; }

# === Тест 1: Firebird бэкап ===
log "Тест Firebird бэкапа..."
LATEST_FB=$(ls -t /var/backups/oes/firebird/*.fbk.gz 2>/dev/null | head -1)

if [ -z "$LATEST_FB" ]; then
    ERRORS="${ERRORS}\n- Нет бэкапов Firebird"
else
    # Проверить что файл не повреждён (можно разжать)
    if gunzip -t "$LATEST_FB" 2>/dev/null; then
        SIZE=$(du -h "$LATEST_FB" | cut -f1)
        log "Firebird OK: $LATEST_FB ($SIZE)"
        
        # Попытаться восстановить в тестовую БД
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
            log "Firebird RESTORE тест: OK"
        else
            ERRORS="${ERRORS}\n- Firebird: ошибка тестового восстановления"
        fi
        
        rm -f "$TEMP_FBK" "$TEMP_FDB"
    else
        ERRORS="${ERRORS}\n- Firebird: бэкап повреждён ($LATEST_FB)"
    fi
fi

# === Тест 2: PostgreSQL бэкап (если используется) ===
if ls /var/backups/oes/postgresql/*.dump 2>/dev/null | head -1 | grep -q .; then
    log "Тест PostgreSQL бэкапа..."
    LATEST_PG=$(ls -t /var/backups/oes/postgresql/*.dump 2>/dev/null | head -1)
    
    if pg_restore --list "$LATEST_PG" > /dev/null 2>&1; then
        SIZE=$(du -h "$LATEST_PG" | cut -f1)
        log "PostgreSQL OK: $LATEST_PG ($SIZE)"
    else
        ERRORS="${ERRORS}\n- PostgreSQL: бэкап повреждён"
    fi
fi

# === Тест 3: Конфигурационный бэкап ===
log "Тест бэкапа конфигурации..."
LATEST_CFG=$(ls -t /var/backups/oes/config/*.tar.gz 2>/dev/null | head -1)
if [ -n "$LATEST_CFG" ]; then
    if tar tzf "$LATEST_CFG" > /dev/null 2>&1; then
        log "Конфигурация OK: $LATEST_CFG"
    else
        ERRORS="${ERRORS}\n- Конфигурация: архив повреждён"
    fi
else
    ERRORS="${ERRORS}\n- Нет бэкапов конфигурации"
fi

# === Результат ===
if [ -z "$ERRORS" ]; then
    log "Все тесты бэкапов пройдены"
else
    log "ОШИБКИ:${ERRORS}"
    curl -s -X POST \
        "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "text=КРИТИЧНО: Тест бэкапов OES провалился: $ERRORS"
fi
```

```bash
# Cron: первое число каждого месяца в 04:00
0 4 1 * * /opt/oes/scripts/test-backup.sh
```

---

## Disaster Recovery Plan

### Сценарий: повреждение базы данных Firebird

```
ШАГ 1: Диагностика (5 мин)
  — gfix -validate: определить степень повреждения
  — Проверить дату последнего успешного бэкапа
  — Оценить потерю данных (разница между бэкапом и сейчас)

ШАГ 2: Остановить сервис (1 мин)
  sudo systemctl stop oes-daemon   # или остановить приложение
  # НЕ пытаться ничего записывать в повреждённую БД!

ШАГ 3: Восстановление из последнего бэкапа (10-20 мин)
  — Найти последний чистый бэкап:
    ls -lt /var/backups/oes/firebird/
  
  — Восстановить:
    gunzip -c /var/backups/oes/firebird/oes_ДАТА.fbk.gz > /tmp/restore.fbk
    gbak -restore -user SYSDBA -password PASS /tmp/restore.fbk /var/lib/oes/data/oes.fdb
  
  — Проверить:
    gfix -user SYSDBA -password PASS -validate /var/lib/oes/data/oes.fdb

ШАГ 4: Запуск (2 мин)
  sudo systemctl start oes-daemon

ШАГ 5: Проверка (5 мин)
  — Проверить открытие ключевых документов
  — Проверить логи на ошибки
  — Уведомить пользователей о потенциальной потере данных

ИТОГО: 20-35 минут
Потеря данных: данные с момента последнего бэкапа (до 6 часов)
```

### Сценарий: переустановка Windows (desktop-режим)

```
ШАГ 1: Перед переустановкой (профилактика)
  — Экспортировать конфигурацию OES (меню → Файл → Экспорт настроек)
  — Скопировать %APPDATA%\OES\ на внешний диск
  — Скопировать файлы проектов (.fdb, .sqlite)
  — Сохранить лицензионный ключ

ШАГ 2: Восстановление на новой системе
  — Установить OES (скачать инсталлятор с GitHub Releases)
  — Скопировать %APPDATA%\OES\ с резервного диска
  — Скопировать файлы проектов
  — Ввести лицензионный ключ
  — Импортировать настройки (Файл → Импорт настроек)

ИТОГО: 15-30 минут
```

### Сценарий: полная потеря сервера (daemon-режим)

```
ШАГ 1: Новый сервер / ВМ (10 мин)
  — Развернуть Ubuntu 22.04 LTS
  — Или восстановить снапшот ВМ

ШАГ 2: Установить OES daemon (15 мин)
  — Следовать руководству по установке
  — Установить Firebird Server
  — Настроить /etc/oes/daemon.conf

ШАГ 3: Восстановить данные (15-30 мин)
  — Firebird БД:
    rsync user@backup:/backups/oes/firebird/LATEST.fbk.gz /tmp/
    gunzip /tmp/LATEST.fbk.gz
    gbak -restore -user SYSDBA -password PASS /tmp/LATEST.fbk /var/lib/oes/data/oes.fdb
  
  — Конфигурация:
    tar xzf /backup/oes-config_ДАТА.tar.gz -C /

ШАГ 4: Запустить и проверить (5 мин)
  sudo systemctl start oes-daemon
  sudo systemctl status oes-daemon

ИТОГО: 40-60 минут
```

---

## Сводная таблица бэкапов OES

```
Что                    | Частота    | Хранение (лок.) | Где хранить
-----------------------|-----------|-----------------|-------------------
Firebird БД (gbak)     | 6ч / 24ч  | 30 дней         | /var/backups + NAS
PostgreSQL (pg_dump)   | 6ч / 24ч  | 30 дней         | /var/backups + NAS
SQLite файлы проектов  | при изм.  | 30 дней         | Рядом с файлом
Конфигурация /etc/oes  | при изм.  | 90 дней         | /var/backups + NAS
Лицензии               | при изм.  | бессрочно       | Несколько локаций
%APPDATA%\OES\ / ~/Library/Application Support/OES/ | 24ч | 30 дней | Внешний диск/NAS
Тест бэкапов           | месяц     | —               | —
```

---

## Мониторинг свежести бэкапов

```bash
#!/bin/bash
# scripts/check-backup-freshness.sh
# Запускать каждый час

BACKUP_DIR="/var/backups/oes/firebird"
MAX_AGE_HOURS=25

LATEST=$(find "$BACKUP_DIR" -name "*.fbk.gz" -type f -printf '%T@\n' 2>/dev/null | sort -rn | head -1)

if [ -z "$LATEST" ]; then
    MSG="КРИТИЧНО: Нет бэкапов Firebird в $BACKUP_DIR"
else
    AGE_SECONDS=$(echo "$(date +%s) - ${LATEST%.*}" | bc)
    AGE_HOURS=$((AGE_SECONDS / 3600))
    
    if [ "$AGE_HOURS" -gt "$MAX_AGE_HOURS" ]; then
        MSG="ПРЕДУПРЕЖДЕНИЕ: Последний бэкап Firebird ${AGE_HOURS}ч назад (макс: ${MAX_AGE_HOURS}ч)"
    else
        exit 0  # Всё OK
    fi
fi

curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
    -d chat_id="$TELEGRAM_CHAT_ID" \
    -d text="$MSG" > /dev/null
```
