# 05. Docker — контейнеризация для OES

> Docker в контексте OES используется преимущественно для:
> 1. **Build-контейнеров** — воспроизводимая сборка C++/wxWidgets в CI/CD
> 2. **Dev-окружения** — поднять PostgreSQL, Firebird, вспомогательные сервисы локально
> 3. **OES Daemon** — опционально, для серверного (headless) режима на Linux

> Desktop-режим OES (wxWidgets GUI) контейнеризации не подлежит.

---

## Dockerfile: C++ Build образ (OES компилятор)

```dockerfile
# Dockerfile.build
# Образ для воспроизводимой сборки OES на Linux (C++17 + wxWidgets)

FROM ubuntu:22.04 AS base

# Отключить интерактивные диалоги apt
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Базовые инструменты
RUN apt-get update && apt-get install -y \
    # Компиляторы
    gcc-13 g++-13 \
    # Сборка
    cmake ninja-build make \
    pkg-config \
    # VCS
    git \
    # Утилиты
    curl wget unzip \
    # wxWidgets зависимости (GTK3 для headless daemon, без GUI)
    libgtk-3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libpng-dev libjpeg-dev libtiff-dev \
    libexpat1-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev \
    # Firebird
    firebird3.0-dev libfbclient2 \
    # PostgreSQL
    libpq-dev \
    # MySQL
    libmysqlclient-dev \
    # SQLite
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# Задать gcc-13 как дефолтный
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# --- Собрать wxWidgets из исходников ---
FROM base AS wxwidgets-builder

ARG WX_VERSION=3.3.2

WORKDIR /opt/wxwidgets-src
RUN curl -fsSL https://github.com/wxWidgets/wxWidgets/releases/download/v${WX_VERSION}/wxWidgets-${WX_VERSION}.tar.bz2 \
    | tar xj --strip-components=1

# Для daemon-режима (headless): GTK3 backend без дисплея (не использовать --disable-gui с --with-gtk=3)
RUN mkdir build-release && cd build-release && \
    ../configure \
      --enable-unicode \
      --disable-shared \
      --with-gtk=3 \
      --prefix=/usr/local \
      CXX=g++ CXXFLAGS="-std=c++17 -O2" && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# --- Финальный build-образ ---
FROM base AS builder

# Скопировать собранный wxWidgets
COPY --from=wxwidgets-builder /usr/local /usr/local
RUN ldconfig

# Настройка рабочей директории
WORKDIR /workspace

# Метаданные образа
LABEL org.opencontainers.image.description="OES C++ build environment"
LABEL org.opencontainers.image.source="https://github.com/org/oes-enterprise"
```

Использование build-образа:
```bash
# Собрать образ
docker build -f Dockerfile.build -t oes-builder:latest .
docker build -f Dockerfile.build -t oes-builder:wx3.3.2 \
  --build-arg WX_VERSION=3.3.2 .

# Скомпилировать OES внутри контейнера
docker run --rm \
  -v $(pwd):/workspace \
  -v oes-build-cache:/workspace/build \
  oes-builder:latest \
  bash -c "cd /workspace && \
    mkdir -p build/release && cd build/release && \
    cmake ../.. -G Ninja -DCMAKE_BUILD_TYPE=Release && \
    ninja -j$(nproc)"
```

---

## Dockerfile: OES Daemon (production образ)

```dockerfile
# Dockerfile.daemon
# Многоэтапная сборка: compile → minimal runtime image

# === Этап 1: Компиляция ===
FROM ghcr.io/org/oes-builder:latest AS compiler

WORKDIR /src
COPY . .

RUN mkdir -p build/release && cd build/release && \
    cmake ../.. \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -DOES_BUILD_DAEMON=ON \
      -DOES_BUILD_DESKTOP=OFF && \
    ninja -j$(nproc) oes-daemon

# === Этап 2: Runtime образ (минимальный) ===
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Только runtime зависимости (не dev-пакеты)
RUN apt-get update && apt-get install -y \
    libfbclient2 \
    libpq5 \
    libsqlite3-0 \
    libmysqlclient21 \
    libcurl4 \
    libssl3 \
    zlib1g \
    libgtk-3-0 \
    && rm -rf /var/lib/apt/lists/*

# Безопасность: не запускать от root
RUN groupadd --system --gid 1001 oes && \
    useradd --system --uid 1001 --gid oes --home /var/lib/oes oes

# Директории
RUN mkdir -p /opt/oes /var/lib/oes /var/log/oes /etc/oes && \
    chown -R oes:oes /var/lib/oes /var/log/oes

# Скопировать daemon бинарник
COPY --from=compiler --chown=oes:oes /src/build/release/bin/oes-daemon /opt/oes/oes-daemon
RUN chmod 755 /opt/oes/oes-daemon

# Конфиг по умолчанию (будет перезаписан mount-ом или env)
COPY --chown=oes:oes config/oes.conf.docker /etc/oes/oes.conf

USER oes

WORKDIR /var/lib/oes

EXPOSE 8765

HEALTHCHECK --interval=30s --timeout=10s --start-period=30s --retries=3 \
    CMD curl -sf http://localhost:8765/health || exit 1

CMD ["/opt/oes/oes-daemon", "--config", "/etc/oes/oes.conf", "--foreground"]
```

---

## .dockerignore

```
# .dockerignore
.git
.gitignore
build/
cmake-build-*/
*.o
*.a
*.so
*.dll
*.exe
.vs/
.vscode/
.idea/
*.user
*.suo
*.ncb
*.db
*.fdb
*.gdb
*.log

# Конфиги с секретами
oes.conf
oes.conf.production
*.conf.enc
*.key
*.pem
*.pfx
*.p12

# Документация
docs/
*.md
!README.md

# Тесты (не нужны в production образе)
tests/
test/
*_test.cpp
*_test.h
```

---

## docker-compose: Dev-окружение (базы данных)

```yaml
# docker-compose.dev.yml
# Инфраструктура для разработки OES: базы данных без приложения
# Само приложение запускается нативно (Visual Studio / CLion)

version: '3.8'

services:
  # === PostgreSQL (основной или альтернативный backend) ===
  postgres:
    image: postgres:16-alpine
    container_name: oes-dev-postgres
    restart: unless-stopped
    environment:
      POSTGRES_USER: oes_user
      POSTGRES_PASSWORD: dev_password_change_me
      POSTGRES_DB: oes_db
    ports:
      - "5432:5432"
    volumes:
      - postgres_data:/var/lib/postgresql/data
      - ./scripts/postgres-init.sql:/docker-entrypoint-initdb.d/init.sql:ro
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U oes_user -d oes_db"]
      interval: 10s
      timeout: 5s
      retries: 5

  # === Firebird Server (альтернатива embedded для dev) ===
  firebird:
    image: jacobalberty/firebird:v4.0
    container_name: oes-dev-firebird
    restart: unless-stopped
    environment:
      FIREBIRD_ROOT_PASSWORD: masterkey_dev
      FIREBIRD_USER: oes_user
      FIREBIRD_PASSWORD: dev_fb_password
      FIREBIRD_DATABASE: oes.fdb
    ports:
      - "3050:3050"
    volumes:
      - firebird_data:/firebird/data
    healthcheck:
      test: ["CMD-SHELL", "nc -z localhost 3050 || exit 1"]
      interval: 15s
      timeout: 10s
      retries: 5

  # === MySQL (если тестируем MySQL backend) ===
  mysql:
    image: mysql:8.0
    container_name: oes-dev-mysql
    restart: unless-stopped
    environment:
      MYSQL_ROOT_PASSWORD: dev_root_password
      MYSQL_USER: oes_user
      MYSQL_PASSWORD: dev_mysql_password
      MYSQL_DATABASE: oes_db
    ports:
      - "3306:3306"
    volumes:
      - mysql_data:/var/lib/mysql
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost",
             "-u", "oes_user", "--password=dev_mysql_password"]
      interval: 15s
      timeout: 5s
      retries: 5

  # === Adminer — web UI для всех баз ===
  adminer:
    image: adminer:latest
    container_name: oes-dev-adminer
    restart: unless-stopped
    ports:
      - "8080:8080"
    environment:
      ADMINER_DEFAULT_SERVER: postgres
    depends_on:
      - postgres
      - mysql

volumes:
  postgres_data:
  firebird_data:
  mysql_data:
```

```bash
# Поднять dev-базы
docker compose -f docker-compose.dev.yml up -d

# Поднять только PostgreSQL
docker compose -f docker-compose.dev.yml up -d postgres

# Остановить
docker compose -f docker-compose.dev.yml down

# Удалить данные (полный сброс)
docker compose -f docker-compose.dev.yml down -v
```

---

## docker-compose: OES Daemon (production)

```yaml
# docker-compose.yml
version: '3.8'

services:
  # === OES Daemon ===
  oes-daemon:
    image: ghcr.io/org/oes-daemon:${OES_VERSION:-latest}
    container_name: oes-daemon
    restart: unless-stopped
    ports:
      - "8765:8765"
    volumes:
      # Конфигурация (монтировать, не встраивать в образ)
      - /etc/oes/oes.conf:/etc/oes/oes.conf:ro
      # Данные (базы, загруженные файлы)
      - oes_data:/var/lib/oes
      # Логи
      - /var/log/oes:/var/log/oes
    environment:
      - OES_LOG_LEVEL=info
    healthcheck:
      test: ["CMD", "curl", "-sf", "http://localhost:8765/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 30s
    depends_on:
      postgres:
        condition: service_healthy
    networks:
      - oes-network

  # === PostgreSQL ===
  postgres:
    image: postgres:16-alpine
    container_name: oes-postgres
    restart: unless-stopped
    environment:
      POSTGRES_USER: ${DB_USER}
      POSTGRES_PASSWORD: ${DB_PASSWORD}
      POSTGRES_DB: ${DB_NAME}
    volumes:
      - postgres_data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U ${DB_USER} -d ${DB_NAME}"]
      interval: 10s
      timeout: 5s
      retries: 5
    networks:
      - oes-network
    # НЕ публикуем порт наружу — доступ только из docker network

  # === Nginx (TLS termination) ===
  nginx:
    image: nginx:alpine
    container_name: oes-nginx
    restart: unless-stopped
    ports:
      - "80:80"
      - "443:443"
    volumes:
      - ./nginx/oes-daemon.conf:/etc/nginx/conf.d/oes-daemon.conf:ro
      - /etc/ssl/cloudflare:/etc/ssl/cloudflare:ro
      - /var/log/nginx:/var/log/nginx
    depends_on:
      oes-daemon:
        condition: service_healthy
    networks:
      - oes-network

volumes:
  oes_data:
    driver: local
  postgres_data:
    driver: local

networks:
  oes-network:
    driver: bridge
```

---

## GitHub Container Registry (ghcr.io)

```bash
# Авторизоваться
echo $GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin

# Собрать и запушить build-образ (редко меняется)
docker build -f Dockerfile.build \
  -t ghcr.io/org/oes-builder:wx3.3.2 \
  -t ghcr.io/org/oes-builder:latest \
  --build-arg WX_VERSION=3.3.2 .
docker push ghcr.io/org/oes-builder:wx3.3.2
docker push ghcr.io/org/oes-builder:latest

# Собрать и запушить daemon образ (при каждом релизе)
VERSION=$(git describe --tags --abbrev=0)  # например: v1.2.3
COMMIT=$(git rev-parse --short HEAD)

docker build -f Dockerfile.daemon \
  -t ghcr.io/org/oes-daemon:${VERSION} \
  -t ghcr.io/org/oes-daemon:${COMMIT} \
  -t ghcr.io/org/oes-daemon:latest .

docker push ghcr.io/org/oes-daemon:${VERSION}
docker push ghcr.io/org/oes-daemon:${COMMIT}
docker push ghcr.io/org/oes-daemon:latest

# На сервере — обновить
docker pull ghcr.io/org/oes-daemon:latest
OES_VERSION=latest docker compose up -d oes-daemon
```

---

## GitHub Actions: CI сборка в контейнере

```yaml
# .github/workflows/build.yml
name: Build OES

on:
  push:
    branches: [master, develop]   # master = production, develop = интеграция
  pull_request:

jobs:
  build-linux-daemon:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/org/oes-builder:wx3.3.2
      credentials:
        username: ${{ github.actor }}
        password: ${{ secrets.GITHUB_TOKEN }}

    steps:
      - uses: actions/checkout@v4

      - name: Configure CMake
        run: |
          mkdir -p build/release && cd build/release
          cmake ../.. \
            -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_STANDARD=17 \
            -DOES_BUILD_DAEMON=ON

      - name: Build
        run: |
          cd build/release
          ninja -j$(nproc)

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: oes-daemon-linux
          path: build/release/bin/oes-daemon
          retention-days: 30
```

---

## Полезные команды

```bash
# === Сборка ===
docker compose build                          # Собрать все
docker compose build --no-cache oes-daemon    # Без кэша
docker compose build oes-daemon               # Только daemon

# === Запуск ===
docker compose up -d                          # Запустить в фоне
docker compose up -d --build                  # Пересобрать и запустить

# === Статус ===
docker compose ps                             # Статус контейнеров
docker compose logs -f oes-daemon             # Логи в реальном времени
docker compose logs --tail=100 oes-daemon     # Последние 100 строк

# === Остановка ===
docker compose down                           # Остановить
docker compose down -v                        # Остановить + удалить volumes

# === Отладка ===
docker compose exec oes-daemon bash           # Shell внутрь контейнера
docker compose exec postgres psql -U oes_user -d oes_db

# === Очистка ===
docker system prune -f                        # Удалить неиспользуемые ресурсы
docker image prune -a -f                      # Удалить все неиспользуемые образы
docker volume prune -f                        # Удалить неиспользуемые volumes
```

---

## Когда Docker vs нативная установка для OES

```
Docker НЕ подходит для:
  — Desktop режим (wxWidgets GUI — нельзя контейнеризировать без X11/Wayland)
  — Windows-native OES с Firebird embedded (сложная интеграция)
  — Инсталлятор OES (NSIS/WiX — только нативно на Windows)

Docker ПОДХОДИТ для:
  — C++ build environment (воспроизводимая сборка в CI/CD)
  — OES Daemon (headless, Linux, серверный режим)
  — Dev-окружение (PostgreSQL, Firebird Server, MySQL для разработчиков)
  — License Server и Update Server вендора

Рекомендация для OES:
  — Build-контейнер в CI/CD (GitHub Actions) — ВСЕГДА
  — Dev базы данных через docker-compose.dev.yml — ВСЕГДА
  — OES Daemon в production — по желанию (Docker или systemd — оба хороши)
  — Desktop клиент — нативная установка (NSIS/WiX инсталлятор)
```
