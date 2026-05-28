# 23. Access Management

## Principle of least privilege

> Every team member receives the **minimum required** access level to do their job. Nothing more.

---

## Access matrix

### GitHub

| Role | Repos (own Team) | Repos (other Team) | Org settings | Billing |
|------|-------------------|-------------------|-------------|---------|
| Developer | Write | — | — | — |
| Senior/Lead | Write + branch protection | Read (on request) | — | — |
| Build/Release | Write + Tags | Read | — | — |
| CTO/Owner | Admin | Admin | Admin | Admin |

### Build / CI servers (SSH)

| Role | Build Server | Release Storage | Signing Key |
|------|-------------|-----------------|-------------|
| Junior Dev | — | — | — |
| Developer | Read (build logs) | — | — |
| Senior/Lead | Full (trigger builds) | Read | — |
| Build Engineer | Full | Full | Restricted |
| CTO | Full | Full | Full |

### Databases (Firebird / PostgreSQL / SQLite)

| Role | SELECT | INSERT/UPDATE | DELETE | ALTER/DROP | Backup (gbak) |
|------|--------|-------------|--------|-----------|---------------|
| App (ORM) | yes | yes | yes | — | — |
| Developer (staging) | yes | yes | — | — | — |
| Senior/Lead | yes | yes | yes (staging) | — | — |
| DBA / DevOps | yes | yes | yes | yes | yes |
| Backup cron | yes | — | — | — | yes |
| Read-only reporter | yes | — | — | — | — |

### Access to customer production data

> **No developer has direct access to a customer production DB without an explicit request and approval.**

| Event | Who approves | Access method | Record |
|---------|-------------|----------------|---------|
| Incident diagnostics | CTO + customer | Temporary read-only | Jira ticket |
| Data migration | CTO | Supervised script | PR + log |
| Restore from backup | CTO + customer | Through DevOps | Protocol |

### External services

| Service | Developer | Lead | Build Engineer | CTO |
|--------|-----------|------|----------------|-----|
| GitHub Actions Secrets | — | — | Full | Full |
| Code Signing Certificate | — | — | Restricted | Full |
| Release CDN | — | Read | Full | Full |
| Crash Report Server | Read | Full | — | Full |
| Update Server | — | Read | Full | Full |
| Jira | Write (own project) | Admin (own project) | — | Admin |
| Telegram Bot (alerts) | — | Read | Full | Full |

### License keys and distribution

| Action | Role |
|----------|------|
| Issue a trial key to a customer | Lead or CTO |
| Issue a commercial license | CTO |
| Publish a release on the site | Build Engineer + CTO |
| Sign the installer | Build Engineer (certificate in HSM/safe) |

---

## Access provisioning

### Request

```
1. Developer → request in Telegram/Jira:
   "Need access to [what] for [why] until [date if temporary]"

2. Tech lead → approves or rejects

3. DevOps/Admin → grants access

4. Recorded:
   - Who requested
   - Who approved
   - What was granted (specifically)
   - When + expiration (if temporary)
```

### GitHub — adding to a Team

```bash
# Via GitHub UI:
# Organization → People → Invite member → Add to Team

# Or via CLI:
gh api orgs/OES-TEAM/teams/TEAM/memberships/USERNAME -f role=member
```

### Access to the build server

```bash
# 1. Developer generates an ed25519 key
ssh-keygen -t ed25519 -C "name@company.com" -f ~/.ssh/oes_build_key

# 2. Sends the PUBLIC key through a secure channel (not Telegram!)
cat ~/.ssh/oes_build_key.pub

# 3. DevOps adds it — creates a dedicated user, not root
useradd -m -s /bin/bash developer_name
mkdir -p /home/developer_name/.ssh
echo "ssh-ed25519 AAAA... name@company.com" \
    >> /home/developer_name/.ssh/authorized_keys
chmod 700  /home/developer_name/.ssh
chmod 600  /home/developer_name/.ssh/authorized_keys

# 4. Restrict access (CMake logs only)
# /etc/sudoers.d/developer_name:
# developer_name ALL=(ALL) NOPASSWD: /usr/bin/cmake --build *
# developer_name ALL=(ALL) NOPASSWD: /usr/bin/tail -f /var/log/build.log
```

### Firebird database — users

```sql
-- Firebird: users are managed through gsec or SQL (Firebird 3+)

-- IMPORTANT: Firebird does NOT support "GRANT ... ON ALL TABLES TO ..."
-- (unlike PostgreSQL). Grants must be issued per table.
-- Use roles and a script to automate:

-- 1. Create roles
CREATE ROLE ROLE_READONLY;
CREATE ROLE ROLE_APP;

-- 2. Grant role privileges on each table (repeat for every table)
--    Or generate the script from system tables:
-- SELECT 'GRANT SELECT ON ' || RDB$RELATION_NAME || ' TO ROLE_READONLY;'
--   FROM RDB$RELATIONS
--  WHERE RDB$SYSTEM_FLAG = 0 AND RDB$VIEW_BLR IS NULL;
GRANT SELECT ON DOCUMENTS         TO ROLE_READONLY;
GRANT SELECT ON DOCUMENT_SECTIONS TO ROLE_READONLY;
GRANT SELECT ON USERS              TO ROLE_READONLY;
-- ... other tables ...

GRANT SELECT, INSERT, UPDATE, DELETE ON DOCUMENTS         TO ROLE_APP;
GRANT SELECT, INSERT, UPDATE, DELETE ON DOCUMENT_SECTIONS TO ROLE_APP;
-- ... other tables ...

-- 3. Create users and assign roles
CREATE USER DEV_READONLY PASSWORD 'strong-password';
GRANT ROLE_READONLY TO DEV_READONLY;

CREATE USER OES_APP PASSWORD 'strong-password';
GRANT ROLE_APP TO OES_APP;

-- Backup user
CREATE USER BACKUP_CRON PASSWORD 'strong-password';
-- gbak only needs CONNECT
```

```sql
-- PostgreSQL (for additional DBs)
-- Read-only for a developer
CREATE USER dev_readonly WITH PASSWORD 'strong-password';
GRANT CONNECT ON DATABASE oes_db TO dev_readonly;
GRANT USAGE ON SCHEMA public TO dev_readonly;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO dev_readonly;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
    GRANT SELECT ON TABLES TO dev_readonly;

-- Full access for the application
CREATE USER oes_app WITH PASSWORD 'strong-password';
GRANT ALL PRIVILEGES ON DATABASE oes_db TO oes_app;
```

---

## Access revocation

### When to revoke

| Event | Action | Deadline |
|---------|---------|------|
| Termination | Revoke EVERYTHING | Immediately (same day) |
| Move to another project | Revoke old project access | Within 1 day |
| Contractor contract ends | Revoke EVERYTHING | Last day |
| Key/password compromise | Revoke + rotate | Immediately (within an hour) |
| Inactivity > 90 days | Revoke, can be restored | When discovered |

### Offboarding checklist

```
[ ] GitHub
  [ ] Remove from the organization
  [ ] Remove from every Team
  [ ] Check personal access tokens (revoke)
  [ ] Check deploy keys (if personal)

[ ] Build / CI servers
  [ ] Remove SSH key from authorized_keys on EVERY server
  [ ] Remove the user (userdel -r)
  [ ] Check cron jobs owned by this user
  [ ] Check screen/tmux sessions

[ ] Databases
  [ ] Firebird: DROP USER or REVOKE via gsec/isql
  [ ] PostgreSQL: REVOKE + DROP USER
  [ ] Rotate shared passwords if any were used

[ ] External services
  [ ] Jira — deactivate the account
  [ ] Confluence — deactivate
  [ ] Crash Report Server — remove access
  [ ] Update Server — remove access

[ ] Signing certificate
  [ ] Confirm the physical USB token was returned
  [ ] Check whether exported copies exist

[ ] Communication
  [ ] Remove from project Telegram groups
  [ ] Remove from Slack/Discord
  [ ] Disable corporate email
```

### Automated revocation script

```bash
#!/bin/bash
# offboard.sh — access revocation script for offboarding
# Usage: ./offboard.sh github_username

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

# Build servers
for SERVER in build.oes-internal.com staging.oes-internal.com; do
  echo "Removing SSH key from $SERVER..."
  ssh admin@$SERVER "
    sudo sed -i '/$USERNAME/d' /home/*/.ssh/authorized_keys 2>/dev/null
    sudo userdel -r $USERNAME 2>/dev/null
  " && echo "OK $SERVER" || echo "WARN: $SERVER (manual check needed)"
done

# Firebird (through isql)
echo "Revoking Firebird access..."
ssh admin@db.oes-internal.com "
  isql -user SYSDBA -pass masterkey <<EOF
  DROP USER ${USERNAME^^};
  COMMIT;
  EXIT;
EOF
" && echo "OK Firebird" || echo "WARN: Firebird (manual check needed)"

echo ""
echo "=== Manual steps ==="
echo "[ ] Jira / Confluence — deactivate manually"
echo "[ ] Telegram groups — remove manually"
echo "[ ] Verify return of the USB token with the certificate (if issued)"
echo "[ ] Rotate shared passwords this user had access to"
```

---

## Secret rotation

### Rotation schedule

| Secret | Frequency | How |
|--------|-------------|-----|
| Build server SSH keys | Once a year | Regenerate, refresh on every host |
| Firebird SYSDBA password | Every 6 months | `gsec -modify SYSDBA -pw new_password` |
| PostgreSQL passwords | Every 6 months | `ALTER USER ... PASSWORD '...'` |
| Code signing certificate | At expiration (usually 1-3 years) | New CSR → CA |
| Update server API key | Once a year | Regenerate in the server console |
| Crash report server token | Once a year | Rotate in the config |
| License master key | On compromise | New key + customer notification |

### On compromise — immediately

```
1. Identify what was compromised (key, certificate, password)
2. Immediately revoke (block/revoke)
3. Inspect logs — was it used by an attacker
4. Notify the team + leadership
5. For a code signing cert: notify the CA to revoke
6. Postmortem: how it leaked, how to prevent a recurrence
```

---

## Access audit

### Monthly

```
[ ] Review GitHub org members — are they all current?
[ ] Review SSH keys on build servers — any stragglers?
[ ] Review Firebird/PostgreSQL users — any unused?
[ ] Review GitHub personal access tokens — any stale?
[ ] Review who has access to the code signing certificate
[ ] Review install logs — anyone installing unsigned builds
```

### Tools

```bash
# Who has SSH access to the build server?
cat /home/*/.ssh/authorized_keys

# Firebird users
isql -user SYSDBA -pass masterkey -e \
  "SELECT SEC\$USER_NAME FROM SEC\$USERS;"

# PostgreSQL users
sudo -u postgres psql -c \
  "SELECT usename, usesuper, usecreatedb FROM pg_user;"

# Who's in the GitHub org?
gh api orgs/OES-TEAM/members --jq '.[].login'

# Active sessions on the build server
who
last -20
```
