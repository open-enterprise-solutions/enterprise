# 02. Server Setup from Scratch — OES

> Ubuntu 22.04/24.04 LTS or macOS 13+ -> production-ready server/workstation for OES builds and/or OES daemon deployment.

In OES the server is used in two roles:
- **Build Server** — C++/wxWidgets compilation, installer build, CI/CD
- **OES Daemon Server** — running OES in service mode (headless, HTTP API for thin clients)

---

## 1. First login

```bash
# Connect via SSH (provider gives the root password)
ssh root@203.0.113.10

# Change the root password
passwd
# Enter a new strong password (save in 1Password)
```

## 2. Create a user

```bash
# For the build server
adduser build-user
usermod -aG sudo build-user

# For the OES daemon server — a separate unprivileged user
adduser oes
# Do NOT grant sudo (principle of least privilege)

# Verify
su - build-user
sudo whoami
# root
```

## 3. SSH: key + disable password

```bash
# On the LOCAL machine — copy the key to the server
ssh-copy-id -i ~/.ssh/id_ed25519.pub build-user@203.0.113.10

# Test key-based login (without password)
ssh build-user@203.0.113.10

# On the SERVER — disable password login
sudo nano /etc/ssh/sshd_config
```

```
# /etc/ssh/sshd_config — change:
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
AuthorizedKeysFile .ssh/authorized_keys
MaxAuthTries 3
AllowUsers build-user
```

```bash
# Validate the config and restart
sudo sshd -t
sudo systemctl restart sshd

# IMPORTANT: do NOT close the current session!
# Open a NEW terminal and verify login:
ssh build-user@203.0.113.10
```

## 4. fail2ban

```bash
sudo apt update && sudo apt install -y fail2ban

sudo cp /etc/fail2ban/jail.conf /etc/fail2ban/jail.local
sudo nano /etc/fail2ban/jail.local
```

```ini
# /etc/fail2ban/jail.local
[DEFAULT]
bantime = 3600
findtime = 600
maxretry = 3
banaction = ufw

[sshd]
enabled = true
port = 22
filter = sshd
logpath = /var/log/auth.log
maxretry = 3
bantime = 86400
```

```bash
sudo systemctl enable fail2ban
sudo systemctl start fail2ban
sudo fail2ban-client status sshd
```

## 5. UFW (firewall)

```bash
# Configure rules BEFORE enabling
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp      # SSH

# If the server runs an OES daemon (HTTP API):
sudo ufw allow 8765/tcp    # OES HTTP API (port from oes.conf)
# OR only via nginx reverse proxy:
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# Enable
sudo ufw enable
# Confirm: y

# Verify
sudo ufw status verbose
```

> If the OES daemon is only reachable via nginx, do NOT open port 8765 externally — only nginx proxies to it locally.

## 6. System updates

```bash
# Update all packages
sudo apt update && sudo apt upgrade -y

# Install base utilities
sudo apt install -y \
  curl wget git htop iotop \
  build-essential cmake ninja-build \
  software-properties-common \
  apt-transport-https \
  ca-certificates \
  gnupg lsb-release \
  unzip jq tree \
  logrotate \
  pkg-config

# Automatic security updates
sudo apt install -y unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades
# Select "Yes"

sudo nano /etc/apt/apt.conf.d/50unattended-upgrades
```

```
Unattended-Upgrade::Allowed-Origins {
    "${distro_id}:${distro_codename}-security";
};
Unattended-Upgrade::Automatic-Reboot "false";
Unattended-Upgrade::Mail "admin@oes-team.com";
```

## 7. C++ build tools (Build Server)

### Linux (Ubuntu 22.04/24.04)

```bash
# GCC (latest stable version)
sudo apt install -y gcc-13 g++-13
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# Verify
gcc --version   # gcc 13.x
g++ --version

# CMake (>= 3.25 for modern features)
sudo apt install -y cmake
cmake --version
# If the version is too old — install from the Kitware repo:
# https://apt.kitware.com/

# Ninja (fast build system)
sudo apt install -y ninja-build

# LLVM/Clang (optional, for static analysis)
sudo apt install -y clang clang-tidy clang-format
```

### macOS (Homebrew)

```bash
# Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Compilers and build tools
brew install gcc cmake ninja pkg-config

# Verify
gcc --version
cmake --version   # >= 3.25

# Clang is already installed via Xcode Command Line Tools:
xcode-select --install
clang --version
```

## 8. wxWidgets dependencies (Build Server)

### Linux (Ubuntu 22.04/24.04)

```bash
# Dependencies for building wxWidgets 3.3.x on Linux (GTK)
sudo apt install -y \
  libgtk-3-dev \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libwebkit2gtk-4.1-dev \
  libpng-dev libjpeg-dev libtiff-dev \
  libexpat1-dev \
  libxkbcommon-dev \
  libwayland-dev \
  libcurl4-openssl-dev \
  libssl-dev \
  zlib1g-dev

# Build wxWidgets from source (if no package of required version)
WX_VERSION=3.3.2
cd /opt
sudo wget https://github.com/wxWidgets/wxWidgets/releases/download/v${WX_VERSION}/wxWidgets-${WX_VERSION}.tar.bz2
sudo tar xjf wxWidgets-${WX_VERSION}.tar.bz2
cd wxWidgets-${WX_VERSION}
sudo mkdir build-gtk && cd build-gtk
sudo ../configure --enable-unicode --enable-shared --with-gtk=3 --prefix=/usr/local
sudo make -j$(nproc)
sudo make install
sudo ldconfig

# Verify
wx-config --version
```

### macOS (Homebrew)

```bash
# wxWidgets 3.3.x via Homebrew (uses the Cocoa backend)
brew install wxwidgets

# Verify
wx-config --version
wx-config --cxxflags
```

## 9. Firebird dependencies (Build Server / Daemon Server)

### Linux (Ubuntu 22.04/24.04)

```bash
# Firebird client and dev packages
sudo apt install -y firebird3.0-dev libfbclient2

# For OES daemon — Firebird server (if not embedded)
sudo apt install -y firebird3.0-server

# Firebird embedded — shipped with OES, no system package required
# (libfbembed.so is bundled with the application)

# Verify
isql-fb -z
```

### macOS (Homebrew)

```bash
# Firebird via Homebrew (client + server)
brew install firebird

# Start Firebird server (if server mode is needed)
brew services start firebird

# Verify
isql-fb -z
# Or directly:
/usr/local/opt/firebird/bin/isql -z
```

## 10. PostgreSQL client and server

```bash
# PostgreSQL (if used as the OES backend)
sudo apt install -y postgresql postgresql-contrib libpq-dev

# Check status
sudo systemctl status postgresql
sudo systemctl enable postgresql

# Create a user and database for OES
sudo -u postgres psql
```

```sql
-- In psql:
CREATE USER oes_user WITH PASSWORD 'GENERATED_PASSWORD';
CREATE DATABASE oes_db OWNER oes_user;
GRANT ALL PRIVILEGES ON DATABASE oes_db TO oes_user;

-- Connect and configure
\c oes_db
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

\q
```

```bash
# Verify connection
psql -U oes_user -d oes_db -h localhost
```

## 11. OES Daemon deployment

```bash
# Create directories
sudo mkdir -p /opt/oes
sudo mkdir -p /var/lib/oes/databases
sudo mkdir -p /var/log/oes
sudo mkdir -p /etc/oes

# Copy the binary (from a build artifact)
# scp build-user@build-server:/artifacts/oes-daemon /opt/oes/oes-daemon
# or via a deploy script

# Set permissions
sudo chown -R oes:oes /opt/oes
sudo chown -R oes:oes /var/lib/oes
sudo chown -R oes:oes /var/log/oes
sudo chmod 755 /opt/oes/oes-daemon

# Configuration
sudo cp /path/to/oes.conf.example /etc/oes/oes.conf
sudo nano /etc/oes/oes.conf
sudo chown oes:oes /etc/oes/oes.conf
sudo chmod 600 /etc/oes/oes.conf
```

## 12. OES as a systemd service (Linux Daemon)

```bash
sudo nano /etc/systemd/system/oes-daemon.service
```

```ini
[Unit]
Description=OES Enterprise Low-Code Platform Daemon
After=network.target postgresql.service
Wants=postgresql.service

[Service]
Type=simple
User=oes
Group=oes
WorkingDirectory=/var/lib/oes
ExecStart=/opt/oes/oes-daemon --config /etc/oes/oes.conf --daemon
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=10
StandardOutput=append:/var/log/oes/daemon.log
StandardError=append:/var/log/oes/daemon-error.log

# Security
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/oes /var/log/oes
PrivateTmp=yes
CapabilityBoundingSet=

[Install]
WantedBy=multi-user.target
```

```bash
# Activate
sudo systemctl daemon-reload
sudo systemctl enable oes-daemon
sudo systemctl start oes-daemon

# Verify
sudo systemctl status oes-daemon
sudo journalctl -u oes-daemon -f
```

## 13. Nginx (reverse proxy for OES Daemon)

```bash
sudo apt install -y nginx

# Config for OES daemon
sudo nano /etc/nginx/sites-available/oes-daemon
```

```nginx
server {
    listen 80;
    server_name oes.example.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name oes.example.com;

    ssl_certificate /etc/ssl/cloudflare/cert.pem;
    ssl_certificate_key /etc/ssl/cloudflare/key.pem;
    ssl_protocols TLSv1.2 TLSv1.3;

    location / {
        proxy_pass http://127.0.0.1:8765;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 300s;
    }
}
```

```bash
sudo ln -s /etc/nginx/sites-available/oes-daemon /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

## 14. SSL certificate

### Option A: Cloudflare (recommended for update/license servers)

```bash
# In the Cloudflare Dashboard:
# SSL/TLS -> Origin Server -> Create Certificate
# Copy the certificate and key

sudo mkdir -p /etc/ssl/cloudflare
sudo nano /etc/ssl/cloudflare/cert.pem    # Paste the certificate
sudo nano /etc/ssl/cloudflare/key.pem     # Paste the private key
sudo chmod 600 /etc/ssl/cloudflare/key.pem
```

### Option B: Certbot (Let's Encrypt)

```bash
sudo apt install -y certbot python3-certbot-nginx
sudo certbot --nginx -d oes.example.com

# Auto-renewal
sudo certbot renew --dry-run
```

## 15. Cloning the OES repository on the build server

```bash
# Create an SSH key for the build server (deploy key)
ssh-keygen -t ed25519 -C "deploy@oes-build" -f ~/.ssh/deploy_key -N ""

# Add the public key to GitHub:
# Repository -> Settings -> Deploy Keys -> Add (read-only)
cat ~/.ssh/deploy_key.pub

# Configure SSH for GitHub
nano ~/.ssh/config
```

```
Host github.com
  HostName github.com
  User git
  IdentityFile ~/.ssh/deploy_key
```

```bash
# Linux / macOS — clone the project
sudo mkdir -p /opt/build
sudo chown build-user:build-user /opt/build
git clone git@github.com:org/enterprise.git /opt/build/enterprise
cd /opt/build/enterprise
git checkout master   # main production branch
# git checkout develop  # integration branch
```

## 16. Building OES on the build server

### Linux / macOS (CMake + Ninja)

```bash
cd /opt/build/enterprise

# CMake generation
mkdir -p build/release && cd build/release
cmake ../.. \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DwxWidgets_ROOT=/usr/local \
  -DOES_BUILD_DAEMON=ON

# Build
ninja -j$(nproc)

# Artifacts
ls -la bin/
# oes-enterprise   — main desktop application (src/engine/enterprise/mainApp.cpp)
# oes-designer     — designer (src/engine/designer/mainApp.cpp)
# oes-daemon       — daemon/service binary (src/engine/daemon/daemon.cpp)
```

### Windows (MSBuild)

```powershell
# In Developer Command Prompt / PowerShell with MSBuild on PATH
cd C:\build\enterprise
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m
# Artifacts: bin\x64\Release\
```

## 17. OES Daemon deploy script

```bash
#!/bin/bash
# /opt/scripts/deploy-oes-daemon.sh
set -e

BUILD_SERVER="build-user@build-server"
BUILD_ARTIFACT="/opt/build/oes-enterprise/build/release/bin/oes-daemon"
DEPLOY_DIR="/opt/oes"
SERVICE_NAME="oes-daemon"

echo "=== OES Daemon deploy ==="

# Copy the new binary
echo "Copying binary..."
scp ${BUILD_SERVER}:${BUILD_ARTIFACT} /tmp/oes-daemon-new

# Stop the service
echo "Stopping service..."
sudo systemctl stop ${SERVICE_NAME}

# Replace the binary
sudo mv /tmp/oes-daemon-new ${DEPLOY_DIR}/oes-daemon
sudo chown oes:oes ${DEPLOY_DIR}/oes-daemon
sudo chmod 755 ${DEPLOY_DIR}/oes-daemon

# Start the service
echo "Starting service..."
sudo systemctl start ${SERVICE_NAME}

# Health check
sleep 3
if sudo systemctl is-active --quiet ${SERVICE_NAME}; then
  echo "=== Deploy completed successfully ==="
  sudo systemctl status ${SERVICE_NAME} --no-pager
else
  echo "ERROR: service failed to start!"
  sudo journalctl -u ${SERVICE_NAME} --lines 30 --no-pager
  exit 1
fi
```

```bash
chmod +x /opt/scripts/deploy-oes-daemon.sh
```

## 18. Swap file (if RAM is limited)

```bash
# For servers with 2-4 GB RAM (C++ compilation is memory-intensive)
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Make persistent
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab

# Tune swappiness
echo 'vm.swappiness=10' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

# Verify
free -h
```

> C++ project compilation requires significantly more RAM than Node.js/Python. For a build server, at least 8 GB RAM is recommended; for a daemon server, 2 GB.

## 19. Log rotation

```bash
# OES daemon logs
sudo nano /etc/logrotate.d/oes-daemon
```

```
/var/log/oes/*.log {
    daily
    missingok
    rotate 30
    compress
    delaycompress
    notifempty
    copytruncate
    postrotate
        systemctl kill -s HUP oes-daemon 2>/dev/null || true
    endscript
}
```

```bash
# Build logs (build server)
sudo nano /etc/logrotate.d/oes-build
```

```
/opt/build/logs/*.log {
    weekly
    missingok
    rotate 8
    compress
    delaycompress
    notifempty
}
```

---

## Final checklist

### Build Server

```
[ ] User build-user created
[ ] SSH key added, password login disabled
[ ] Root login disabled
[ ] fail2ban running
[ ] UFW enabled (port 22 only)
[ ] Automatic updates configured
[ ] GCC 13+ / Clang installed
[ ] CMake 3.25+ installed
[ ] Ninja installed
[ ] wxWidgets 3.3.x built / installed
[ ] Firebird dev packages installed
[ ] PostgreSQL dev packages installed
[ ] Repository cloned
[ ] Test build successful
[ ] Swap file (if needed, min. 4GB for C++ builds)
[ ] Log rotation configured
```

### Daemon Server

```
[ ] User oes created (no sudo)
[ ] SSH key for build-user added, password login disabled
[ ] Root login disabled
[ ] fail2ban running
[ ] UFW enabled (22, 443 or 8765)
[ ] Automatic updates configured
[ ] Firebird / PostgreSQL installed and configured
[ ] OES directories created with correct permissions
[ ] oes.conf created and protected (chmod 600)
[ ] OES daemon compiled and placed in /opt/oes/
[ ] systemd unit created and enabled
[ ] OES daemon running and operational
[ ] Nginx configured as reverse proxy (if needed)
[ ] SSL certificate configured
[ ] Swap file (if needed)
[ ] Log rotation configured
[ ] Backups configured
[ ] Monitoring configured
```
