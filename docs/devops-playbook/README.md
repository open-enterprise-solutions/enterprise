# OES DevOps Playbook

**Practical DevOps guide for Open Enterprise Solutions (OES)**

Stack: C++17, wxWidgets 3.3.2, MSBuild (Windows) / CMake (macOS/Linux), Firebird (primary, embedded), PostgreSQL, SQLite, MySQL, NSIS/WiX installer, GitHub Actions.

Platforms: Windows (primary, MSBuild), macOS (Homebrew/CMake), Linux (CMake, daemon/server mode).

All examples use abstract server names, paths, and project names. Adapt to your specific OES installation.

---

## Table of Contents

| # | Document | Description |
|---|---------|----------|
| 01 | [Credentials Management](01-credentials-management.md) | OES secret storage: Firebird passwords, signing keys, configs. SOPS, Windows Credential Manager, checklists |
| 02 | [Server Setup from Scratch](02-server-setup.md) | Deploying OES daemon on Linux/Windows Server: from clean OS to production |
| 03 | [Nginx / Reverse Proxy](03-nginx.md) | Reverse proxy in front of OES Daemon, SSL termination, routing |
| 04 | [Process Management](04-service-management.md) | Managing OES daemon processes: systemd (Linux), Windows Service, auto-restart |
| 05 | [Docker](05-docker.md) | Containerizing OES daemon, docker-compose, multi-stage C++ builds |
| 06 | [PostgreSQL Administration](06-postgresql.md) | Setting up PostgreSQL for OES, backups, replication, optimization |
| 07 | [Firebird Administration](07-caching.md) | Configuring Firebird Server, gbak, gfix, monitoring, optimization |
| 08 | [Cloudflare / DNS](08-cloudflare.md) | DNS for OES web components, SSL, DDoS protection, API |
| 09 | [CI/CD with GitHub Actions](09-github-actions.md) | MSBuild/CMake build, cppcheck, Google Test, NSIS installer, code signing, release publishing |
| 10 | [Monitoring](10-monitoring.md) | Crashpad crash reporting, logging, daemon/service monitoring, Firebird health checks |
| 11 | [Backup and Recovery](11-backup-recovery.md) | Firebird gbak, PostgreSQL pg_dump, 3-2-1 strategy, disaster recovery for desktop and server |
| 12 | [Hardening](12-security-hardening.md) | Code signing, DLL hijacking protection, ASLR/DEP/CFG, DPAPI, secure C++ code |
| 13 | [Troubleshooting](13-troubleshooting.md) | Crash dumps, WinDbg/VS analysis, Firebird errors, wxWidgets issues, remote diagnostics |
| 14 | [AI Agent Security](14-ai-agents-security.md) | Safe use of Claude Code / Copilot in a C++ project, protecting secrets, reviewing AI code |
| 15 | [High Availability and Failover](15-high-availability.md) | Firebird standby, PostgreSQL Patroni, Keepalived, HAProxy, multi-node OES, disaster recovery |

---

## Quick start

### For a new developer
1. Read [01-credentials-management.md](01-credentials-management.md) — how to store project secrets
2. Set up CI/CD per [09-github-actions.md](09-github-actions.md) — build and tests on PR
3. Study [14-ai-agents-security.md](14-ai-agents-security.md) — safe use of AI tools

### For first server deployment
1. [02-server-setup.md](02-server-setup.md) — basic Linux/Windows server setup
2. [11-backup-recovery.md](11-backup-recovery.md) — set up Firebird backups **before** going to production
3. [10-monitoring.md](10-monitoring.md) — daemon monitoring and alerts
4. [12-security-hardening.md](12-security-hardening.md) — hardening: Code Signing, least privilege

### When something breaks
- [13-troubleshooting.md](13-troubleshooting.md) — quick checklists and diagnostic commands

---

## OES Architecture: deployment modes

```
Desktop (single user):
  oes.exe + Firebird Embedded (bundled)
  Entry point: src/engine/enterprise/mainApp.cpp
  Designer: src/engine/designer/mainApp.cpp
  Data: %APPDATA%\OES\data\*.fdb           (Windows)
        ~/Library/Application Support/OES/ (macOS)
        ~/.local/share/oes/                (Linux)
  Backups: local + external drive

LAN Server (small office):
  oesd (daemon) + Firebird Server
  Entry point: src/engine/daemon/daemon.cpp
  OES clients connect over the network
  Data: /var/lib/oes/data/*.fdb (Linux/macOS)
        C:\ProgramData\OES\data\ (Windows)

Enterprise Server:
  Multi-node OES daemon + PostgreSQL / Firebird Server
  HAProxy / Keepalived
  Centralized monitoring and backups
```

---

## Documentation conventions

- `10.0.0.1` / `10.0.0.2` — abstract OES server IPs
- `oes-server` — abstract server name
- `oes-daemon` / `OESDaemon` — service/daemon name
- `/var/lib/oes/` — standard Linux data path
- `C:\ProgramData\OES\` — standard Windows data path
- `SYSDBA` / `FB_SYSDBA_PASSWORD` — Firebird credentials (always from environment variables)
- All Linux examples target Ubuntu 22.04/24.04 LTS
- All macOS examples target macOS 13+ (Ventura/Sonoma) with Homebrew
- All Windows examples target Windows 10/11 / Server 2022
- Main solution file: `enterprise.sln` (Windows MSBuild); `CMakeLists.txt` (macOS/Linux)
- Main branches: `master` (production), `develop` (integration)
