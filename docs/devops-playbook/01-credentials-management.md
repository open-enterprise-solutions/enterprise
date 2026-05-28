# 01. Credentials Management — OES

> The most important document. A leaked secret means compromise of client installations and licensing infrastructure.

---

## Secret types in OES

### Database connection keys
- **Firebird embedded** — database master password (`SYSDBA`), application database password
- **Firebird server** — host, port, user, password (for server mode)
- **PostgreSQL** — host, port, user, application password
- **SQLite** — file path (not a password, but the file must be protected by permissions)
- **MySQL / ODBC** — connection strings, user passwords

### License keys
- **OES serial number** — end-user license key
- **License signing private key** — RSA/ECDSA key for generating licenses on the vendor side
- **License verification public key** — embedded in the application (not a secret, but important for integrity)
- **License Server API token** — for license validation in online mode

### Build and distribution keys
- **Code Signing Certificate** — Authenticode certificate for signing EXE/MSI/NSIS installer
- **Code Signing private key** — store in HSM or encrypted storage
- **PFX/P12 password** — for export/import of the Code Signing certificate
- **GitHub Deploy Keys** — repository access from the build server (read-only)
- **GitHub Actions Secrets** — CI/CD secrets for building and publishing

### Server infrastructure keys (daemon mode)
- **SSH keys** — access to build server and OES daemon deployment servers
- **Cloudflare API Token** — DNS management for update/license servers
- **Telegram Bot Token** — notifications about builds and errors (optional)

### Application configuration secrets
- **Configuration master encryption key** — for encrypting the `oes.conf` file or the registry
- **SMTP passwords** — if OES sends email notifications from daemon mode
- **S3/MinIO keys** — for storing database backups

---

## Storage tools

| Tool | Purpose | Free |
|-----------|---------|-----------|
| 1Password Teams | Team storage of passwords, keys, certificates | No ($4/month/user) |
| Bitwarden | 1Password alternative, self-hosted | Yes (self-host) |
| HashiCorp Vault | Programmatic storage, API access (build server) | Yes (open-source) |
| GitHub Secrets | CI/CD secrets (Code Signing, API tokens) | Yes |
| SOPS + age | Encryption of configuration files in git | Yes |
| Windows Credential Manager | Local storage on user's machine | Yes (built-in) |
| DPAPI (Windows) | OS-level secret encryption by the application | Yes (built-in) |
| pass (GPG) | DevOps personal passwords | Yes |

### When to use what

```
Team passwords, DB passwords, license keys          -> 1Password / Bitwarden
CI/CD secrets (Code Signing, API tokens)            -> GitHub Secrets
Build server configuration in the repository        -> SOPS + age
Runtime secrets on the server (daemon mode)         -> HashiCorp Vault / environment variables
Password storage on user machines                   -> Windows Credential Manager / DPAPI
DevOps personal passwords                           -> pass (GPG)
```

---

## Processes

### Granting access to a new employee

```
1. Create an account in 1Password / Bitwarden
2. Add to required vaults/collections (dev only — NOT production, NOT license keys)
3. Generate a personal SSH key:
   ssh-keygen -t ed25519 -C "name@oes-team"
4. Add the public key to the build server:
   ssh-copy-id -i ~/.ssh/id_ed25519.pub build-user@build-server
5. Add to the GitHub organization with required permissions
6. Grant Cloudflare access (if needed) with minimal permissions
7. DO NOT grant access to the license signing private key (CI/CD only)
```

**NEVER:**
- Send customer license keys via messengers
- Store the Code Signing certificate on developer machines
- Commit DB connection strings to source code
- Use a shared SSH key for the whole team

### How to share an SSH key

```bash
# Public key — can be shared openly (Slack, email, GitHub)
cat ~/.ssh/id_ed25519.pub
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAA... user@machine

# Private key — NEVER SHARE
# Everyone generates their own. Period.
```

If a deploy key is needed for the build server:
```bash
# On the build server
ssh-keygen -t ed25519 -C "deploy@oes-build" -f ~/.ssh/deploy_key -N ""
# Add the public key to GitHub -> Settings -> Deploy Keys (read-only)
cat ~/.ssh/deploy_key.pub
```

### How to rotate a database password

#### Firebird (embedded / server)

```bash
# 1. Generate a new password
NEW_PASS=$(openssl rand -base64 24 | tr -dc 'a-zA-Z0-9' | head -c 24)
echo "New password: $NEW_PASS"
# Save in 1Password/Bitwarden

# 2. Change the Firebird user password (server mode)
# Via isql-fb or the gsec utility:
gsec -user SYSDBA -pass masterkey -mo APP_USER -pw "$NEW_PASS"

# 3. For embedded Firebird — password is stored in OES encrypted config
# Update in the configuration file (see template below)

# 4. Update the secret in 1Password and GitHub Secrets
# 5. Restart OES daemon (if running as a service)
sc stop OESDaemon && sc start OESDaemon
# or on Linux:
sudo systemctl restart oes-daemon
```

#### PostgreSQL (if used as a backend)

```bash
# 1. Generate a new password
NEW_PASS=$(openssl rand -base64 32)

# 2. Change the password in PostgreSQL
sudo -u postgres psql -c "ALTER USER oes_user WITH PASSWORD '$NEW_PASS';"

# 3. Update the connection string in OES configuration
# C:\ProgramData\OES\oes.conf (Windows)
# /etc/oes/oes.conf (Linux daemon)

# 4. Restart OES daemon
sc stop OESDaemon && sc start OESDaemon

# 5. Update the encrypted config in the repository (if using SOPS)
sops --encrypt oes.conf.production > oes.conf.production.enc
git add oes.conf.production.enc && git commit -m "rotate db password"
```

### Checklist when an employee leaves

```
[ ] Remove the SSH key from the build server and all daemon servers
    grep -r "user@machine" /home/*/.ssh/authorized_keys
    sed -i '/user@machine/d' /home/build-user/.ssh/authorized_keys

[ ] Remove from the GitHub organization
[ ] Remove from 1Password / Bitwarden
[ ] Remove from Cloudflare
[ ] Rotate ALL secrets the user had access to
    - DB passwords (Firebird, PostgreSQL, MySQL)
    - API tokens (License Server, Cloudflare)
    - Code Signing container password (if access existed)
[ ] If the user had access to the license private key — reissue it immediately
[ ] Remove from Telegram groups (monitoring, alerts)
[ ] Remove from VPN (if any)
[ ] Review logs from the last day
[ ] Document the date and what was revoked
```

### Where to store OES configuration on work machines

```
Windows (recommended):
  C:\ProgramData\OES\oes.conf      - service/daemon config (SYSTEM permissions)
  C:\ProgramData\OES\license.key   - license key (Administrators permissions)
  HKLM\Software\OES\               - registry (for non-critical settings)

macOS (Desktop):
  ~/Library/Application Support/OES/oes.conf   - desktop application config
  ~/Library/Application Support/OES/license.key
  /etc/oes/oes.conf                             - daemon config (chmod 600)
  /var/lib/oes/                                 - daemon data
  /var/log/oes/                                 - daemon logs

Linux daemon:
  /etc/oes/oes.conf                - daemon config (chmod 600, owner oes)
  /var/lib/oes/                    - application data
  /var/log/oes/                    - logs

Access permissions:
  - oes.conf must be accessible ONLY to the OES process
  - NOT in public directories
  - NOT in git
```

```cmd
REM Windows — grant permissions only to SYSTEM and Administrators
icacls "C:\ProgramData\OES\oes.conf" /inheritance:r
icacls "C:\ProgramData\OES\oes.conf" /grant SYSTEM:(F)
icacls "C:\ProgramData\OES\oes.conf" /grant Administrators:(F)
```

```bash
# Linux daemon
sudo chown oes:oes /etc/oes/oes.conf
sudo chmod 600 /etc/oes/oes.conf
```

```bash
# macOS daemon
sudo chown oes:oes /etc/oes/oes.conf
sudo chmod 600 /etc/oes/oes.conf

# macOS desktop (user config)
chmod 600 ~/Library/Application\ Support/OES/oes.conf
```

### oes.conf.example template (configuration without values)

```ini
; === OES Configuration Template ===
; Copy to oes.conf and fill in values
; NEVER commit oes.conf with real data

[Application]
Mode=                       ; desktop | daemon | service
LicenseKey=                 ; License key (for built-in verification)
LicenseServerURL=           ; https://license.oes-vendor.com/api/v1/validate

[Database.Primary]
Engine=                     ; firebird | postgresql | sqlite | mysql
Host=                       ; localhost (or IP for server)
Port=                       ; 3050 (Firebird) | 5432 (PostgreSQL) | 3306 (MySQL)
Database=                   ; Path to .fdb file or DB name
User=                       ; DB user
Password=                   ; GENERATED_PASSWORD - keep in 1Password

[Database.Firebird]
EmbeddedMode=               ; true | false
MasterPassword=             ; Firebird SYSDBA master password

[Daemon]
ListenHost=                 ; 127.0.0.1 (local only) | 0.0.0.0
ListenPort=                 ; 8765 (daemon-mode HTTP API port)
TLSCert=                    ; Path to TLS certificate
TLSKey=                     ; Path to TLS private key

[Updates]
UpdateServerURL=            ; https://updates.oes-vendor.com/
UpdateCheckToken=           ; Token for authenticated updates

[Notifications]
SMTPHost=                   ; smtp.example.com
SMTPPort=                   ; 587
SMTPUser=                   ;
SMTPPassword=               ;
TelegramBotToken=           ; For notifications in daemon mode
TelegramChatID=             ;

[Backup]
S3Endpoint=                 ; https://s3.amazonaws.com (optional)
S3Bucket=                   ;
S3AccessKey=                ;
S3SecretKey=                ;
```

### Emergency: secret leak

**If a secret is exposed publicly (git, screenshot, log):**

```
STEP 1: IMMEDIATELY rotate the compromised secret
        - Change DB password / revoke API token / regenerate key
        - If the Code Signing certificate is compromised - IMMEDIATELY
          contact the CA for revocation and reissue
        - If the license private key is compromised - regenerate the key
          pair and issue an update for all clients

STEP 2: Update the secret everywhere it is used
        - oes.conf on all servers
        - GitHub Secrets
        - 1Password / Bitwarden
        - CI/CD pipelines

STEP 3: Check logs for unauthorized access
        - Firebird / PostgreSQL logs - unusual connections
        - License Server logs - mass activations
        - OES daemon logs: /var/log/oes/daemon.log

STEP 4: Remove the secret from git history (if it was committed)
        # Use BFG Repo-Cleaner:
        bfg --delete-files oes.conf
        git reflog expire --expire=now --all && git gc --prune=now --aggressive
        git push --force --all
        # Notify all team members about the force push

STEP 5: Document the incident
        - What leaked (DB password, license key, Code Signing?)
        - When discovered
        - Potentially affected clients
        - Actions taken
        - Measures to prevent recurrence
```

---

## SOPS + age (configuration encryption in git)

### Installation

```bash
# macOS
brew install sops age

# Ubuntu (build server)
sudo apt install age
SOPS_VERSION=3.8.1
curl -LO https://github.com/getsops/sops/releases/download/v${SOPS_VERSION}/sops-v${SOPS_VERSION}.linux.amd64
sudo mv sops-v${SOPS_VERSION}.linux.amd64 /usr/local/bin/sops
sudo chmod +x /usr/local/bin/sops

# Windows (build machine)
choco install sops
# or download the binary from GitHub Releases
```

### Key generation

```bash
# Create an age key
age-keygen -o key.txt
# Public key: age1ql3z7hjy54pw3hyww5ayyfg7zqgvc7w3j2elw8zmrj2kg5sfn9aqmcac8p

# Save the key in 1Password!
cat key.txt

# On the build server - place the key:
mkdir -p /root/.config/sops/age/
nano /root/.config/sops/age/keys.txt
chmod 600 /root/.config/sops/age/keys.txt
```

### .sops.yaml configuration

```yaml
# .sops.yaml at the repository root
creation_rules:
  - path_regex: oes\.conf\.production\.enc$
    age: >-
      age1ql3z7hjy54pw3hyww5ayyfg7zqgvc7w3j2elw8zmrj2kg5sfn9aqmcac8p
  - path_regex: oes\.conf\.staging\.enc$
    age: >-
      age1ql3z7hjy54pw3hyww5ayyfg7zqgvc7w3j2elw8zmrj2kg5sfn9aqmcac8p
```

### Encryption / decryption

```bash
# Encrypt a configuration file
sops --encrypt oes.conf.production > oes.conf.production.enc

# Decrypt
export SOPS_AGE_KEY_FILE=/root/.config/sops/age/keys.txt
sops --decrypt oes.conf.production.enc > oes.conf.production

# Edit an encrypted file (opens in $EDITOR)
sops oes.conf.production.enc

# Commit the encrypted file (safe to keep in git)
git add oes.conf.production.enc .sops.yaml
git commit -m "update encrypted production config"
```

### Usage in CI/CD (GitHub Actions)

```yaml
# In GitHub Secrets: SOPS_AGE_KEY (contents of key.txt)
- name: Decrypt OES config
  run: |
    mkdir -p /root/.config/sops/age/
    echo "${{ secrets.SOPS_AGE_KEY }}" > /root/.config/sops/age/keys.txt
    sops --decrypt oes.conf.production.enc > oes.conf.production
```

---

## Code Signing (installer signing)

### Securely storing the certificate in CI/CD

```bash
# Never store the PFX file in the repository!
# GitHub Secrets: CODE_SIGN_PFX (base64 of the .pfx file)
# GitHub Secrets: CODE_SIGN_PASSWORD (.pfx password)
```

```yaml
# .github/workflows/release.yml
- name: Sign installer
  env:
    CODE_SIGN_PFX: ${{ secrets.CODE_SIGN_PFX }}
    CODE_SIGN_PASSWORD: ${{ secrets.CODE_SIGN_PASSWORD }}
  run: |
    # Decode PFX
    echo "$CODE_SIGN_PFX" | base64 -d > signing.pfx

    # Sign the installer (signtool.exe)
    "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe" sign `
      /f signing.pfx `
      /p "$CODE_SIGN_PASSWORD" `
      /tr http://timestamp.digicert.com `
      /td sha256 /fd sha256 `
      OES-Setup.exe

    # Remove PFX after signing
    Remove-Item signing.pfx
```

---

## Platform-specific secret storage

### macOS: Keychain

```bash
# Save Firebird password to macOS Keychain
security add-generic-password \
  -a "oes-app" \
  -s "OES-Firebird-SYSDBA" \
  -w "GENERATED_PASSWORD"

# Retrieve password from Keychain
security find-generic-password -s "OES-Firebird-SYSDBA" -w
```

```cpp
// In OES code (macOS): reading from Keychain via Security framework
#include <Security/Security.h>

wxString LoadFirebirdPasswordFromKeychain() {
    SecKeychainItemRef item = nullptr;
    UInt32 passwordLen = 0;
    void* passwordData = nullptr;
    
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr, 15, "OES-Firebird-DB",
        7, "oes-app",
        &passwordLen, &passwordData, &item
    );
    
    if (status == errSecSuccess) {
        wxString pwd = wxString::FromUTF8(static_cast<char*>(passwordData), passwordLen);
        SecKeychainItemFreeContent(nullptr, passwordData);
        return pwd;
    }
    return wxEmptyString;
}
```

### Linux: libsecret / gnome-keyring

```bash
# Via secret-tool (libsecret)
secret-tool store --label='OES Firebird Password' service oes-firebird username sysdba

# Retrieve
secret-tool lookup service oes-firebird username sysdba
```

## Windows DPAPI (OS-level encryption)

For storing DB passwords in desktop OES mode, DPAPI is used — encryption tied to the Windows user account:

```cpp
// Example usage in OES C++ code (Windows)
// Encrypt the connection string via CryptProtectData
#include <windows.h>
#include <wincrypt.h>

// Encrypt a secret (only the current user can decrypt)
bool EncryptSecret(const std::wstring& plaintext, std::vector<BYTE>& encrypted) {
    DATA_BLOB input, output;
    input.pbData = (BYTE*)plaintext.data();
    input.cbData = plaintext.size() * sizeof(wchar_t);
    if (!CryptProtectData(&input, L"OES Config", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    encrypted.assign(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return true;
}
```

---

## Generating secure passwords

```bash
# Random 32-character password
openssl rand -base64 32

# Letters and digits only (Firebird-compatible)
openssl rand -base64 48 | tr -dc 'a-zA-Z0-9' | head -c 24

# UUID (for License Key generation)
uuidgen

# PowerShell (Windows)
[System.Web.Security.Membership]::GeneratePassword(24, 4)
# or
-join ((65..90) + (97..122) + (48..57) | Get-Random -Count 24 | ForEach-Object {[char]$_})
```

---

## Security rules (enforce on the team)

1. **Never** commit oes.conf / connection strings to git
2. **Never** send customer license keys via messengers
3. **Never** log passwords or connection strings (neither in debug nor in release)
4. **Never** store the Code Signing certificate on developer machines
5. **Always** keep oes.conf.example without values in the repository
6. **Always** rotate secrets when an employee leaves
7. **Always** use different passwords for dev/staging/production DBs
8. **Always** encrypt database backups
9. **Always** grant minimal DB user permissions (only required tables/schemas)
10. **Regularly** (once per quarter) rotate critical secrets
11. **Check** git diff before committing — make sure no connection string slipped in
12. **Embed** only the license verification public key, NEVER the private key
