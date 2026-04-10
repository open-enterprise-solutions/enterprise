# 23. Управление доступами

## Принцип минимальных привилегий

> Каждый участник получает **минимально необходимый** уровень доступа для выполнения своей работы. Не больше.

---

## Матрица доступов

### GitHub

| Роль | Repos (свой Team) | Repos (чужой Team) | Org settings | Billing |
|------|-------------------|-------------------|-------------|---------|
| Developer | Write | — | — | — |
| Senior/Lead | Write + branch protection | Read (по запросу) | — | — |
| Build/Release | Write + Tags | Read | — | — |
| CTO/Owner | Admin | Admin | Admin | Admin |

### Серверы сборки / CI (SSH)

| Роль | Build Server | Release Storage | Signing Key |
|------|-------------|-----------------|-------------|
| Junior Dev | — | — | — |
| Developer | Read (логи сборки) | — | — |
| Senior/Lead | Full (запуск сборок) | Read | — |
| Build Engineer | Full | Full | Restricted |
| CTO | Full | Full | Full |

### Базы данных (Firebird / PostgreSQL / SQLite)

| Роль | SELECT | INSERT/UPDATE | DELETE | ALTER/DROP | Backup (gbak) |
|------|--------|-------------|--------|-----------|---------------|
| App (ORM) | ✅ | ✅ | ✅ | — | — |
| Developer (staging) | ✅ | ✅ | — | — | — |
| Senior/Lead | ✅ | ✅ | ✅ (staging) | — | — |
| DBA / DevOps | ✅ | ✅ | ✅ | ✅ | ✅ |
| Backup cron | ✅ | — | — | — | ✅ |
| Read-only reporter | ✅ | — | — | — | — |

### Доступ к production-данным клиентов

> **Никто из разработчиков не имеет прямого доступа к production БД клиентов без явного запроса и одобрения.**

| Событие | Кто одобряет | Способ доступа | Фиксация |
|---------|-------------|----------------|---------|
| Диагностика инцидента | CTO + клиент | Временный read-only | Jira-тикет |
| Миграция данных | CTO | Скрипт под наблюдением | PR + лог |
| Восстановление из бэкапа | CTO + клиент | Через DevOps | Протокол |

### Внешние сервисы

| Сервис | Developer | Lead | Build Engineer | CTO |
|--------|-----------|------|----------------|-----|
| GitHub Actions Secrets | — | — | Full | Full |
| Code Signing Certificate | — | — | Restricted | Full |
| Release CDN | — | Read | Full | Full |
| Crash Report Server | Read | Full | — | Full |
| Update Server | — | Read | Full | Full |
| Jira | Write (свой проект) | Admin (свой проект) | — | Admin |
| Telegram Bot (алерты) | — | Read | Full | Full |

### Лицензионные ключи и дистрибутив

| Действие | Роль |
|----------|------|
| Создание trial-ключа клиенту | Lead или CTO |
| Создание коммерческой лицензии | CTO |
| Публикация релиза на сайте | Build Engineer + CTO |
| Подписание инсталлятора | Build Engineer (сертификат в HSM/сейфе) |

---

## Процесс выдачи доступа

### Запрос

```
1. Разработчик → запрос в Telegram/Jira:
   "Нужен доступ к [что] для [зачем] до [дата если временный]"

2. Тимлид → одобряет или отклоняет

3. DevOps/Admin → выдаёт доступ

4. Фиксация:
   - Кто запросил
   - Кто одобрил
   - Что выдано (конкретно)
   - Когда + срок действия (если временный)
```

### GitHub — добавление в Team

```bash
# Через GitHub UI:
# Organization → People → Invite member → Add to Team

# Или через CLI:
gh api orgs/OES-TEAM/teams/TEAM/memberships/USERNAME -f role=member
```

### Доступ к build-серверу

```bash
# 1. Разработчик генерирует ed25519 ключ
ssh-keygen -t ed25519 -C "name@company.com" -f ~/.ssh/oes_build_key

# 2. Отправляет ПУБЛИЧНЫЙ ключ через защищённый канал (не Telegram!)
cat ~/.ssh/oes_build_key.pub

# 3. DevOps добавляет — создаёт отдельного пользователя, не root
useradd -m -s /bin/bash developer_name
mkdir -p /home/developer_name/.ssh
echo "ssh-ed25519 AAAA... name@company.com" \
    >> /home/developer_name/.ssh/authorized_keys
chmod 700  /home/developer_name/.ssh
chmod 600  /home/developer_name/.ssh/authorized_keys

# 4. Ограничить доступ (только логи CMake)
# /etc/sudoers.d/developer_name:
# developer_name ALL=(ALL) NOPASSWD: /usr/bin/cmake --build *
# developer_name ALL=(ALL) NOPASSWD: /usr/bin/tail -f /var/log/build.log
```

### База данных Firebird — пользователи

```sql
-- Firebird: пользователи управляются через gsec или SQL (Firebird 3+)

-- ВАЖНО: Firebird НЕ поддерживает "GRANT ... ON ALL TABLES TO ..."
-- (в отличие от PostgreSQL). Права нужно выдавать пер-таблично.
-- Используйте роли и скрипт для автоматизации:

-- 1. Создать роль
CREATE ROLE ROLE_READONLY;
CREATE ROLE ROLE_APP;

-- 2. Выдать права роли на каждую таблицу (повторить для всех таблиц)
--    Или сгенерировать скрипт из системных таблиц:
-- SELECT 'GRANT SELECT ON ' || RDB$RELATION_NAME || ' TO ROLE_READONLY;'
--   FROM RDB$RELATIONS
--  WHERE RDB$SYSTEM_FLAG = 0 AND RDB$VIEW_BLR IS NULL;
GRANT SELECT ON DOCUMENTS         TO ROLE_READONLY;
GRANT SELECT ON DOCUMENT_SECTIONS TO ROLE_READONLY;
GRANT SELECT ON USERS              TO ROLE_READONLY;
-- ... остальные таблицы ...

GRANT SELECT, INSERT, UPDATE, DELETE ON DOCUMENTS         TO ROLE_APP;
GRANT SELECT, INSERT, UPDATE, DELETE ON DOCUMENT_SECTIONS TO ROLE_APP;
-- ... остальные таблицы ...

-- 3. Создать пользователей и назначить роли
CREATE USER DEV_READONLY PASSWORD 'strong-password';
GRANT ROLE_READONLY TO DEV_READONLY;

CREATE USER OES_APP PASSWORD 'strong-password';
GRANT ROLE_APP TO OES_APP;

-- Пользователь для бэкапа
CREATE USER BACKUP_CRON PASSWORD 'strong-password';
-- gbak требует только CONNECT
```

```sql
-- PostgreSQL (для дополнительных БД)
-- Read-only для разработчика
CREATE USER dev_readonly WITH PASSWORD 'strong-password';
GRANT CONNECT ON DATABASE oes_db TO dev_readonly;
GRANT USAGE ON SCHEMA public TO dev_readonly;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO dev_readonly;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
    GRANT SELECT ON TABLES TO dev_readonly;

-- Полный доступ для приложения
CREATE USER oes_app WITH PASSWORD 'strong-password';
GRANT ALL PRIVILEGES ON DATABASE oes_db TO oes_app;
```

---

## Процесс отзыва доступа

### Когда отзывать

| Событие | Действие | Срок |
|---------|---------|------|
| Увольнение | Отозвать ВСЁ | Немедленно (в тот же день) |
| Перевод на другой проект | Отозвать доступы старого проекта | В течение 1 дня |
| Завершение контракта подрядчика | Отозвать ВСЁ | В последний день |
| Компрометация ключа/пароля | Отозвать + ротировать | Немедленно (в течение часа) |
| Неактивность > 90 дней | Отозвать, можно восстановить | При обнаружении |

### Чеклист отзыва (offboarding)

```
□ GitHub
  □ Удалить из организации
  □ Удалить из всех Teams
  □ Проверить личные access tokens (revoke)
  □ Проверить deploy keys (если персональные)

□ Build / CI серверы
  □ Удалить SSH ключ из authorized_keys на ВСЕХ серверах
  □ Удалить пользователя (userdel -r)
  □ Проверить cron jobs от имени этого пользователя
  □ Проверить screen/tmux сессии

□ Базы данных
  □ Firebird: DROP USER или REVOKE через gsec/isql
  □ PostgreSQL: REVOKE + DROP USER
  □ Сменить shared пароли, если использовались

□ Внешние сервисы
  □ Jira — деактивировать учётную запись
  □ Confluence — деактивировать
  □ Crash Report Server — удалить доступ
  □ Update Server — удалить доступ

□ Подписывающий сертификат
  □ Убедиться, что физический USB-токен возвращён
  □ Проверить, нет ли экспортированных копий

□ Коммуникации
  □ Удалить из Telegram групп проектов
  □ Удалить из Slack/Discord
  □ Отозвать корпоративный email
```

### Скрипт автоматического отзыва доступов

```bash
#!/bin/bash
# offboard.sh — скрипт отзыва доступов при оффбординге
# Использование: ./offboard.sh github_username

USERNAME=$1

if [ -z "$USERNAME" ]; then
  echo "Usage: ./offboard.sh username"
  exit 1
fi

echo "=== Offboarding $USERNAME ==="

# GitHub
echo "Removing from GitHub org..."
gh api -X DELETE orgs/OES-TEAM/members/$USERNAME 2>/dev/null \
  && echo "OK GitHub" || echo "WARN: GitHub (manual check needed)"

# Build-серверы
for SERVER in build.oes-internal.com staging.oes-internal.com; do
  echo "Removing SSH key from $SERVER..."
  ssh admin@$SERVER "
    sudo sed -i '/$USERNAME/d' /home/*/.ssh/authorized_keys 2>/dev/null
    sudo userdel -r $USERNAME 2>/dev/null
  " && echo "OK $SERVER" || echo "WARN: $SERVER (manual check needed)"
done

# Firebird (через isql)
echo "Revoking Firebird access..."
ssh admin@db.oes-internal.com "
  isql -user SYSDBA -pass masterkey <<EOF
  DROP USER ${USERNAME^^};
  COMMIT;
  EXIT;
EOF
" && echo "OK Firebird" || echo "WARN: Firebird (manual check needed)"

echo ""
echo "=== Ручные шаги ==="
echo "□ Jira / Confluence — деактивировать вручную"
echo "□ Telegram группы — удалить вручную"
echo "□ Проверить возврат USB-токена с сертификатом (если выдавался)"
echo "□ Ротировать shared пароли, к которым был доступ"
```

---

## Ротация секретов

### График ротации

| Секрет | Периодичность | Как |
|--------|-------------|-----|
| SSH ключи build-серверов | Раз в год | Перегенерировать, обновить у всех |
| Firebird SYSDBA пароль | Раз в 6 месяцев | `gsec -modify SYSDBA -pw new_password` |
| PostgreSQL пароли | Раз в 6 месяцев | `ALTER USER ... PASSWORD '...'` |
| Code signing certificate | По сроку действия (обычно 1-3 года) | Новый CSR → CA |
| Update server API key | Раз в год | Перегенерировать в панели сервера |
| Crash report server token | Раз в год | Ротировать в конфиге |
| Лицензионный мастер-ключ | При компрометации | Новый ключ + уведомление клиентов |

### При компрометации — немедленно

```
1. Определить что скомпрометировано (ключ, сертификат, пароль)
2. Немедленно заблокировать/отозвать (revoke)
3. Проверить журналы — был ли использован злоумышленником
4. Уведомить команду + руководство
5. Для code signing cert: уведомить CA об отзыве
6. Postmortem: как утёк, как предотвратить повторение
```

---

## Аудит доступов

### Ежемесячно

```
□ Проверить список участников GitHub org — все ли актуальны?
□ Проверить SSH ключи на build-серверах — нет ли лишних?
□ Проверить пользователей Firebird/PostgreSQL — нет ли неиспользуемых?
□ Проверить GitHub personal access tokens — нет ли просроченных?
□ Проверить, у кого есть доступ к code signing сертификату
□ Проверить журнал установок — не устанавливает ли кто-то неподписанные сборки
```

### Инструменты

```bash
# Кто имеет SSH доступ к build-серверу?
cat /home/*/.ssh/authorized_keys

# Пользователи Firebird
isql -user SYSDBA -pass masterkey -e \
  "SELECT SEC\$USER_NAME FROM SEC\$USERS;"

# Пользователи PostgreSQL
sudo -u postgres psql -c \
  "SELECT usename, usesuper, usecreatedb FROM pg_user;"

# Кто в GitHub org?
gh api orgs/OES-TEAM/members --jq '.[].login'

# Активные сессии на build-сервере
who
last -20
```
