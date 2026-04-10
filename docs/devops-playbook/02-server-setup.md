# 02. Настройка сервера с нуля — OES

> Ubuntu 22.04/24.04 LTS или macOS 13+ → production-ready сервер/рабочая станция для сборки OES и/или развёртывания OES daemon.

В OES сервер используется в двух ролях:
- **Build Server** — компиляция C++/wxWidgets, сборка инсталлятора, CI/CD
- **OES Daemon Server** — запуск OES в режиме сервиса (headless, HTTP API для тонких клиентов)

---

## 1. Первый вход

```bash
# Подключиться по SSH (провайдер даёт root пароль)
ssh root@203.0.113.10

# Сменить пароль root
passwd
# Ввести новый сложный пароль (сохранить в 1Password)
```

## 2. Создать пользователя

```bash
# Для build-сервера
adduser build-user
usermod -aG sudo build-user

# Для сервера с OES daemon — отдельный непривилегированный пользователь
adduser oes
# НЕ давать sudo (принцип минимальных привилегий)

# Проверить
su - build-user
sudo whoami
# root
```

## 3. SSH: ключ + отключить пароль

```bash
# На ЛОКАЛЬНОЙ машине — скопировать ключ на сервер
ssh-copy-id -i ~/.ssh/id_ed25519.pub build-user@203.0.113.10

# Проверить вход по ключу (без пароля)
ssh build-user@203.0.113.10

# На СЕРВЕРЕ — отключить вход по паролю
sudo nano /etc/ssh/sshd_config
```

```
# /etc/ssh/sshd_config — изменить:
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
AuthorizedKeysFile .ssh/authorized_keys
MaxAuthTries 3
AllowUsers build-user
```

```bash
# Проверить конфиг и перезапустить
sudo sshd -t
sudo systemctl restart sshd

# ВАЖНО: НЕ закрывать текущую сессию!
# Открыть НОВЫЙ терминал и проверить вход:
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

## 5. UFW (файрвол)

```bash
# Настроить правила ДО включения
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp      # SSH

# Если сервер — OES daemon (HTTP API):
sudo ufw allow 8765/tcp    # OES HTTP API (порт из oes.conf)
# ИЛИ только через nginx reverse proxy:
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# Включить
sudo ufw enable
# Подтвердить: y

# Проверить
sudo ufw status verbose
```

> Если OES daemon доступен только через nginx, порт 8765 НЕ открывать наружу — только nginx проксирует к нему локально.

## 6. Обновление системы

```bash
# Обновить все пакеты
sudo apt update && sudo apt upgrade -y

# Установить базовые утилиты
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

# Автообновления безопасности
sudo apt install -y unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades
# Выбрать "Yes"

sudo nano /etc/apt/apt.conf.d/50unattended-upgrades
```

```
Unattended-Upgrade::Allowed-Origins {
    "${distro_id}:${distro_codename}-security";
};
Unattended-Upgrade::Automatic-Reboot "false";
Unattended-Upgrade::Mail "admin@oes-team.com";
```

## 7. Инструменты C++ сборки (Build Server)

### Linux (Ubuntu 22.04/24.04)

```bash
# GCC (последняя стабильная версия)
sudo apt install -y gcc-13 g++-13
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# Проверить
gcc --version   # gcc 13.x
g++ --version

# CMake (>= 3.25 для современных фич)
sudo apt install -y cmake
cmake --version
# Если версия старая — установить из Kitware репозитория:
# https://apt.kitware.com/

# Ninja (быстрый build system)
sudo apt install -y ninja-build

# LLVM/Clang (опционально, для статического анализа)
sudo apt install -y clang clang-tidy clang-format
```

### macOS (Homebrew)

```bash
# Homebrew (если не установлен)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Компиляторы и инструменты сборки
brew install gcc cmake ninja pkg-config

# Проверить
gcc --version
cmake --version   # >= 3.25

# Clang уже установлен через Xcode Command Line Tools:
xcode-select --install
clang --version
```

## 8. wxWidgets зависимости (Build Server)

### Linux (Ubuntu 22.04/24.04)

```bash
# Зависимости для сборки wxWidgets 3.3.x под Linux (GTK)
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

# Собрать wxWidgets из исходников (если нет пакета нужной версии)
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

# Проверить
wx-config --version
```

### macOS (Homebrew)

```bash
# wxWidgets 3.3.x через Homebrew (использует Cocoa backend)
brew install wxwidgets

# Проверить
wx-config --version
wx-config --cxxflags
```

## 9. Firebird зависимости (Build Server / Daemon Server)

### Linux (Ubuntu 22.04/24.04)

```bash
# Firebird клиент и dev-пакеты
sudo apt install -y firebird3.0-dev libfbclient2

# Для OES daemon — Firebird server (если не embedded)
sudo apt install -y firebird3.0-server

# Firebird embedded — поставляется вместе с OES, не требует установки системного пакета
# (libfbembed.so идёт в bundle с приложением)

# Проверить
isql-fb -z
```

### macOS (Homebrew)

```bash
# Firebird через Homebrew (клиент + сервер)
brew install firebird

# Запустить Firebird сервер (если нужен серверный режим)
brew services start firebird

# Проверить
isql-fb -z
# Или напрямую:
/usr/local/opt/firebird/bin/isql -z
```

## 10. PostgreSQL клиент и сервер

```bash
# PostgreSQL (если используется как backend для OES)
sudo apt install -y postgresql postgresql-contrib libpq-dev

# Проверить статус
sudo systemctl status postgresql
sudo systemctl enable postgresql

# Создать пользователя и базу для OES
sudo -u postgres psql
```

```sql
-- В psql:
CREATE USER oes_user WITH PASSWORD 'СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ';
CREATE DATABASE oes_db OWNER oes_user;
GRANT ALL PRIVILEGES ON DATABASE oes_db TO oes_user;

-- Подключиться и настроить
\c oes_db
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

\q
```

```bash
# Проверить подключение
psql -U oes_user -d oes_db -h localhost
```

## 11. Развёртывание OES Daemon

```bash
# Создать директории
sudo mkdir -p /opt/oes
sudo mkdir -p /var/lib/oes/databases
sudo mkdir -p /var/log/oes
sudo mkdir -p /etc/oes

# Скопировать бинарник (из артефакта сборки)
# scp build-user@build-server:/artifacts/oes-daemon /opt/oes/oes-daemon
# или через скрипт деплоя

# Установить права
sudo chown -R oes:oes /opt/oes
sudo chown -R oes:oes /var/lib/oes
sudo chown -R oes:oes /var/log/oes
sudo chmod 755 /opt/oes/oes-daemon

# Конфигурация
sudo cp /path/to/oes.conf.example /etc/oes/oes.conf
sudo nano /etc/oes/oes.conf
sudo chown oes:oes /etc/oes/oes.conf
sudo chmod 600 /etc/oes/oes.conf
```

## 12. OES как systemd сервис (Linux Daemon)

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

# Безопасность
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
# Активировать
sudo systemctl daemon-reload
sudo systemctl enable oes-daemon
sudo systemctl start oes-daemon

# Проверить
sudo systemctl status oes-daemon
sudo journalctl -u oes-daemon -f
```

## 13. Nginx (reverse proxy для OES Daemon)

```bash
sudo apt install -y nginx

# Конфиг для OES daemon
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

## 14. SSL сертификат

### Вариант A: Cloudflare (рекомендуется для update/license серверов)

```bash
# В Cloudflare Dashboard:
# SSL/TLS → Origin Server → Create Certificate
# Скопировать сертификат и ключ

sudo mkdir -p /etc/ssl/cloudflare
sudo nano /etc/ssl/cloudflare/cert.pem    # Вставить сертификат
sudo nano /etc/ssl/cloudflare/key.pem     # Вставить приватный ключ
sudo chmod 600 /etc/ssl/cloudflare/key.pem
```

### Вариант B: Certbot (Let's Encrypt)

```bash
sudo apt install -y certbot python3-certbot-nginx
sudo certbot --nginx -d oes.example.com

# Автообновление
sudo certbot renew --dry-run
```

## 15. Клонирование репозитория OES на build-сервер

```bash
# Создать SSH ключ для build-сервера (deploy key)
ssh-keygen -t ed25519 -C "deploy@oes-build" -f ~/.ssh/deploy_key -N ""

# Добавить публичный ключ в GitHub:
# Repository → Settings → Deploy Keys → Add (read-only)
cat ~/.ssh/deploy_key.pub

# Настроить SSH для GitHub
nano ~/.ssh/config
```

```
Host github.com
  HostName github.com
  User git
  IdentityFile ~/.ssh/deploy_key
```

```bash
# Linux / macOS — клонировать проект
sudo mkdir -p /opt/build
sudo chown build-user:build-user /opt/build
git clone git@github.com:org/enterprise.git /opt/build/enterprise
cd /opt/build/enterprise
git checkout master   # основная ветка production
# git checkout develop  # ветка интеграции
```

## 16. Сборка OES на build-сервере

### Linux / macOS (CMake + Ninja)

```bash
cd /opt/build/enterprise

# Генерация CMake
mkdir -p build/release && cd build/release
cmake ../.. \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DwxWidgets_ROOT=/usr/local \
  -DOES_BUILD_DAEMON=ON

# Сборка
ninja -j$(nproc)

# Артефакты
ls -la bin/
# oes-enterprise   — основное desktop-приложение (src/engine/enterprise/mainApp.cpp)
# oes-designer     — дизайнер (src/engine/designer/mainApp.cpp)
# oes-daemon       — daemon/service бинарник (src/engine/daemon/daemon.cpp)
```

### Windows (MSBuild)

```powershell
# В Developer Command Prompt / PowerShell с MSBuild в PATH
cd C:\build\enterprise
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m
# Артефакты: bin\x64\Release\
```

## 17. Скрипт деплоя OES Daemon

```bash
#!/bin/bash
# /opt/scripts/deploy-oes-daemon.sh
set -e

BUILD_SERVER="build-user@build-server"
BUILD_ARTIFACT="/opt/build/oes-enterprise/build/release/bin/oes-daemon"
DEPLOY_DIR="/opt/oes"
SERVICE_NAME="oes-daemon"

echo "=== Деплой OES Daemon ==="

# Скопировать новый бинарник
echo "Копирование бинарника..."
scp ${BUILD_SERVER}:${BUILD_ARTIFACT} /tmp/oes-daemon-new

# Остановить сервис
echo "Остановка сервиса..."
sudo systemctl stop ${SERVICE_NAME}

# Заменить бинарник
sudo mv /tmp/oes-daemon-new ${DEPLOY_DIR}/oes-daemon
sudo chown oes:oes ${DEPLOY_DIR}/oes-daemon
sudo chmod 755 ${DEPLOY_DIR}/oes-daemon

# Запустить сервис
echo "Запуск сервиса..."
sudo systemctl start ${SERVICE_NAME}

# Проверить здоровье
sleep 3
if sudo systemctl is-active --quiet ${SERVICE_NAME}; then
  echo "=== Деплой завершён успешно ==="
  sudo systemctl status ${SERVICE_NAME} --no-pager
else
  echo "ОШИБКА: сервис не запустился!"
  sudo journalctl -u ${SERVICE_NAME} --lines 30 --no-pager
  exit 1
fi
```

```bash
chmod +x /opt/scripts/deploy-oes-daemon.sh
```

## 18. Swap файл (если мало RAM)

```bash
# Для серверов с 2-4 GB RAM (C++ компиляция требовательна к памяти)
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Сделать постоянным
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab

# Настроить swappiness
echo 'vm.swappiness=10' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

# Проверить
free -h
```

> Компиляция C++ проектов требует значительно больше RAM, чем Node.js/Python. Для build-сервера рекомендуется минимум 8 GB RAM, для daemon-сервера — 2 GB.

## 19. Логротация

```bash
# Логи OES daemon
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
# Логи сборки (build-сервер)
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

## Финальный чеклист

### Build Server

```
[ ] Пользователь build-user создан
[ ] SSH ключ добавлен, вход по паролю отключён
[ ] Root login отключён
[ ] fail2ban работает
[ ] UFW включён (только 22)
[ ] Автообновления настроены
[ ] GCC 13+ / Clang установлены
[ ] CMake 3.25+ установлен
[ ] Ninja установлен
[ ] wxWidgets 3.3.x собран / установлен
[ ] Firebird dev-пакеты установлены
[ ] PostgreSQL dev-пакеты установлены
[ ] Репозиторий склонирован
[ ] Тестовая сборка прошла успешно
[ ] Swap файл (если нужен, мин. 4GB для C++ сборки)
[ ] Логротация настроена
```

### Daemon Server

```
[ ] Пользователь oes создан (без sudo)
[ ] SSH ключ для build-user добавлен, вход по паролю отключён
[ ] Root login отключён
[ ] fail2ban работает
[ ] UFW включён (22, 443 или 8765)
[ ] Автообновления настроены
[ ] Firebird / PostgreSQL установлены и настроены
[ ] Директории OES созданы с правильными правами
[ ] oes.conf создан и защищён (chmod 600)
[ ] OES daemon скомпилирован и размещён в /opt/oes/
[ ] systemd unit создан и включён
[ ] OES daemon запущен и работает
[ ] Nginx настроен как reverse proxy (если нужен)
[ ] SSL сертификат настроен
[ ] Swap файл (если нужен)
[ ] Логротация настроена
[ ] Бэкапы настроены
[ ] Мониторинг настроен
```
