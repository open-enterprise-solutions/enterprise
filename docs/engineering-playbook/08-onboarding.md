# 08. Онбординг

## Цель

Новый участник команды должен в течение первой недели:
- Иметь все необходимые доступы
- Собрать проект локально
- Понять рабочий процесс
- Создать свой первый PR

---

## День 1: Доступы и инструменты

### Получить доступы

| Сервис | Кто выдаёт | Уровень доступа |
|--------|-----------|-----------------|
| GitHub (OES org) | Тимлид | Write (для своих репозиториев) |
| Серверы (SSH) | Тимлид/DevOps | Свой SSH ключ, deploy user |
| Трекер задач (GitHub Issues) | Тимлид | Полный доступ |
| Корпоративный мессенджер | Тимлид | Полный доступ |
| Лицензионные ключи зависимостей | Тимлид | По необходимости |

### Установить инструменты разработки

**Поддерживаемые платформы:**
- **Windows** — основная платформа, сборка через `enterprise.sln` (MSBuild/Visual Studio 2017+)
- **macOS / Linux** — кросс-платформенная цель, сборка через CMake (создаётся отдельно)

**Windows (основная платформа):**

```powershell
# 1. Visual Studio 2019/2022 с компонентами:
#    - Desktop development with C++
#    - Windows SDK (последняя версия)
#    - CMake tools for Visual Studio
# Скачать: https://visualstudio.microsoft.com/

# 2. Git для Windows
# Скачать: https://git-scm.com/download/win
git --version

# 3. CMake (если не установлен через VS)
winget install Kitware.CMake

# 4. vcpkg — менеджер пакетов C++
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
# Добавить C:\vcpkg в PATH

# 5. Firebird (для локальной разработки)
# Скачать Firebird 4.0 с https://firebirdsql.org/
# Установить как сервис

# 6. IBExpert или FlameRobin — GUI для Firebird
# IBExpert: https://ibexpert.net/

# 7. IDE — Visual Studio 2019/2022 или CLion
# CLion: https://www.jetbrains.com/clion/

# 8. Claude Code CLI (опционально)
# Требует Node.js: https://nodejs.org/
npm install -g @anthropic-ai/claude-code
```

**Linux (для кросс-платформенной разработки):**

```bash
# 1. Компилятор и инструменты
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libgtk-3-dev \
    libfirebird-dev \
    libpq-dev \
    libsqlite3-dev
# libgtk-3-dev   — для wxWidgets
# libfirebird-dev — заголовки Firebird (пакет может называться firebird3.0-dev в старых дистрибутивах)
# libpq-dev      — PostgreSQL
# libsqlite3-dev — SQLite

# 2. wxWidgets 3.3.2
wget https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2
tar -xf wxWidgets-3.3.2.tar.bz2
cd wxWidgets-3.3.2
./configure --enable-debug --with-gtk=3
make -j$(nproc)
sudo make install

# 3. Firebird 4.x
# Firebird 4.x недоступен в стандартных репозиториях Ubuntu/Debian.
# Установите из официального репозитория: https://firebirdsql.org/en/firebird-4-0/
# Или через .deb-пакет с GitHub Releases:
# https://github.com/FirebirdSQL/firebird/releases
# Пример для Ubuntu 22.04 (x86_64):
# wget https://github.com/FirebirdSQL/firebird/releases/download/v4.0.4/Firebird-4.0.4.3010-0.amd64.tar.gz
# ... следуйте инструкциям из архива (INSTALL.sh)

# 4. Google Test (для тестов)
sudo apt install libgtest-dev
```

### Настроить Git

```bash
# Имя и email (как в GitHub аккаунте)
git config --global user.name "Your Name"
git config --global user.email "your.email@company.com"

# Основная ветка по умолчанию
git config --global init.defaultBranch master

# Автоматическое удаление tracking веток
git config --global fetch.prune true

# Rebase по умолчанию при pull (вместо merge)
git config --global pull.rebase true

# Для Windows: нормализация переносов строк
git config --global core.autocrlf true
```

### Настроить SSH ключ для GitHub

```bash
# 1. Создать ключ
ssh-keygen -t ed25519 -C "your.email@company.com"
# Нажать Enter для стандартного пути (~/.ssh/id_ed25519)
# Установить пароль (passphrase) — рекомендуется

# 2. Добавить в SSH agent
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# 3. Скопировать публичный ключ
cat ~/.ssh/id_ed25519.pub

# 4. Добавить в GitHub
# GitHub → Settings → SSH and GPG keys → New SSH key

# 5. Проверить подключение
ssh -T git@github.com
# "Hi username! You've successfully authenticated..."
```

---

## День 2: Сборка проекта

### Склонировать репозиторий

```bash
# Создать рабочую папку
mkdir -p ~/projects/oes
cd ~/projects/oes

# Склонировать репозиторий OES
git clone git@github.com:oes-org/enterprise.git
cd enterprise
```

### Настроить конфигурацию

```bash
# 1. Скопировать шаблон конфигурации
cp config.ini.example config.ini

# 2. Отредактировать config.ini
# Заполнить параметры Firebird для локальной БД
# Спросить у тимлида значения если нужны специфические настройки

# Минимальная конфигурация для локальной разработки:
# [database]
# type=firebird
# host=localhost
# port=3050
# database=C:\OES\oes_dev.fdb   (Windows)
# database=/var/lib/oes/oes_dev.fdb  (Linux)
# user=SYSDBA
# password=masterkey   (только для локальной разработки!)
```

### Сборка через MSBuild (Windows — основной способ)

```powershell
# Открыть enterprise.sln в Visual Studio 2019/2022
# Выбрать конфигурацию: Debug | x64

# Или через командную строку (Developer Command Prompt)
cd C:\projects\oes\enterprise

# Проверить что MSBuild доступен
msbuild --version

# Собрать Debug конфигурацию
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# Запустить Designer
.\x64\Debug\OESDesigner.exe

# Запустить Enterprise (runtime)
.\x64\Debug\OESEnterprise.exe
```

### Сборка через CMake (кросс-платформенный способ, в разработке)

```bash
# Создать директорию сборки
mkdir build && cd build

# Конфигурация (Debug)
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DwxWidgets_ROOT_DIR=/usr/local \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Сборка
cmake --build . --config Debug -- -j$(nproc)

# Запуск
./bin/oes
```

### Создать тестовую базу данных Firebird

```bash
# Через isql-fb (укажите пользователя и пароль явно)
isql-fb -user SYSDBA -password masterkey

# В isql:
CREATE DATABASE '/path/to/oes_dev.fdb'
    USER 'SYSDBA' PASSWORD 'masterkey'
    PAGE_SIZE 16384
    DEFAULT CHARACTER SET UTF8;
EXIT;

# Или через IBExpert: File → Create New Database
```

### Проверить что всё работает

1. Приложение запускается без ошибок
2. Подключение к локальной БД Firebird успешно
3. Основные функции (открытие форм, навигация) работают
4. В логах нет критических ошибок

Если что-то не работает — сначала проверьте:
- Запущен ли сервис Firebird (`sc query FirebirdServerDefaultInstance` на Windows)
- Правильно ли заполнен `config.ini` (путь к .fdb файлу, пароль)
- Собраны ли все зависимости (wxWidgets, IBPP)
- Нет ли ошибок компилятора в Output Window

---

## День 2–3: Изучение стандартов

### Обязательно прочитать (в этом порядке)

1. **README.md** проекта — что это, как собрать
2. **CLAUDE.md** проекта — архитектура, бизнес-правила, ключевые решения
3. **Этот гайдлайн** — все документы engineering playbook
4. **Комментарии к ключевым классам** — `DBLayer`, `MainFrame`, `Designer`

### Архитектура OES — краткий обзор

```
src/engine/
├── backend/                  — Ядро платформы
│   ├── appData.cpp           — ibApplicationData: авторизация, AuthenticationAndSetUser()
│   ├── appDataQuery.cpp      — Запросы сессий и пользователей
│   ├── compiler/
│   │   ├── compileCode.cpp   — ibCompileCode, ibTranslateCode
│   │   ├── procUnit.cpp      — ibProcUnit: интерпретатор байткода
│   │   └── value.h           — ibValue, ibNumber (ttmath 128-bit), ibValueTypes
│   ├── databaseLayer/        — ibDatabaseLayer + ibDatabaseLayerFirebird, Postgres и др.
│   │   └── databaseLayer.h   — ibPreparedStatement, ibDatabaseResultSet
│   ├── metaCollection/partial/
│   │   └── commonObjectQuery.cpp — CRUD, CreateAndUpdateTableDB()
│   ├── metadataConfiguration.cpp — Управление конфигурацией метаданных
│   └── debugger/debugServer.cpp  — ibDebuggerServer
├── frontend/                 — wxWidgets UI компоненты
│   └── visualView/ctrl/      — ibValueForm, ibValueFrame, ibValueTextCtrl,
│                               ibValueButton, ibValueModelTableBox
├── designer/mainApp.cpp      — Точка входа дизайнера (ibValueModuleManager, ibCompileModule)
└── enterprise/mainApp.cpp    — Точка входа enterprise (runtime)
```

### Ключевые вещи для запоминания

- Ветки: `master` (production), `develop` (интеграция), `feature/*` (разработка), `fix/*` (баги)
- Коммиты: `type: description` на English
- PR: из feature/fix в `develop`; из `develop` в `master` только при релизе
- Код: C++17, RAII, ibTransactionGuard для транзакций, ibPreparedStatement для запросов
- Исключения: ibBackendCoreException (ошибки движка), ibBackendInterruptException (прерывание скрипта)
- AI: партнёр, не автопилот. Человек ревьюит всё

---

## Первая неделя: Первый PR

### Выбрать задачу

Тимлид назначит первую задачу — обычно что-то небольшое:
- Исправить предупреждение компилятора
- Добавить проверку входных данных
- Заменить сырой указатель на `unique_ptr`
- Исправить пустой блок catch (добавить логирование)
- Обновить документацию

Задача с лейблом `good first issue` — идеальный старт.

### Процесс

```bash
# 1. Обновить develop
git checkout develop
git pull origin develop

# 2. Создать ветку
git checkout -b fix/add-input-validation-login

# 3. Внести изменения
# ... кодить ...

# 4. Собрать и проверить
# В Visual Studio: Build → Build Solution (Ctrl+Shift+B)
# Убедиться что нет новых предупреждений компилятора

# 5. Запустить тесты (если уже настроены)
ctest --output-on-failure

# 6. Запустить статический анализ
cppcheck --enable=all --std=c++17 src/

# 7. Закоммитить
git add src/engine/frontend/visualView/ctrl/ibValueLoginDialog.cpp
git add src/engine/frontend/visualView/ctrl/ibValueLoginDialog.h
git commit -m "fix: add input length validation in login dialog"

# 8. Пушнуть
git push -u origin fix/add-input-validation-login

# 9. Создать PR в GitHub
# Base: develop ← Compare: fix/add-input-validation-login
# Заполнить описание (что, зачем, как тестировать)
```

### Ожидания от первого PR

- Ничего страшного если будут замечания — это нормально
- Ревьюер поможет разобраться со стандартами кода
- Цель — пройти полный цикл: задача → ветка → код → PR → ревью → мердж

---

## Чеклист нового участника

### День 1
- [ ] Получить доступ к GitHub (OES org)
- [ ] Получить доступ к серверам (SSH ключ добавлен)
- [ ] Получить доступ к трекеру задач
- [ ] Установить Visual Studio 2019/2022 с компонентами C++
- [ ] Установить Git для Windows
- [ ] Установить Firebird 4.0 (сервер и клиент)
- [ ] Установить IBExpert или FlameRobin
- [ ] Настроить git config (имя, email, autocrlf)
- [ ] Создать и добавить SSH ключ в GitHub

### День 2
- [ ] Склонировать репозиторий
- [ ] Настроить config.ini
- [ ] Собрать проект (Debug/x64)
- [ ] Создать тестовую БД Firebird
- [ ] Запустить OES, убедиться что работает
- [ ] Прочитать README.md проекта
- [ ] Прочитать CLAUDE.md проекта

### День 3–5
- [ ] Прочитать все документы engineering playbook
- [ ] Разобраться с архитектурой: DBLayer, MainFrame, Designer
- [ ] Получить первую задачу от тимлида
- [ ] Создать feature/fix ветку
- [ ] Написать код
- [ ] Создать PR
- [ ] Пройти ревью
- [ ] Смерджить PR (с помощью ревьюера)

### Конец первой недели
- [ ] Понимаю процесс: задача → ветка → PR → ревью → мердж
- [ ] Могу собрать проект локально без помощи
- [ ] Знаю где искать задачи (GitHub Issues)
- [ ] Знаю как использовать AI-инструменты (Claude Code)
- [ ] Знаю к кому обращаться с вопросами

---

## Полезные команды на каждый день

```bash
# Git — частые операции
git status                          # Текущее состояние
git diff                            # Неотслеживаемые изменения
git log --oneline -10               # Последние 10 коммитов
git stash                           # Временно спрятать изменения
git stash pop                       # Достать спрятанные изменения

# CMake — сборка
cmake --build build --config Debug             # Сборка
cmake --build build --config Debug --target oes_tests  # Сборка тестов
ctest --test-dir build --output-on-failure     # Запуск тестов

# MSBuild (Windows)
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# cppcheck — статический анализ
cppcheck --enable=all --std=c++17 src/

# Firebird — работа с БД
isql-fb -user SYSDBA -password masterkey localhost:path/to/oes.fdb    # Подключиться к БД
gbak -b -user SYSDBA -pass masterkey localhost:oes.fdb oes_backup.fbk  # Бэкап

# Claude Code
claude                              # Запустить Claude Code в текущей папке
```

---

## Вопросы?

Если что-то непонятно — спрашивайте. Нет глупых вопросов, есть плохая документация. Если документация не ответила на ваш вопрос — это повод её улучшить (и хороший первый PR).
