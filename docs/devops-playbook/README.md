# OES DevOps Playbook

**Практическое руководство по DevOps для Open Enterprise Solutions (OES)**

Стек: C++17, wxWidgets 3.3.2, MSBuild (Windows) / CMake (macOS/Linux), Firebird (primary, embedded), PostgreSQL, SQLite, MySQL, NSIS/WiX installer, GitHub Actions.

Платформы: Windows (primary, MSBuild), macOS (Homebrew/CMake), Linux (CMake, daemon/server mode).

Все примеры используют абстрактные имена серверов, путей и проектов. Адаптируйте под конкретную инсталляцию OES.

---

## Оглавление

| # | Документ | Описание |
|---|---------|----------|
| 01 | [Управление учётными данными](01-credentials-management.md) | Хранение секретов OES: Firebird пароли, ключи подписи, конфиги. SOPS, Windows Credential Manager, чеклисты |
| 02 | [Настройка сервера с нуля](02-server-setup.md) | Развёртывание OES daemon на Linux/Windows Server: от чистой ОС до production |
| 03 | [Nginx / Reverse Proxy](03-nginx.md) | Reverse proxy перед OES Daemon, SSL терминация, маршрутизация |
| 04 | [Управление процессами](04-service-management.md) | Управление OES daemon процессами: systemd (Linux), Windows Service, автоперезапуск |
| 05 | [Docker](05-docker.md) | Контейнеризация OES daemon, docker-compose, multi-stage C++ builds |
| 06 | [PostgreSQL администрирование](06-postgresql.md) | Настройка PostgreSQL для OES, бэкапы, репликация, оптимизация |
| 07 | [Firebird администрирование](07-caching.md) | Настройка Firebird Server, gbak, gfix, мониторинг, оптимизация |
| 08 | [Cloudflare / DNS](08-cloudflare.md) | DNS для OES web-компонентов, SSL, защита от DDoS, API |
| 09 | [CI/CD с GitHub Actions](09-github-actions.md) | MSBuild/CMake сборка, cppcheck, Google Test, NSIS инсталлятор, подписание кода, публикация релизов |
| 10 | [Мониторинг](10-monitoring.md) | Crashpad crash reporting, логирование, мониторинг daemon/service, Firebird health checks |
| 11 | [Бэкапы и восстановление](11-backup-recovery.md) | Firebird gbak, PostgreSQL pg_dump, стратегия 3-2-1, disaster recovery для desktop и server |
| 12 | [Харденинг](12-security-hardening.md) | Code signing, DLL hijacking protection, ASLR/DEP/CFG, DPAPI, безопасный C++ код |
| 13 | [Диагностика проблем](13-troubleshooting.md) | Crash dumps, WinDbg/VS анализ, Firebird ошибки, wxWidgets проблемы, удалённая диагностика |
| 14 | [Безопасность AI-агентов](14-ai-agents-security.md) | Безопасная работа с Claude Code / Copilot в C++ проекте, защита секретов, ревью AI-кода |
| 15 | [High Availability и Failover](15-high-availability.md) | Firebird standby, PostgreSQL Patroni, Keepalived, HAProxy, multi-node OES, disaster recovery |

---

## Быстрый старт

### Для нового разработчика
1. Прочитайте [01-credentials-management.md](01-credentials-management.md) — как хранить секреты проекта
2. Настройте CI/CD по [09-github-actions.md](09-github-actions.md) — сборка и тесты на PR
3. Изучите [14-ai-agents-security.md](14-ai-agents-security.md) — безопасная работа с AI-инструментами

### Для первого развёртывания на сервере
1. [02-server-setup.md](02-server-setup.md) — базовая настройка Linux/Windows сервера
2. [11-backup-recovery.md](11-backup-recovery.md) — настроить бэкапы Firebird **до** запуска в production
3. [10-monitoring.md](10-monitoring.md) — мониторинг daemon и алерты
4. [12-security-hardening.md](12-security-hardening.md) — харденинг: Code Signing, минимальные права

### При проблемах
- [13-troubleshooting.md](13-troubleshooting.md) — быстрые чеклисты и команды диагностики

---

## Архитектура OES: режимы развёртывания

```
Desktop (одиночный пользователь):
  oes.exe + Firebird Embedded (встроенная)
  Entry point: src/engine/enterprise/mainApp.cpp
  Designer: src/engine/designer/mainApp.cpp
  Данные: %APPDATA%\OES\data\*.fdb          (Windows)
          ~/Library/Application Support/OES/ (macOS)
          ~/.local/share/oes/               (Linux)
  Бэкапы: локальные + внешний диск

LAN Server (малый офис):
  oesd (daemon) + Firebird Server
  Entry point: src/engine/daemon/daemon.cpp
  Клиенты OES подключаются по сети
  Данные: /var/lib/oes/data/*.fdb (Linux/macOS)
          C:\ProgramData\OES\data\ (Windows)

Enterprise Server:
  Multi-node OES daemon + PostgreSQL / Firebird Server
  HAProxy / Keepalived
  Централизованный мониторинг и бэкапы
```

---

## Соглашения в документации

- `10.0.0.1` / `10.0.0.2` — абстрактные IP серверов OES
- `oes-server` — абстрактное имя сервера
- `oes-daemon` / `OESDaemon` — имя сервиса/daemon
- `/var/lib/oes/` — стандартный путь данных на Linux
- `C:\ProgramData\OES\` — стандартный путь данных на Windows
- `SYSDBA` / `FB_SYSDBA_PASSWORD` — учётные данные Firebird (всегда из переменных окружения)
- Все примеры для Linux рассчитаны на Ubuntu 22.04/24.04 LTS
- Все примеры для macOS рассчитаны на macOS 13+ (Ventura/Sonoma) с Homebrew
- Все примеры для Windows рассчитаны на Windows 10/11 / Server 2022
- Основной файл решения: `enterprise.sln` (Windows MSBuild); `CMakeLists.txt` (macOS/Linux)
- Основные ветки: `master` (production), `develop` (интеграция)
