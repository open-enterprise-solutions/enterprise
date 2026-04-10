# 15. High Availability и Failover

## Уровни отказоустойчивости для OES

| Уровень | Что включает | Downtime | Для кого |
|---------|-------------|----------|----------|
| **Desktop** | Встроенная Firebird, локальные бэкапы | нет (local) | Одиночный пользователь |
| **Basic Server** | OES Daemon + Firebird Server, автоперезапуск, бэкапы | минуты | Малый офис, LAN deployment |
| **Standard** | + PostgreSQL replica, failover, балансировка | секунды | Средний бизнес, несколько серверов |
| **Enterprise** | + Multi-node, автоматический failover, geo-redundancy | ~0 | Крупные предприятия, SLA |

---

## Desktop: надёжность локальной установки

```
Встроенный Firebird (Embedded):
  — БД = один .fdb файл рядом с данными пользователя
  — Нет сетевых зависимостей
  — Защита: ежедневные бэкапы через gbak
  — При повреждении: восстановить из бэкапа (< 10 мин)
```

### Автоматическое резервное копирование при запуске

```cpp
// src/engine/enterprise/startup_backup.cpp
// Создать быстрый бэкап при старте если предыдущий > 24 часов
// Вызывается из ibApplicationData::AuthenticationAndSetUser() после успешного входа

void CreateStartupBackup(const wxString& dbPath) {
    wxString backupDir = GetAppDataPath() + wxFILE_SEP_PATH + "Backups";
    wxFileName::Mkdir(backupDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    
    wxDateTime now = wxDateTime::Now();
    wxString backupPath = backupDir + wxFILE_SEP_PATH +
        "oes_" + now.Format("%Y-%m-%d") + ".fbk";
    
    // Проверить если бэкап за сегодня уже есть
    if (wxFileExists(backupPath)) return;
    
    // Запустить gbak асинхронно (не блокировать запуск)
    // Пароль заключается в кавычки на случай спецсимволов
    wxString cmd = wxString::Format(
        "gbak -backup -user SYSDBA -password \"%s\" \"%s\" \"%s\"",
        wxGetenv("FB_PASSWORD"), dbPath, backupPath
    );
    
    wxExecute(cmd, wxEXEC_ASYNC);
    wxLogMessage("Запущено резервное копирование: %s", backupPath);
}
```

---

## Basic Server: автоматический перезапуск

### Windows Service: Recovery Actions

```powershell
# Настроить автовосстановление Windows Service OES

sc.exe failure OESDaemon `
    reset= 86400 `
    actions= restart/5000/restart/10000/restart/30000
# restart через 5, 10, 30 секунд при падении
# Сброс счётчика через 24 часа (86400 сек)

# Или через PowerShell:
$service = Get-WmiObject Win32_Service | Where-Object { $_.Name -eq "OESDaemon" }
# (использовать sc.exe — проще и надёжнее)
```

### Systemd: конфигурация автоперезапуска

```ini
# /etc/systemd/system/oes-daemon.service
[Unit]
Description=OES Enterprise Daemon
After=network.target firebird3.0-guardian.service
Wants=firebird3.0-guardian.service
StartLimitIntervalSec=300        # окно ограничения запусков
StartLimitBurst=5                # максимум 5 попыток за 5 минут

[Service]
Type=simple
User=oes
Group=oes
ExecStart=/opt/oes/bin/oesd --config /etc/oes/daemon.conf
Restart=on-failure
RestartSec=10s                   # ждать 10 сек перед перезапуском
TimeoutStopSec=30s
KillMode=mixed
KillSignal=SIGTERM

# Watchdog (OES должен поддерживать sd_notify)
# WatchdogSec=60s

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable oes-daemon
sudo systemctl start oes-daemon

# Проверить конфигурацию автоперезапуска
sudo systemctl show oes-daemon | grep -E "Restart|StartLimit"
```

---

## Standard: репликация и failover

### Firebird: Master + Standby (OES Server mode)

```
Для Firebird Classic/Super Server (не Embedded):

┌─────────────────┐   gbak + rsync    ┌─────────────────┐
│  Firebird Master │ ──────────────→  │  Firebird Standby│
│  (R/W активная) │                   │  (warm standby) │
│  10.0.0.1:3050  │                   │  10.0.0.2:3050  │
└─────────────────┘                   └─────────────────┘
         ↑                                     ↑
   OES clients                         Failover target
```

**Стратегия репликации Firebird через gbak:**

```bash
#!/bin/bash
# scripts/firebird-sync.sh
# Запускать каждые 15 минут для warm standby

PRIMARY_HOST="10.0.0.1"
PRIMARY_DB="/var/lib/oes/data/oes.fdb"
STANDBY_DB="/var/lib/oes/data/oes.fdb"
FB_USER="SYSDBA"
FB_PASS="${FB_SYSDBA_PASSWORD}"
TEMP_BACKUP="/tmp/oes_sync_$(date +%s).fbk"

# Создать бэкап с primary
gbak \
    -backup \
    -user "$FB_USER" \
    -password "$FB_PASS" \
    "${PRIMARY_HOST}:${PRIMARY_DB}" \
    "$TEMP_BACKUP"

# Скопировать на standby (если запускается локально на standby)
# Или использовать:
# gbak -backup "user:pass@primary:/path/db.fdb" /tmp/backup.fbk
# gbak -restore /tmp/backup.fbk "user:pass@standby:/path/db.fdb" -replace_database

# Восстановить на standby (заменить существующую)
gbak \
    -restore \
    -user "$FB_USER" \
    -password "$FB_PASS" \
    -replace_database \
    "$TEMP_BACKUP" \
    "$STANDBY_DB"

rm -f "$TEMP_BACKUP"
echo "[$(date)] Синхронизация Firebird standby завершена"
```

```bash
# Cron: каждые 15 минут
*/15 * * * * /opt/oes/scripts/firebird-sync.sh >> /var/log/oes/fb-sync.log 2>&1
```

### PostgreSQL: Master + Replica (если используется)

```
┌─────────────┐   WAL streaming   ┌─────────────┐
│   Master    │ ────────────────→ │   Replica   │
│  (R/W)      │                   │  (R/O)      │
│  10.0.0.1   │                   │  10.0.0.2   │
└─────────────┘                   └─────────────┘
       ↑
   OES Daemon
```

**Настройка PostgreSQL репликации:**

```conf
# postgresql.conf (Master)
wal_level = replica
max_wal_senders = 5
wal_keep_size = 1024
synchronous_commit = on
```

```bash
# Создать пользователя репликации
sudo -u postgres psql -c "CREATE USER oes_replicator WITH REPLICATION PASSWORD 'strong-password';"

# Начальная копия на Replica
sudo systemctl stop postgresql
sudo rm -rf /var/lib/postgresql/16/main/*
sudo -u postgres pg_basebackup \
    -h 10.0.0.1 \
    -U oes_replicator \
    -D /var/lib/postgresql/16/main \
    -Fp -Xs -P
sudo -u postgres touch /var/lib/postgresql/16/main/standby.signal
```

### Скрипт автоматического failover

```bash
#!/bin/bash
# scripts/oes-failover.sh
# Переключиться на standby при недоступности primary

PRIMARY_HOST="10.0.0.1"
STANDBY_HOST="10.0.0.2"
OES_CONFIG="/etc/oes/daemon.conf"
CHECK_TIMEOUT=10
FAIL_COUNT=0
MAX_FAILS=3

check_primary() {
    # Проверить доступность Firebird на primary
    nc -z -w "$CHECK_TIMEOUT" "$PRIMARY_HOST" 3050 2>/dev/null
    return $?
}

promote_standby() {
    echo "[$(date)] Переключение на standby: $STANDBY_HOST"
    
    # Обновить конфигурацию OES daemon
    sed -i "s/Host=.*/Host=$STANDBY_HOST/" "$OES_CONFIG"
    
    # Перезапустить OES daemon
    systemctl restart oes-daemon
    
    # Уведомить команду
    curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
        -d chat_id="$TELEGRAM_CHAT_ID" \
        -d "text=FAILOVER: OES переключился на standby ($STANDBY_HOST). Primary ($PRIMARY_HOST) недоступен."
    
    echo "[$(date)] Failover завершён. OES теперь использует: $STANDBY_HOST"
}

# Проверить primary
for i in $(seq 1 $MAX_FAILS); do
    if check_primary; then
        echo "[$(date)] Primary OK: $PRIMARY_HOST"
        exit 0
    fi
    FAIL_COUNT=$((FAIL_COUNT + 1))
    echo "[$(date)] Попытка $FAIL_COUNT/$MAX_FAILS: $PRIMARY_HOST недоступен"
    sleep 5
done

echo "[$(date)] Primary недоступен $MAX_FAILS раз подряд. Запуск failover."
promote_standby
```

---

## Enterprise: Multi-node кластер

### Архитектура OES Multi-node

```
                    ┌──────────────────────┐
                    │  Load Balancer / HAProxy │
                    │  VIP: 10.0.0.100:4001   │
                    └──────────┬───────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
        ┌─────▼─────┐    ┌────▼────┐    ┌──────▼─────┐
        │  OES Node1 │    │ Node 2  │    │  OES Node3 │
        │  Daemon    │    │ Daemon  │    │  Daemon    │
        │  10.0.0.1  │    │10.0.0.2 │    │  10.0.0.3  │
        └─────┬──────┘    └────┬────┘    └──────┬─────┘
              │                │                │
        ┌─────▼────────────────▼────────────────▼─────┐
        │            Shared Storage (NFS / Ceph)       │
        │         Firebird БД: /mnt/shared/oes.fdb     │
        │      ИЛИ  PostgreSQL кластер (Patroni)       │
        └──────────────────────────────────────────────┘
```

### HAProxy для балансировки OES Daemon

```conf
# /etc/haproxy/haproxy.cfg

global
    log /dev/log local0
    maxconn 4096

defaults
    log global
    timeout connect 5s
    timeout client  30s
    timeout server  30s

frontend oes_frontend
    bind *:4001
    default_backend oes_backends

backend oes_backends
    balance leastconn
    option tcp-check

    server oes1 10.0.0.1:4001 check inter 5s fall 3 rise 2
    server oes2 10.0.0.2:4001 check inter 5s fall 3 rise 2
    server oes3 10.0.0.3:4001 check inter 5s fall 3 rise 2 backup
```

```bash
sudo systemctl enable haproxy
sudo systemctl start haproxy

# Статус
echo "show stat" | sudo socat /var/run/haproxy/admin.sock stdio | head -5
```

### Keepalived: Floating IP для Active/Passive

```
Когда OES Daemon не поддерживает горизонтальное масштабирование
(один процесс с exclusive lock на .fdb):

┌─────────────────┐  VIP: 10.0.0.100  ┌─────────────────┐
│  OES Primary    │←─────────────────→│  OES Secondary  │
│  MASTER         │   keepalived       │  BACKUP         │
│  10.0.0.1       │                    │  10.0.0.2       │
│  (активен)      │                    │  (в ожидании)   │
└─────────────────┘                    └─────────────────┘

Primary упал → VIP переезжает на Secondary → клиенты переподключаются
```

```conf
# /etc/keepalived/keepalived.conf (Primary — MASTER)
vrrp_script check_oes {
    script "/usr/bin/systemctl is-active oes-daemon"
    interval 5
    weight -30
    fall 3
    rise 2
}

vrrp_instance OES_VI {
    state MASTER
    interface eth0
    virtual_router_id 51
    priority 100
    advert_int 1

    virtual_ipaddress {
        10.0.0.100/24
    }

    track_script {
        check_oes
    }

    notify_master "/opt/oes/scripts/on-become-master.sh"
    notify_backup "/opt/oes/scripts/on-become-backup.sh"
}
```

```bash
# on-become-master.sh: запустить OES daemon
#!/bin/bash
systemctl start oes-daemon
logger "Keepalived: стал MASTER, OES daemon запущен"

# on-become-backup.sh: остановить OES daemon (exclusive lock на .fdb)
#!/bin/bash
systemctl stop oes-daemon
logger "Keepalived: стал BACKUP, OES daemon остановлен"
```

### Patroni для PostgreSQL (если используется)

```yaml
# patroni.yml (Node 1)
scope: oes-pg-cluster
name: oes-pg-node1

restapi:
  listen: 0.0.0.0:8008

etcd:
  hosts: 10.0.0.10:2379,10.0.0.11:2379,10.0.0.12:2379

bootstrap:
  dcs:
    ttl: 30
    loop_wait: 10
    retry_timeout: 10
    maximum_lag_on_failover: 1048576
  postgresql:
    use_pg_rewind: true
    use_slots: true

postgresql:
  listen: 0.0.0.0:5432
  data_dir: /var/lib/postgresql/16/main
  authentication:
    replication:
      username: oes_replicator
      password: "${REPLICATION_PASSWORD}"
    superuser:
      username: postgres
      password: "${POSTGRES_PASSWORD}"

tags:
  nofailover: false
  noloadbalance: false
  clonefrom: false
```

```bash
# Статус кластера Patroni
patronictl -c /etc/patroni.yml list

# Ручной failover
patronictl -c /etc/patroni.yml failover oes-pg-cluster --master oes-pg-node1 --candidate oes-pg-node2

# Переключение (плановое, с ожиданием репликации)
patronictl -c /etc/patroni.yml switchover oes-pg-cluster
```

---

## Мониторинг HA

### Что мониторить

| Метрика | Порог | Алерт |
|---------|-------|-------|
| OES Daemon не отвечает | 3 проверки | Critical |
| Firebird sync lag | > 30 минут | Warning |
| PostgreSQL replica lag | > 1 МБ WAL | Warning |
| Keepalived state change | любое событие | Critical |
| Firebird БД размер растёт аномально | > 10% в день | Warning |
| Disk > 85% (под .fdb файлом) | порог | Critical |
| RAM > 90% | порог | Critical |
| OES Daemon restart | > 0 за 30 мин | Warning |

### Health check скрипт

```bash
#!/bin/bash
# scripts/ha-health-check.sh

ISSUES=""
HOSTNAME=$(hostname)

check_tcp() {
    local name="$1"
    local host="$2"
    local port="$3"
    if ! nc -z -w 5 "$host" "$port" 2>/dev/null; then
        ISSUES="${ISSUES}\nFAIL: $name ($host:$port)"
    else
        echo "OK: $name"
    fi
}

check_service() {
    local name="$1"
    local svc="$2"
    if ! systemctl is-active --quiet "$svc"; then
        ISSUES="${ISSUES}\nFAIL: Сервис $name остановлен"
    else
        echo "OK: $name"
    fi
}

check_disk_space() {
    local path="$1"
    local threshold="$2"
    local usage
    usage=$(df "$path" | tail -1 | awk '{print $5}' | tr -d '%')
    if [ "$usage" -gt "$threshold" ]; then
        ISSUES="${ISSUES}\nWARN: Диск $path заполнен на ${usage}%"
    else
        echo "OK: Диск $path (${usage}%)"
    fi
}

# === Проверки ===
check_service "OES Daemon" "oes-daemon"
check_service "Firebird" "firebird"
check_tcp "OES API" "localhost" "4001"
check_tcp "Firebird" "localhost" "3050"
check_disk_space "/var/lib/oes" "85"
check_disk_space "/" "90"

# Проверить freshness бэкапа
LATEST_BACKUP=$(find /var/backups/oes/firebird -name "*.fbk.gz" -type f -printf '%T@\n' 2>/dev/null | sort -rn | head -1)
if [ -n "$LATEST_BACKUP" ]; then
    AGE_HOURS=$(( ( $(date +%s) - ${LATEST_BACKUP%.*} ) / 3600 ))
    if [ "$AGE_HOURS" -gt 25 ]; then
        ISSUES="${ISSUES}\nWARN: Последний бэкап ${AGE_HOURS}ч назад"
    else
        echo "OK: Бэкап (${AGE_HOURS}ч назад)"
    fi
fi

# === Отправить алерт ===
if [ -n "$ISSUES" ]; then
    # Использовать $'...' синтаксис для корректного раскрытия \n в bash
    MSG=$'HA Alert ['"$HOSTNAME"$']:'"$ISSUES"
    echo -e "$MSG"
    curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
        --data-urlencode "chat_id=$TELEGRAM_CHAT_ID" \
        --data-urlencode "text=$MSG" > /dev/null
fi
```

```bash
# Cron: каждые 5 минут
*/5 * * * * /opt/oes/scripts/ha-health-check.sh >> /var/log/oes/ha-health.log 2>&1
```

---

## Чеклист по уровням

### Desktop (каждая инсталляция)
- [ ] Ежедневное автоматическое резервное копирование Firebird (gbak)
- [ ] Минимум 30 дней хранение локальных бэкапов
- [ ] Копия бэкапов на внешний диск или NAS
- [ ] Процедура восстановления задокументирована для пользователя
- [ ] Crashpad инициализирован (сбор crash dumps)

### Basic Server (OES в малом офисе)
- [ ] Systemd/Windows Service с Restart=on-failure
- [ ] Мониторинг сервиса (check-oes-service.sh в cron)
- [ ] Алерты в Telegram при остановке
- [ ] Ежедневные бэкапы Firebird на NAS/внешнее хранилище
- [ ] Health check endpoint в OES Daemon (если поддерживается)
- [ ] Тестирование восстановления из бэкапа ежемесячно

### Standard (многопользовательский OES сервер)
- [ ] Firebird warm standby (gbak sync каждые 15 мин)
- [ ] PostgreSQL replica (если используется)
- [ ] Скрипт автоматического failover
- [ ] HAProxy или Keepalived (в зависимости от архитектуры OES)
- [ ] Бэкапы offsite (NAS в другой локации)
- [ ] Ротация бэкапов: 7d + 4w + 3m
- [ ] Мониторинг lag репликации

### Enterprise (критичная инфраструктура OES)
- [ ] Patroni (PostgreSQL auto-failover) — если используется PostgreSQL
- [ ] Multi-node OES Daemon + Keepalived
- [ ] Ceph или NFS для shared storage
- [ ] Geo-redundancy (standby в другом ДЦ/офисе)
- [ ] RTO < 5 минут, RPO < 15 минут
- [ ] Disaster Recovery план задокументирован и протестирован
- [ ] SLA определён и мониторится
