# 10. Мониторинг

> Crash reporting, телеметрия, health checks для daemon-режима OES, мониторинг встроенной БД Firebird.

---

## Что мониторить в OES

```
Настольное приложение (клиентская сторона):
  — Крэши: dump-файлы, стек вызовов (Crashpad/Breakpad)
  — Исключения C++ (необработанные, SEH на Windows)
  — Производительность UI (зависания wxWidgets > 1 сек)
  — Память процесса (утечки, рост > 500 МБ)
  — Время загрузки приложения и открытия документов
  — Ошибки подключения к БД (Firebird embedded)
  — Ошибки сохранения файлов

Daemon/Service (серверная сторона):
  — Статус сервиса (Windows Service / systemd)
  — CPU / RAM процесса сервиса
  — Активные подключения клиентов
  — Время отклика на запросы
  — Ошибки в логах
  — Размер и состояние базы данных Firebird/PostgreSQL

База данных:
  — Firebird: наличие corruption (gfix -validate)
  — PostgreSQL: активные соединения, медленные запросы, размер
  — SQLite: целостность (PRAGMA integrity_check)
  — Свободное место на диске для БД
```

---

## Crash Reporting: Crashpad (рекомендуется для OES)

### Интеграция Crashpad в C++

```cpp
// src/crash_reporter.cpp
#include "client/crashpad_client.h"
#include "client/crash_report_database.h"
#include "client/settings.h"

bool InitCrashpad(const std::string& reports_dir, const std::string& upload_url) {
    // Путь к crashpad_handler.exe (рядом с приложением)
    base::FilePath handler(L"crashpad_handler.exe");
    
    // Директория для хранения дампов
    base::FilePath db_path(base::UTF8ToWide(reports_dir));
    
    // Метрики окружения
    std::map<std::string, std::string> annotations;
    annotations["product"] = "OES";
    annotations["version"] = OES_VERSION_STRING;
    annotations["os"] = "Windows";
    
    // Инициализация
    crashpad::CrashpadClient client;
    bool result = client.StartHandler(
        handler,
        db_path,
        db_path,
        upload_url,      // "" — только локальные дампы, без отправки
        annotations,
        {},
        true,            // restartable
        true             // asynchronous_start
    );
    
    return result;
}

// В main() или WinMain():
// InitCrashpad(GetAppDataPath() + "\\OES\\CrashReports", "");
```

### Структура директории crash reports

```
%APPDATA%\OES\CrashReports\
├── attachments\          — дополнительные файлы (лог приложения)
├── completed\            — обработанные дампы
├── new\                  — новые дампы (ещё не обработаны)
├── pending\              — ожидают отправки
└── settings.dat
```

### Скрипт сбора и анализа дампов (при поддержке)

```powershell
# scripts/analyze-crashes.ps1
# Анализ crash dumps из директории пользователя

param(
    [string]$DumpDir = "$env:APPDATA\OES\CrashReports",
    [string]$SymbolsDir = ".\symbols",
    [string]$ReportDir = ".\crash-analysis"
)

$dumps = Get-ChildItem -Path $DumpDir -Filter "*.dmp" -Recurse
Write-Host "Найдено дампов: $($dumps.Count)"

foreach ($dump in $dumps) {
    Write-Host "Анализ: $($dump.Name)"
    
    # cdb (Windows Debugger) для анализа
    $analysis = & cdb -z $dump.FullName -c "!analyze -v; .ecxr; k; q" 2>&1
    
    $reportFile = Join-Path $ReportDir "$($dump.BaseName)-analysis.txt"
    $analysis | Out-File $reportFile -Encoding UTF8
    
    Write-Host "  Отчёт: $reportFile"
}
```

### Настройка символов для анализа дампов

```
CI/CD при Release-сборке:
  1. MSBuild генерирует .pdb файлы
  2. Сохранить .pdb в symbol server или в артефакты GitHub Release
  3. При анализе дампа: указать путь к символам

В Visual Studio:
  Debug → Options → Debugging → Symbols
  → Symbol file (.pdb) locations: \\symbol-server\OES\1.2.3\

Команды для dump анализа:
  windbg -z crash.dmp -y "srv*c:\symbols*https://msdl.microsoft.com/download/symbols;.\symbols"
```

---

## Логирование приложения

### Простой файловый логгер для OES

```cpp
// src/logger.h
#pragma once
#include <wx/log.h>
#include <wx/file.h>

class OESLogger : public wxLog {
public:
    static void Init(const wxString& logPath) {
        wxLog::SetActiveTarget(new OESLogger(logPath));
        wxLog::SetLogLevel(wxLOG_Debug);
    }
    
    static void SetLevel(wxLogLevel level) {
        wxLog::SetLogLevel(level);
    }
    
protected:
    void DoLogTextAtLevel(wxLogLevel level, const wxString& msg) override {
        wxString prefix;
        switch (level) {
            case wxLOG_Error:   prefix = "[ERROR]"; break;
            case wxLOG_Warning: prefix = "[WARN] "; break;
            case wxLOG_Info:    prefix = "[INFO] "; break;
            default:            prefix = "[DEBUG]"; break;
        }
        
        wxString line = wxNow() + " " + prefix + " " + msg + "\n";
        m_file.Write(line);
        m_file.Flush();
    }
    
private:
    explicit OESLogger(const wxString& path) : m_file(path, wxFile::write_append) {}
    wxFile m_file;
};
```

### Ротация логов

```cpp
// Ограничить размер лога при запуске
void RotateLogIfNeeded(const wxString& logPath, size_t maxSizeMb = 10) {
    wxFileName fn(logPath);
    if (fn.GetSize() > maxSizeMb * 1024 * 1024) {
        wxString archivePath = logPath + ".1";
        wxRemoveFile(archivePath);
        wxRenameFile(logPath, archivePath);
    }
}
```

### Расположение файлов логов

```
Windows:
  %APPDATA%\OES\Logs\oes.log         — основной лог
  %APPDATA%\OES\Logs\oes.log.1       — предыдущий лог
  %APPDATA%\OES\Logs\oes-daemon.log  — лог daemon/service

macOS (Desktop):
  ~/Library/Logs/OES/oes.log
  ~/Library/Logs/DiagnosticReports/  — crash reports
macOS (Daemon — launchd):
  /var/log/oes/daemon.log
  /var/log/oes/daemon-error.log

Linux (Desktop):
  ~/.local/share/OES/logs/oes.log
Linux (Daemon — systemd):
  /var/log/oes/oes-daemon.log        — в режиме сервиса
  journalctl -u oes-daemon           — через journald
```

---

## Мониторинг Windows Service (daemon-режим)

### Скрипт проверки состояния сервиса

```powershell
#!/usr/bin/env pwsh
# scripts/check-oes-service.ps1
# Запускать через Task Scheduler каждые 5 минут

$ServiceName = "OESDaemon"
$TelegramToken = $env:TELEGRAM_BOT_TOKEN
$TelegramChatId = $env:TELEGRAM_CHAT_ID

function Send-TelegramAlert {
    param([string]$Message)
    if (-not $TelegramToken) { return }
    
    Invoke-RestMethod `
        -Uri "https://api.telegram.org/bot$TelegramToken/sendMessage" `
        -Method POST `
        -ContentType "application/json" `
        -Body (ConvertTo-Json @{
            chat_id = $TelegramChatId
            text = $Message
        }) | Out-Null
}

$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue

if ($null -eq $service) {
    Send-TelegramAlert "КРИТИЧНО: Сервис $ServiceName не найден на $(hostname)"
    exit 1
}

if ($service.Status -ne "Running") {
    $msg = "КРИТИЧНО: Сервис $ServiceName остановлен ($(hostname)). Попытка перезапуска..."
    Write-Host $msg
    Send-TelegramAlert $msg
    
    # Попытка автовосстановления
    Start-Service -Name $ServiceName
    Start-Sleep -Seconds 5
    
    $service.Refresh()
    if ($service.Status -eq "Running") {
        Send-TelegramAlert "INFO: Сервис $ServiceName успешно перезапущен на $(hostname)"
    } else {
        Send-TelegramAlert "КРИТИЧНО: Сервис $ServiceName не удалось запустить на $(hostname)"
        exit 1
    }
} else {
    Write-Host "OK: Сервис $ServiceName работает"
}

# Проверить потребление памяти
$proc = Get-Process -Name "oesd" -ErrorAction SilentlyContinue
if ($proc -and $proc.WorkingSet64 -gt 500MB) {
    $memMb = [math]::Round($proc.WorkingSet64 / 1MB)
    Send-TelegramAlert "ПРЕДУПРЕЖДЕНИЕ: OES daemon использует ${memMb} МБ памяти на $(hostname)"
}
```

### macOS launchd мониторинг

```bash
# Статус daemon (macOS)
sudo launchctl list | grep oes

# Логи daemon
tail -f /var/log/oes/daemon.log

# Через unified logging (macOS 10.12+)
log show --predicate 'process == "oes-daemon"' --last 1h --style syslog

# Перезапуск если daemon упал
sudo launchctl stop com.oes.daemon
sudo launchctl start com.oes.daemon
```

### Systemd unit для Linux (daemon-режим)

```ini
# /etc/systemd/system/oes-daemon.service
[Unit]
Description=OES Enterprise Daemon
After=network.target firebird3.0-guardian.service
Wants=firebird3.0-guardian.service

[Service]
Type=simple
User=oes
Group=oes
ExecStart=/opt/oes/bin/oesd --config /etc/oes/daemon.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=10s
StartLimitInterval=60s
StartLimitBurst=3

# Лог через journald
StandardOutput=journal
StandardError=journal
SyslogIdentifier=oes-daemon

# Безопасность
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/oes /var/log/oes

[Install]
WantedBy=multi-user.target
```

```bash
# Команды управления
sudo systemctl enable oes-daemon
sudo systemctl start oes-daemon
sudo systemctl status oes-daemon
sudo journalctl -u oes-daemon -f          # Логи в реальном времени
sudo journalctl -u oes-daemon --since "1 hour ago"
```

---

## Мониторинг базы данных Firebird

### Скрипт проверки целостности БД

```bash
#!/bin/bash
# scripts/check-firebird-db.sh
# Запускать ежедневно через cron

DB_PATH="/var/lib/oes/data/oes.fdb"
LOG_FILE="/var/log/oes/db-check.log"
TELEGRAM_TOKEN="$TELEGRAM_BOT_TOKEN"
TELEGRAM_CHAT="$TELEGRAM_CHAT_ID"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"; }

send_alert() {
    if [ -n "$TELEGRAM_TOKEN" ]; then
        curl -s -X POST \
            "https://api.telegram.org/bot${TELEGRAM_TOKEN}/sendMessage" \
            -d "chat_id=${TELEGRAM_CHAT}" \
            -d "text=$1" > /dev/null
    fi
}

# Проверка что файл существует и не пустой
if [ ! -f "$DB_PATH" ]; then
    log "ОШИБКА: файл базы данных не найден: $DB_PATH"
    send_alert "КРИТИЧНО: БД OES не найдена на $(hostname)"
    exit 1
fi

DB_SIZE=$(du -h "$DB_PATH" | cut -f1)
log "Размер БД: $DB_SIZE"

# Проверка целостности через gfix
log "Запуск gfix -validate..."
VALIDATE_RESULT=$(gfix -user SYSDBA -password masterkey -validate -full "$DB_PATH" 2>&1)

if echo "$VALIDATE_RESULT" | grep -qi "error\|corruption\|damaged"; then
    log "ОШИБКА ЦЕЛОСТНОСТИ: $VALIDATE_RESULT"
    send_alert "КРИТИЧНО: Ошибка целостности БД OES на $(hostname)! Немедленно проверьте."
    exit 1
else
    log "Проверка целостности: OK"
fi

# Проверка свободного места (минимум 1 ГБ)
FREE_KB=$(df "$(dirname "$DB_PATH")" | tail -1 | awk '{print $4}')
if [ "$FREE_KB" -lt 1048576 ]; then
    FREE_MB=$((FREE_KB / 1024))
    log "ПРЕДУПРЕЖДЕНИЕ: мало свободного места для БД: ${FREE_MB} МБ"
    send_alert "ПРЕДУПРЕЖДЕНИЕ: Мало места для БД OES на $(hostname): ${FREE_MB} МБ"
fi

log "Проверка БД завершена успешно"
```

### Мониторинг PostgreSQL для OES (если используется)

```bash
#!/bin/bash
# scripts/check-oes-postgresql.sh

PG_HOST="localhost"
PG_PORT="5432"
PG_DB="oes_db"
PG_USER="oes_user"

# Проверить доступность
if ! pg_isready -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" > /dev/null 2>&1; then
    echo "КРИТИЧНО: PostgreSQL недоступен"
    exit 1
fi

# Проверить активные соединения
CONN_COUNT=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
    -t -c "SELECT count(*) FROM pg_stat_activity WHERE datname = '$PG_DB';" | tr -d ' ')

MAX_CONN=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
    -t -c "SHOW max_connections;" | tr -d ' ')

echo "Соединений: $CONN_COUNT / $MAX_CONN"

# Алерт если > 80%
THRESHOLD=$((MAX_CONN * 80 / 100))
if [ "$CONN_COUNT" -gt "$THRESHOLD" ]; then
    echo "ПРЕДУПРЕЖДЕНИЕ: много активных соединений ($CONN_COUNT/$MAX_CONN)"
fi

# Медленные запросы (> 5 секунд)
SLOW_QUERIES=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
    -t -c "SELECT count(*) FROM pg_stat_activity 
           WHERE state = 'active' AND query_start < now() - interval '5 seconds';")

if [ "$SLOW_QUERIES" -gt 0 ]; then
    echo "ПРЕДУПРЕЖДЕНИЕ: $SLOW_QUERIES медленных запросов"
fi
```

---

## Простая телеметрия использования (opt-in)

```cpp
// src/telemetry.cpp
// Анонимная телеметрия — только при явном согласии пользователя

class Telemetry {
public:
    static Telemetry& Instance() {
        static Telemetry instance;
        return instance;
    }
    
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    
    // Записать событие использования в локальный файл
    void TrackEvent(const wxString& category, const wxString& action) {
        if (!m_enabled) return;
        
        // Только локальный файл, никакой отправки без явной кнопки
        wxLogMessage("[TELEMETRY] %s / %s", category, action);
    }
    
    // Статистика запусков (для диагностики)
    void IncrementLaunchCount() {
        wxConfig config("OES", "Tetracode");
        long count = 0;
        config.Read("/Telemetry/LaunchCount", &count, 0);
        config.Write("/Telemetry/LaunchCount", count + 1);
        config.Write("/Telemetry/LastLaunch", wxDateTime::Now().FormatISOCombined());
    }
    
private:
    bool m_enabled = false;  // По умолчанию отключена
};
```

---

## Мониторинг Docker-контейнеров (для daemon-режима в контейнере)

```bash
#!/bin/bash
# scripts/check-oes-containers.sh

REQUIRED="oes-daemon oes-db"
ISSUES=""

for name in $REQUIRED; do
    STATUS=$(docker inspect --format='{{.State.Status}}' "$name" 2>/dev/null)
    if [ "$STATUS" != "running" ]; then
        ISSUES="$ISSUES\nCRITICAL: $name не запущен ($STATUS)"
    else
        echo "OK: $name запущен"
    fi
done

if [ -n "$ISSUES" ]; then
    echo -e "$ISSUES"
    curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
        -d chat_id="$TELEGRAM_CHAT_ID" \
        -d "text=OES Docker Alert:$(echo -e "$ISSUES")" > /dev/null
fi
```

---

## Чеклист мониторинга OES

```
Crash reporting:
  [ ] Crashpad инициализирован при старте приложения
  [ ] .pdb символы сохраняются при каждом Release-билде
  [ ] Директория crash dumps настроена и доступна
  [ ] Crash dumps включены в отчёты об ошибках от пользователей

Логирование:
  [ ] Файловый логгер инициализирован в main()
  [ ] Уровень логирования зависит от режима (Debug/Release)
  [ ] Ротация логов настроена (макс. размер 10 МБ)
  [ ] Логи включаются в диагностические пакеты

Daemon/Service (если применимо):
  [ ] Windows Service настроен на автозапуск
  [ ] Systemd unit настроен с Restart=on-failure
  [ ] Скрипт проверки сервиса в Task Scheduler / cron
  [ ] Алерты в Telegram при остановке сервиса

База данных:
  [ ] Ежедневная проверка целостности Firebird (gfix)
  [ ] Мониторинг свободного места под БД
  [ ] PostgreSQL: мониторинг соединений и медленных запросов
  [ ] Алерт при ошибках подключения к БД

Производительность:
  [ ] Мониторинг потребления памяти процессом
  [ ] Время загрузки приложения логируется
```
