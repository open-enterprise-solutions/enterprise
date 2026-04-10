# 04. Управление сервисами OES

> OES может работать как Windows Service (sc.exe, NSSM) или Linux systemd daemon.
> В отличие от Node.js с PM2, C++ приложение регистрируется как нативный системный сервис.

---

## Обзор: режимы запуска OES

```
Desktop режим:
  — Обычный запуск как GUI-приложение (wxWidgets)
  — Entry point: src/engine/enterprise/mainApp.cpp
  — Запускается пользователем, работает пока открыт
  — Никакого менеджера процессов не требуется

Windows Service режим:
  — OES работает как фоновый Windows Service
  — Запускается автоматически при загрузке
  — Инструменты: sc.exe (встроен), NSSM (рекомендуется), WinSW
  — OES может регистрировать себя как сервис самостоятельно

macOS Daemon режим:
  — OES daemon запускается через launchd (launchctl)
  — Entry point: src/engine/daemon/daemon.cpp
  — Plist-файл в /Library/LaunchDaemons/ (system) или ~/Library/LaunchAgents/ (user)
  — Инструменты: launchctl

Linux Daemon режим:
  — OES работает как systemd unit
  — Entry point: src/engine/daemon/daemon.cpp
  — Запускается автоматически при загрузке
  — Инструменты: systemctl, journalctl
```

---

## Windows Service

### Вариант 1: Регистрация через встроенный механизм OES

```cmd
REM OES поддерживает параметры командной строки для управления сервисом
REM (реализуется через Windows Service API в C++)

REM Установить как сервис (от имени Administrator)
"C:\Program Files\OES\oes-daemon.exe" --install-service

REM Удалить сервис
"C:\Program Files\OES\oes-daemon.exe" --uninstall-service

REM Запустить
net start OESDaemon
REM или
sc start OESDaemon

REM Остановить
net stop OESDaemon
REM или
sc stop OESDaemon
```

### Вариант 2: sc.exe (встроенный в Windows)

```cmd
REM Зарегистрировать сервис
sc create OESDaemon ^
  binPath= "\"C:\Program Files\OES\oes-daemon.exe\" --config \"C:\ProgramData\OES\oes.conf\" --service" ^
  DisplayName= "OES Enterprise Platform Daemon" ^
  start= auto ^
  obj= LocalSystem ^
  description= "Open Enterprise Solutions low-code platform server daemon"

REM Установить описание
sc description OESDaemon "OES Enterprise low-code/no-code platform running in server mode"

REM Настройка восстановления при сбое (перезапуск через 10 секунд)
sc failure OESDaemon reset= 86400 actions= restart/10000/restart/30000/restart/60000

REM Запустить
sc start OESDaemon

REM Проверить статус
sc query OESDaemon

REM Остановить
sc stop OESDaemon

REM Удалить
sc delete OESDaemon
```

### Вариант 3: NSSM (Non-Sucking Service Manager) — рекомендуется

NSSM позволяет запустить любой исполняемый файл как сервис с расширенными настройками без изменения кода приложения.

#### Установка NSSM

```powershell
# Через Chocolatey
choco install nssm

# Или скачать с https://nssm.cc/
# Скопировать nssm.exe в C:\Windows\System32\
```

#### Регистрация OES как сервиса

```cmd
REM Интерактивная установка (откроет GUI)
nssm install OESDaemon

REM Установка через командную строку
nssm install OESDaemon "C:\Program Files\OES\oes-daemon.exe"

REM Настройка параметров
nssm set OESDaemon AppParameters "--config \"C:\ProgramData\OES\oes.conf\" --daemon"
nssm set OESDaemon AppDirectory "C:\Program Files\OES"
nssm set OESDaemon DisplayName "OES Enterprise Platform Daemon"
nssm set OESDaemon Description "Open Enterprise Solutions low-code platform in server mode"
nssm set OESDaemon Start SERVICE_AUTO_START

REM Логи через NSSM (stdout/stderr → файл)
nssm set OESDaemon AppStdout "C:\ProgramData\OES\Logs\daemon-out.log"
nssm set OESDaemon AppStderr "C:\ProgramData\OES\Logs\daemon-err.log"
nssm set OESDaemon AppRotateFiles 1
nssm set OESDaemon AppRotateOnline 1
nssm set OESDaemon AppRotateSeconds 86400
nssm set OESDaemon AppRotateBytes 52428800

REM Настройка перезапуска при сбое
nssm set OESDaemon AppExit Default Restart
nssm set OESDaemon AppRestartDelay 5000

REM Зависимости (запускать после PostgreSQL / Firebird)
nssm set OESDaemon DependOnService FirebirdGuardianDefaultInstance

REM Запустить
nssm start OESDaemon

REM Проверить статус
nssm status OESDaemon

REM Остановить
nssm stop OESDaemon

REM Перезапустить
nssm restart OESDaemon

REM Удалить сервис
nssm remove OESDaemon confirm
```

#### Управление через GUI

```cmd
REM Открыть редактор конфигурации сервиса
nssm edit OESDaemon
```

---

## Windows Service: управление через PowerShell

```powershell
# Проверить статус
Get-Service OESDaemon

# Запустить
Start-Service OESDaemon

# Остановить
Stop-Service OESDaemon

# Перезапустить
Restart-Service OESDaemon

# Включить автозапуск
Set-Service OESDaemon -StartupType Automatic

# Отключить автозапуск
Set-Service OESDaemon -StartupType Disabled

# Список всех OES-связанных сервисов
Get-Service | Where-Object { $_.DisplayName -like "*OES*" }

# Подробная информация
Get-Service OESDaemon | Select-Object *

# Логи событий сервиса (Windows Event Log)
Get-EventLog -LogName Application -Source OESDaemon -Newest 20
Get-EventLog -LogName System -Source "Service Control Manager" -Newest 20 |
  Where-Object { $_.Message -like "*OESDaemon*" }
```

---

## Windows Service: скрипты управления

### deploy-windows-service.ps1

```powershell
#!/usr/bin/env pwsh
# deploy-windows-service.ps1 — обновление OES daemon на Windows Server
param(
    [string]$InstallerPath = ".\OES-Setup.exe",
    [string]$ServiceName = "OESDaemon",
    [int]$StopTimeoutSec = 30
)

$ErrorActionPreference = "Stop"

Write-Host "=== Деплой OES Daemon ===" -ForegroundColor Cyan

# Проверить что инсталлятор существует
if (-not (Test-Path $InstallerPath)) {
    Write-Error "Инсталлятор не найден: $InstallerPath"
    exit 1
}

# Остановить сервис
$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($service -and $service.Status -eq "Running") {
    Write-Host "Остановка сервиса $ServiceName..."
    Stop-Service -Name $ServiceName -Force
    $service.WaitForStatus("Stopped", [TimeSpan]::FromSeconds($StopTimeoutSec))
    Write-Host "Сервис остановлен."
}

# Запустить инсталлятор (тихая установка)
Write-Host "Установка обновления..."
$process = Start-Process -FilePath $InstallerPath -ArgumentList "/SILENT /NORESTART" -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Write-Error "Установка завершилась с ошибкой: exit code $($process.ExitCode)"
    exit 1
}

# Запустить сервис
Write-Host "Запуск сервиса $ServiceName..."
Start-Service -Name $ServiceName
Start-Sleep -Seconds 3

# Проверить статус
$service = Get-Service -Name $ServiceName
if ($service.Status -eq "Running") {
    Write-Host "=== Деплой завершён успешно ===" -ForegroundColor Green
    Write-Host "Статус: $($service.Status)"
} else {
    Write-Error "Сервис не запустился! Статус: $($service.Status)"
    Get-EventLog -LogName Application -Source $ServiceName -Newest 10 |
      Format-List TimeGenerated, EntryType, Message
    exit 1
}
```

---

## macOS launchd (Daemon режим)

### Plist файл

```bash
sudo nano /Library/LaunchDaemons/com.oes.daemon.plist
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.oes.daemon</string>

    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/opt/oes/bin/oes-daemon</string>
        <string>--config</string>
        <string>/etc/oes/oes.conf</string>
        <string>--foreground</string>
    </array>

    <key>UserName</key>
    <string>oes</string>

    <key>WorkingDirectory</key>
    <string>/var/lib/oes</string>

    <key>RunAtLoad</key>
    <true/>

    <key>KeepAlive</key>
    <true/>

    <key>StandardOutPath</key>
    <string>/var/log/oes/daemon.log</string>

    <key>StandardErrorPath</key>
    <string>/var/log/oes/daemon-error.log</string>

    <key>EnvironmentVariables</key>
    <dict>
        <key>OES_LOG_LEVEL</key>
        <string>info</string>
    </dict>
</dict>
</plist>
```

### Управление через launchctl

```bash
# Загрузить и запустить
sudo launchctl load /Library/LaunchDaemons/com.oes.daemon.plist

# Остановить и выгрузить
sudo launchctl unload /Library/LaunchDaemons/com.oes.daemon.plist

# Запустить вручную (уже загруженный)
sudo launchctl start com.oes.daemon

# Остановить
sudo launchctl stop com.oes.daemon

# Статус
sudo launchctl list | grep oes

# Логи
tail -f /var/log/oes/daemon.log
```

---

## Linux systemd (Daemon режим)

### Unit файл

```bash
sudo nano /etc/systemd/system/oes-daemon.service
```

```ini
[Unit]
Description=OES Enterprise Platform Daemon
Documentation=https://docs.oes-vendor.com/daemon
After=network-online.target postgresql.service
Wants=network-online.target
# Если используется только Firebird embedded — убрать зависимость от postgresql

[Service]
Type=simple
User=oes
Group=oes
WorkingDirectory=/var/lib/oes
ExecStart=/opt/oes/oes-daemon \
    --config /etc/oes/oes.conf \
    --log-file /var/log/oes/daemon.log \
    --pid-file /run/oes/daemon.pid
ExecReload=/bin/kill -HUP $MAINPID
ExecStop=/bin/kill -TERM $MAINPID

# Перезапуск
Restart=on-failure
RestartSec=10s
StartLimitInterval=120s
StartLimitBurst=5

# Логи (journald + файл через --log-file)
StandardOutput=journal
StandardError=journal
SyslogIdentifier=oes-daemon

# PID файл
RuntimeDirectory=oes
RuntimeDirectoryMode=0755

# Безопасность
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/oes /var/log/oes /tmp
PrivateTmp=yes
PrivateDevices=yes
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
AmbientCapabilities=CAP_NET_BIND_SERVICE

[Install]
WantedBy=multi-user.target
```

### Управление через systemctl

```bash
# Применить изменения unit-файла
sudo systemctl daemon-reload

# Включить автозапуск
sudo systemctl enable oes-daemon

# Запустить
sudo systemctl start oes-daemon

# Остановить
sudo systemctl stop oes-daemon

# Перезапустить (с даунтаймом)
sudo systemctl restart oes-daemon

# Reload конфигурации (без даунтайма, если OES поддерживает SIGHUP)
sudo systemctl reload oes-daemon

# Статус
sudo systemctl status oes-daemon

# Отключить автозапуск
sudo systemctl disable oes-daemon
```

### Журнал логов (journalctl)

```bash
# Последние 50 строк
sudo journalctl -u oes-daemon -n 50

# Следить в реальном времени
sudo journalctl -u oes-daemon -f

# Логи за сегодня
sudo journalctl -u oes-daemon --since today

# Логи за последний час
sudo journalctl -u oes-daemon --since "1 hour ago"

# Логи с конкретного времени
sudo journalctl -u oes-daemon --since "2025-01-15 10:00:00"

# Только ошибки
sudo journalctl -u oes-daemon -p err

# В формате JSON (для парсинга)
sudo journalctl -u oes-daemon -o json | jq '.'

# Экспортировать логи в файл
sudo journalctl -u oes-daemon --since "1 week ago" > /tmp/oes-logs.txt
```

---

## Скрипт деплоя (Linux)

```bash
#!/bin/bash
# /opt/scripts/deploy-oes-daemon.sh
set -e

SERVICE_NAME="oes-daemon"
BINARY_PATH="/opt/oes/oes-daemon"
NEW_BINARY="${1:-/tmp/oes-daemon-new}"
HEALTH_CHECK_URL="http://localhost:8765/health"

echo "=== Деплой OES Daemon (Linux) ==="

# Проверить что новый бинарник существует
if [ ! -f "$NEW_BINARY" ]; then
  echo "ОШИБКА: бинарник не найден: $NEW_BINARY"
  exit 1
fi

# Проверить что бинарник исполняемый
chmod +x "$NEW_BINARY"

# Сохранить старую версию как резервную копию
if [ -f "$BINARY_PATH" ]; then
  cp "$BINARY_PATH" "${BINARY_PATH}.backup"
  echo "Резервная копия: ${BINARY_PATH}.backup"
fi

# Остановить сервис
echo "Остановка $SERVICE_NAME..."
sudo systemctl stop "$SERVICE_NAME"

# Заменить бинарник
echo "Обновление бинарника..."
sudo cp "$NEW_BINARY" "$BINARY_PATH"
sudo chown oes:oes "$BINARY_PATH"
sudo chmod 755 "$BINARY_PATH"

# Запустить сервис
echo "Запуск $SERVICE_NAME..."
sudo systemctl start "$SERVICE_NAME"

# Проверить здоровье
sleep 3
if curl -sf "$HEALTH_CHECK_URL" > /dev/null 2>&1; then
  echo "=== Деплой завершён успешно ==="
  sudo systemctl status "$SERVICE_NAME" --no-pager
else
  echo "ПРЕДУПРЕЖДЕНИЕ: health check не прошёл"
  echo "Проверьте логи: sudo journalctl -u $SERVICE_NAME -n 30"
  # НЕ rollback автоматически — daemon мог запуститься, но endpoint не отвечает сразу
fi

# Удалить временный файл
rm -f "$NEW_BINARY"
```

```bash
# Сделать исполняемым
chmod +x /opt/scripts/deploy-oes-daemon.sh

# Использование
./deploy-oes-daemon.sh /tmp/oes-daemon-new
```

### Rollback

```bash
#!/bin/bash
# /opt/scripts/rollback-oes-daemon.sh
set -e

BINARY_PATH="/opt/oes/oes-daemon"
SERVICE_NAME="oes-daemon"

if [ ! -f "${BINARY_PATH}.backup" ]; then
  echo "ОШИБКА: резервная копия не найдена"
  exit 1
fi

echo "=== Откат OES Daemon ==="
sudo systemctl stop "$SERVICE_NAME"
sudo cp "${BINARY_PATH}.backup" "$BINARY_PATH"
sudo chown oes:oes "$BINARY_PATH"
sudo chmod 755 "$BINARY_PATH"
sudo systemctl start "$SERVICE_NAME"

echo "Откат выполнен."
sudo systemctl status "$SERVICE_NAME" --no-pager
```

---

## Мониторинг процесса

### Linux

```bash
# Статус процесса
sudo systemctl is-active oes-daemon

# PID
cat /run/oes/daemon.pid

# Потребление ресурсов
ps aux | grep oes-daemon
top -p $(cat /run/oes/daemon.pid)

# Открытые файлы / сокеты
sudo lsof -p $(cat /run/oes/daemon.pid)

# Активные подключения к БД (через strace / netstat)
sudo ss -tlnp | grep oes
sudo netstat -tlnp | grep oes-daemon
```

### Windows

```powershell
# Детальный статус с потреблением ресурсов
Get-Process oes-daemon | Select-Object CPU, WorkingSet, VirtualMemorySize, Id

# Или через Task Manager / Process Explorer

# Активные соединения
netstat -ano | findstr "8765"

# Логи из Event Log
Get-EventLog -LogName Application -Newest 20 |
  Where-Object { $_.Source -eq "OESDaemon" } |
  Format-List TimeGenerated, EntryType, Message

# NSSM логи (если используется NSSM)
Get-Content "C:\ProgramData\OES\Logs\daemon-out.log" -Tail 50 -Wait
```

---

## Типичные проблемы

```
Сервис не запускается на Windows:
  1. Проверить Event Log: eventvwr.msc → Windows Logs → Application
  2. Проверить права на oes.conf (должен читаться от имени сервисной учётки)
  3. Проверить путь к oes-daemon.exe в sc/NSSM
  4. Запустить вручную от имени Administrator для отладки:
     "C:\Program Files\OES\oes-daemon.exe" --config "C:\ProgramData\OES\oes.conf" --foreground

Сервис падает и перезапускается (Windows):
  sc query OESDaemon          — проверить состояние
  nssm status OESDaemon       — если используется NSSM
  Проверить C:\ProgramData\OES\Logs\daemon-err.log

Systemd unit не стартует (Linux):
  sudo journalctl -u oes-daemon -n 50   — последние 50 строк
  sudo systemd-analyze verify /etc/systemd/system/oes-daemon.service
  sudo -u oes /opt/oes/oes-daemon --config /etc/oes/oes.conf --check-config

Проблемы с правами к БД:
  Windows: убедиться что сервис запускается от учётки с доступом к C:\ProgramData\OES\
  Linux:   проверить chown oes:oes /var/lib/oes/databases/
```

---

## Сравнение инструментов

```
sc.exe (Windows встроенный):
  + Не требует установки
  + Низкий уровень управления
  - Нет перехвата stdout/stderr в лог-файл (нужно реализовывать в коде)
  - Ограниченные настройки перезапуска

NSSM (рекомендуется для Windows):
  + Запускает любой exe как сервис без изменения кода
  + Автоматически перехватывает stdout/stderr в лог-файлы
  + Расширенные настройки перезапуска и GUI-редактор
  + Ротация логов
  - Требует установки (choco install nssm)

WinSW:
  + XML-конфигурация, хорошо версионируется
  + Не требует отдельного менеджера
  - Менее гибок чем NSSM

launchd (macOS):
  + Встроен в macOS, нативная интеграция
  + Plist-формат конфигурации
  + Автозапуск при загрузке через RunAtLoad
  + Перезапуск через KeepAlive
  - Нет journald-совместимого централизованного логирования
  - Менее гибкие настройки изоляции по сравнению с systemd

systemd (Linux):
  + Встроен в современные дистрибутивы
  + journald (централизованное логирование)
  + Управление зависимостями
  + Изоляция безопасности (namespaces, capabilities)
  + Мониторинг и автоперезапуск
```
