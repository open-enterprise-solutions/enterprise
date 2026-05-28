# 15. High Availability and Failover

## OES fault-tolerance levels

| Level | What it includes | Downtime | For whom |
|---------|-------------|----------|----------|
| **Desktop** | Embedded Firebird, local backups | none (local) | Single user |
| **Basic Server** | OES Daemon + Firebird Server, auto-restart, backups | minutes | Small office, LAN deployment |
| **Standard** | + PostgreSQL replica, failover, load balancing | seconds | Medium business, multiple servers |
| **Enterprise** | + Multi-node, automatic failover, geo-redundancy | ~0 | Large enterprises, SLA |

---

## Desktop: reliability of local installation

```
Firebird Embedded:
  - DB = a single .fdb file next to the user's data
  - No network dependencies
  - Protection: daily gbak backups
  - On corruption: restore from backup (< 10 min)
```

### Automatic backup at startup

```cpp
// src/engine/enterprise/startup_backup.cpp
// Create a quick backup at startup if the previous one is > 24 hours old
// Called from ibApplicationData::AuthenticationAndSetUser() after successful login

void CreateStartupBackup(const wxString& dbPath) {
    wxString backupDir = GetAppDataPath() + wxFILE_SEP_PATH + "Backups";
    wxFileName::Mkdir(backupDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    
    wxDateTime now = wxDateTime::Now();
    wxString backupPath = backupDir + wxFILE_SEP_PATH +
        "oes_" + now.Format("%Y-%m-%d") + ".fbk";
    
    // Skip if a backup for today already exists
    if (wxFileExists(backupPath)) return;
    
    // Run gbak asynchronously (do not block startup)
    // Password is quoted in case of special characters
    wxString cmd = wxString::Format(
        "gbak -backup -user SYSDBA -password \"%s\" \"%s\" \"%s\"",
        wxGetenv("FB_PASSWORD"), dbPath, backupPath
    );
    
    wxExecute(cmd, wxEXEC_ASYNC);
    wxLogMessage("Backup started: %s", backupPath);
}
```

---

## Basic Server: automatic restart

### Windows Service: Recovery Actions

```powershell
# Configure auto-recovery for the OES Windows Service

sc.exe failure OESDaemon `
    reset= 86400 `
    actions= restart/5000/restart/10000/restart/30000
# restart after 5, 10, 30 seconds on failure
# Counter reset after 24 hours (86400 sec)

# Or via PowerShell:
$service = Get-WmiObject Win32_Service | Where-Object { $_.Name -eq "OESDaemon" }
# (use sc.exe - simpler and more reliable)
```

### Systemd: auto-restart configuration

```ini
# /etc/systemd/system/oes-daemon.service
[Unit]
Description=OES Enterprise Daemon
After=network.target firebird3.0-guardian.service
Wants=firebird3.0-guardian.service
StartLimitIntervalSec=300        # restart limit window
StartLimitBurst=5                # max 5 attempts in 5 minutes

[Service]
Type=simple
User=oes
Group=oes
ExecStart=/opt/oes/bin/oesd --config /etc/oes/daemon.conf
Restart=on-failure
RestartSec=10s                   # wait 10 sec before restart
TimeoutStopSec=30s
KillMode=mixed
KillSignal=SIGTERM

# Watchdog (OES must support sd_notify)
# WatchdogSec=60s

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable oes-daemon
sudo systemctl start oes-daemon

# Verify the auto-restart configuration
sudo systemctl show oes-daemon | grep -E "Restart|StartLimit"
```

---

## Standard: replication and failover

### Firebird: Master + Standby (OES Server mode)

```
For Firebird Classic/Super Server (not Embedded):

+-----------------+   gbak + rsync    +-----------------+
| Firebird Master | --------------->  | Firebird Standby|
| (R/W active)    |                   | (warm standby)  |
| 10.0.0.1:3050   |                   | 10.0.0.2:3050   |
+-----------------+                   +-----------------+
         ^                                     ^
   OES clients                         Failover target
```

**Firebird replication strategy via gbak:**

```bash
#!/bin/bash
# scripts/firebird-sync.sh
# Run every 15 minutes for a warm standby

PRIMARY_HOST="10.0.0.1"
PRIMARY_DB="/var/lib/oes/data/oes.fdb"
STANDBY_DB="/var/lib/oes/data/oes.fdb"
FB_USER="SYSDBA"
FB_PASS="${FB_SYSDBA_PASSWORD}"
TEMP_BACKUP="/tmp/oes_sync_$(date +%s).fbk"

# Create a backup from primary
gbak \
    -backup \
    -user "$FB_USER" \
    -password "$FB_PASS" \
    "${PRIMARY_HOST}:${PRIMARY_DB}" \
    "$TEMP_BACKUP"

# Copy to standby (if running locally on standby)
# Or use:
# gbak -backup "user:pass@primary:/path/db.fdb" /tmp/backup.fbk
# gbak -restore /tmp/backup.fbk "user:pass@standby:/path/db.fdb" -replace_database

# Restore on standby (replace the existing one)
gbak \
    -restore \
    -user "$FB_USER" \
    -password "$FB_PASS" \
    -replace_database \
    "$TEMP_BACKUP" \
    "$STANDBY_DB"

rm -f "$TEMP_BACKUP"
echo "[$(date)] Firebird standby sync completed"
```

```bash
# Cron: every 15 minutes
*/15 * * * * /opt/oes/scripts/firebird-sync.sh >> /var/log/oes/fb-sync.log 2>&1
```

### PostgreSQL: Master + Replica (if used)

```
+-------------+   WAL streaming   +-------------+
|   Master    | ----------------> |   Replica   |
|  (R/W)      |                   |  (R/O)      |
|  10.0.0.1   |                   |  10.0.0.2   |
+-------------+                   +-------------+
       ^
   OES Daemon
```

**PostgreSQL replication setup:**

```conf
# postgresql.conf (Master)
wal_level = replica
max_wal_senders = 5
wal_keep_size = 1024
synchronous_commit = on
```

```bash
# Create a replication user
sudo -u postgres psql -c "CREATE USER oes_replicator WITH REPLICATION PASSWORD 'strong-password';"

# Initial copy on Replica
sudo systemctl stop postgresql
sudo rm -rf /var/lib/postgresql/16/main/*
sudo -u postgres pg_basebackup \
    -h 10.0.0.1 \
    -U oes_replicator \
    -D /var/lib/postgresql/16/main \
    -Fp -Xs -P
sudo -u postgres touch /var/lib/postgresql/16/main/standby.signal
```

### Automatic failover script

```bash
#!/bin/bash
# scripts/oes-failover.sh
# Switch to standby when primary becomes unreachable

PRIMARY_HOST="10.0.0.1"
STANDBY_HOST="10.0.0.2"
OES_CONFIG="/etc/oes/daemon.conf"
CHECK_TIMEOUT=10
FAIL_COUNT=0
MAX_FAILS=3

check_primary() {
    # Check Firebird availability on primary
    nc -z -w "$CHECK_TIMEOUT" "$PRIMARY_HOST" 3050 2>/dev/null
    return $?
}

promote_standby() {
    echo "[$(date)] Switching to standby: $STANDBY_HOST"
    
    # Update OES daemon configuration
    sed -i "s/Host=.*/Host=$STANDBY_HOST/" "$OES_CONFIG"
    
    # Restart OES daemon
    systemctl restart oes-daemon
    
    # Notify the team
    curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
        -d chat_id="$TELEGRAM_CHAT_ID" \
        -d "text=FAILOVER: OES switched to standby ($STANDBY_HOST). Primary ($PRIMARY_HOST) unreachable."
    
    echo "[$(date)] Failover complete. OES now uses: $STANDBY_HOST"
}

# Check primary
for i in $(seq 1 $MAX_FAILS); do
    if check_primary; then
        echo "[$(date)] Primary OK: $PRIMARY_HOST"
        exit 0
    fi
    FAIL_COUNT=$((FAIL_COUNT + 1))
    echo "[$(date)] Attempt $FAIL_COUNT/$MAX_FAILS: $PRIMARY_HOST unreachable"
    sleep 5
done

echo "[$(date)] Primary unreachable $MAX_FAILS times in a row. Initiating failover."
promote_standby
```

---

## Enterprise: Multi-node cluster

### OES Multi-node architecture

```
                    +--------------------------+
                    | Load Balancer / HAProxy  |
                    | VIP: 10.0.0.100:4001     |
                    +-----------+--------------+
                                |
              +-----------------+-----------------+
              |                 |                 |
        +-----v-----+    +------v------+    +----v-------+
        | OES Node1 |    | OES Node 2  |    | OES Node3  |
        | Daemon    |    | Daemon      |    | Daemon     |
        | 10.0.0.1  |    | 10.0.0.2    |    | 10.0.0.3   |
        +-----+-----+    +------+------+    +----+-------+
              |                 |                 |
        +-----v-----------------v-----------------v-----+
        |            Shared Storage (NFS / Ceph)        |
        |         Firebird DB: /mnt/shared/oes.fdb      |
        |      OR  PostgreSQL cluster (Patroni)         |
        +-----------------------------------------------+
```

### HAProxy for OES Daemon load balancing

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

# Status
echo "show stat" | sudo socat /var/run/haproxy/admin.sock stdio | head -5
```

### Keepalived: Floating IP for Active/Passive

```
When OES Daemon does not support horizontal scaling
(single process with an exclusive lock on .fdb):

+-----------------+  VIP: 10.0.0.100  +-----------------+
| OES Primary     |<---------------->| OES Secondary    |
| MASTER          |   keepalived      | BACKUP           |
| 10.0.0.1        |                   | 10.0.0.2         |
| (active)        |                   | (standby)        |
+-----------------+                   +-----------------+

Primary fails -> VIP moves to Secondary -> clients reconnect
```

```conf
# /etc/keepalived/keepalived.conf (Primary - MASTER)
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
# on-become-master.sh: start OES daemon
#!/bin/bash
systemctl start oes-daemon
logger "Keepalived: became MASTER, OES daemon started"

# on-become-backup.sh: stop OES daemon (exclusive lock on .fdb)
#!/bin/bash
systemctl stop oes-daemon
logger "Keepalived: became BACKUP, OES daemon stopped"
```

### Patroni for PostgreSQL (if used)

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
# Patroni cluster status
patronictl -c /etc/patroni.yml list

# Manual failover
patronictl -c /etc/patroni.yml failover oes-pg-cluster --master oes-pg-node1 --candidate oes-pg-node2

# Switchover (planned, waits for replication)
patronictl -c /etc/patroni.yml switchover oes-pg-cluster
```

---

## HA monitoring

### What to monitor

| Metric | Threshold | Alert |
|---------|-------|-------|
| OES Daemon not responding | 3 checks | Critical |
| Firebird sync lag | > 30 minutes | Warning |
| PostgreSQL replica lag | > 1 MB WAL | Warning |
| Keepalived state change | any event | Critical |
| Firebird DB size growing abnormally | > 10% per day | Warning |
| Disk > 85% (under .fdb file) | threshold | Critical |
| RAM > 90% | threshold | Critical |
| OES Daemon restart | > 0 in 30 min | Warning |

### Health check script

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
        ISSUES="${ISSUES}\nFAIL: Service $name is stopped"
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
        ISSUES="${ISSUES}\nWARN: Disk $path is ${usage}% full"
    else
        echo "OK: Disk $path (${usage}%)"
    fi
}

# === Checks ===
check_service "OES Daemon" "oes-daemon"
check_service "Firebird" "firebird"
check_tcp "OES API" "localhost" "4001"
check_tcp "Firebird" "localhost" "3050"
check_disk_space "/var/lib/oes" "85"
check_disk_space "/" "90"

# Backup freshness check
LATEST_BACKUP=$(find /var/backups/oes/firebird -name "*.fbk.gz" -type f -printf '%T@\n' 2>/dev/null | sort -rn | head -1)
if [ -n "$LATEST_BACKUP" ]; then
    AGE_HOURS=$(( ( $(date +%s) - ${LATEST_BACKUP%.*} ) / 3600 ))
    if [ "$AGE_HOURS" -gt 25 ]; then
        ISSUES="${ISSUES}\nWARN: Latest backup ${AGE_HOURS}h ago"
    else
        echo "OK: Backup (${AGE_HOURS}h ago)"
    fi
fi

# === Send alert ===
if [ -n "$ISSUES" ]; then
    # Use $'...' syntax for correct \n expansion in bash
    MSG=$'HA Alert ['"$HOSTNAME"$']:'"$ISSUES"
    echo -e "$MSG"
    curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
        --data-urlencode "chat_id=$TELEGRAM_CHAT_ID" \
        --data-urlencode "text=$MSG" > /dev/null
fi
```

```bash
# Cron: every 5 minutes
*/5 * * * * /opt/oes/scripts/ha-health-check.sh >> /var/log/oes/ha-health.log 2>&1
```

---

## Checklists by level

### Desktop (each installation)
- [ ] Daily automatic Firebird backup (gbak)
- [ ] At least 30 days of local backup retention
- [ ] Copy of backups on external drive or NAS
- [ ] Recovery procedure documented for the user
- [ ] Crashpad initialized (crash dump collection)

### Basic Server (OES in a small office)
- [ ] Systemd/Windows Service with Restart=on-failure
- [ ] Service monitoring (check-oes-service.sh in cron)
- [ ] Telegram alerts on stop
- [ ] Daily Firebird backups to NAS / external storage
- [ ] Health check endpoint in OES Daemon (if supported)
- [ ] Backup restore test monthly

### Standard (multi-user OES server)
- [ ] Firebird warm standby (gbak sync every 15 min)
- [ ] PostgreSQL replica (if used)
- [ ] Automatic failover script
- [ ] HAProxy or Keepalived (depending on OES architecture)
- [ ] Offsite backups (NAS at another location)
- [ ] Backup rotation: 7d + 4w + 3m
- [ ] Replication lag monitoring

### Enterprise (critical OES infrastructure)
- [ ] Patroni (PostgreSQL auto-failover) - if PostgreSQL is used
- [ ] Multi-node OES Daemon + Keepalived
- [ ] Ceph or NFS for shared storage
- [ ] Geo-redundancy (standby in another DC/office)
- [ ] RTO < 5 minutes, RPO < 15 minutes
- [ ] Disaster Recovery plan documented and tested
- [ ] SLA defined and monitored
