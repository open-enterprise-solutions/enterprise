# Engineering Playbook — Open Enterprise Solutions (OES)

Corporate development standards for **Open Enterprise Solutions** — a cross-platform low-code/no-code C++ platform. Rules for project organisation, code management, teamwork, and quality control. Mandatory for all team members and AI agents.

**Stack:** C++17, wxWidgets 3.3.2, MSBuild / CMake, Firebird (primary DBMS), PostgreSQL, SQLite, MySQL, ODBC

**License:** LGPL 2.1

---

## Table of Contents

| # | Document | Description |
|---|----------|----------|
| 01 | [Project structure](./01-project-structure.md) | Repository layout, C++ project file structure, mandatory files |
| 02 | [Git workflow](./02-git-workflow.md) | Branches, commits, pull requests, release tags |
| 03 | [Code review](./03-code-review.md) | C++ code review process, checklist, approval rules |
| 04 | [Documentation](./04-documentation.md) | What to document, where, and how (Doxygen, ADR, CLAUDE.md) |
| 05 | [Task management](./05-task-management.md) | Creating, assigning, and tracking tasks in Jira |
| 06 | [AI agents](./06-ai-agents.md) | Rules for working with AI assistants (Claude, GitHub Copilot) |
| 07 | [Security](./07-security.md) | Secrets, access, SSH, build server protection, code signing |
| 08 | [Onboarding](./08-onboarding.md) | Checklist for new team members, C++/wxWidgets environment setup |
| 09 | [Environments and build](./09-environments-deploy.md) | Local, staging, production — CMake configurations, distribution build |
| 10 | [Testing](./10-testing.md) | Unit, integration, UI tests — Google Test, when and how |
| 11 | [C++ best practices](./11-best-practices.md) | C++17, wxWidgets, patterns, RAII, memory safety, code standards |
| 12 | [Dependency updates](./12-dependency-updates.md) | Process for updating wxWidgets, DB drivers, third-party libraries |
| 13 | [Monitoring and logging](./13-monitoring-logging.md) | wxLog logging, crash reports, health checks, alerts |
| 14 | [UI design and layout](./14-design-workflow.md) | wxWidgets UI guidelines, design system, accessibility, HiDPI |
| 15 | [API design](./15-api-design.md) | Internal C++ API, public headers, Plugin API, versioning |
| 16 | [Database](./16-database.md) | Firebird, PostgreSQL, SQLite — schemas, migrations, backups, multi-tenant |
| 17 | [CI/CD](./17-ci-cd.md) | GitHub Actions, CMake builds, automated tests, signing, and distribution publishing |
| 18 | [Incident management](./18-incident-management.md) | P1-P4, crash dump analysis (WinDbg/GDB), rollback, postmortem |
| 19 | [Performance](./19-performance.md) | C++ profiling, wxWidgets optimization, working with large data sets |
| 20 | [Communication](./20-communication.md) | Channels, async/sync, standups, meeting protocols, code review communication |
| 21 | [Distribution and resilience](./21-infrastructure-resilience.md) | Installer (Inno Setup), auto-update, Windows minidumps, DB connection resilience |
| 22 | [Plugin system](./22-plugin-system.md) | C++ Plugin Architecture: DLL/SO loading, C ABI interfaces, versioning, ABI stability |
| 23 | [Access management](./23-access-management.md) | Access matrix, granting/revoking rights, code signing, secret rotation |
| 24 | [Compliance](./24-compliance.md) | LGPL 2.1, dependency licensing, GDPR for crash reports, OES licensing |

---

## How to use

1. **New team member** — start with [Onboarding](./08-onboarding.md), then read all documents in order
2. **Current team member** — use as a reference for questions about processes and standards
3. **AI agent** — follow [AI agents](./06-ai-agents.md) and `CLAUDE.md` in the root of the OES repository

## Updating the standards

Standards are a living document. If a process doesn't work or a better practice emerges:
1. Create a PR with changes in this repository (`docs/engineering-playbook/`)
2. Discuss the change with the team (Telegram + PR comments)
3. After approval — update `CLAUDE.md` in the root of the repository

## About the project

**Open Enterprise Solutions (OES)** is a cross-platform enterprise low-code/no-code platform that lets you build business applications without writing code. Written in C++17 with wxWidgets 3.3.2. Supports Windows, Linux, and macOS. Distributed under LGPL 2.1.
