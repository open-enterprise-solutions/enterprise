# 05. Docker — containerization for OES

> In the OES context, Docker is used primarily for:
> 1. **Build containers** — reproducible C++/wxWidgets builds in CI/CD
> 2. **Dev environment** — running PostgreSQL, Firebird, and helper services locally
> 3. **OES Daemon** — optionally, for server (headless) mode on Linux

> OES desktop mode (wxWidgets GUI) is not suitable for containerization.

---

## Dockerfile: C++ build image (OES compiler)

```dockerfile
# Dockerfile.build
# Image for reproducible OES builds on Linux (C++17 + wxWidgets)

FROM ubuntu:22.04 AS base

# Disable interactive apt prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Base tools
RUN apt-get update && apt-get install -y \
    # Compilers
    gcc-13 g++-13 \
    # Build
    cmake ninja-build make \
    pkg-config \
    # VCS
    git \
    # Utilities
    curl wget unzip \
    # wxWidgets dependencies (GTK3 for headless daemon, no GUI)
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

# Set gcc-13 as the default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# --- Build wxWidgets from source ---
FROM base AS wxwidgets-builder

ARG WX_VERSION=3.3.2

WORKDIR /opt/wxwidgets-src
RUN curl -fsSL https://github.com/wxWidgets/wxWidgets/releases/download/v${WX_VERSION}/wxWidgets-${WX_VERSION}.tar.bz2 \
    | tar xj --strip-components=1

# For daemon (headless) mode: GTK3 backend without a display
# (do not combine --disable-gui with --with-gtk=3)
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

# --- Final build image ---
FROM base AS builder

# Copy the built wxWidgets
COPY --from=wxwidgets-builder /usr/local /usr/local
RUN ldconfig

# Configure the working directory
WORKDIR /workspace

# Image metadata
LABEL org.opencontainers.image.description="OES C++ build environment"
LABEL org.opencontainers.image.source="https://github.com/org/oes-enterprise"
```

Using the build image:
```bash
# Build the image
docker build -f Dockerfile.build -t oes-builder:latest .
docker build -f Dockerfile.build -t oes-builder:wx3.3.2 \
  --build-arg WX_VERSION=3.3.2 .

# Compile OES inside the container
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

## Dockerfile: OES Daemon (production image)

```dockerfile
# Dockerfile.daemon
# Multi-stage build: compile -> minimal runtime image

# === Stage 1: Compilation ===
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

# === Stage 2: Runtime image (minimal) ===
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Runtime dependencies only (no dev packages)
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

# Security: do not run as root
RUN groupadd --system --gid 1001 oes && \
    useradd --system --uid 1001 --gid oes --home /var/lib/oes oes

# Directories
RUN mkdir -p /opt/oes /var/lib/oes /var/log/oes /etc/oes && \
    chown -R oes:oes /var/lib/oes /var/log/oes

# Copy the daemon binary
COPY --from=compiler --chown=oes:oes /src/build/release/bin/oes-daemon /opt/oes/oes-daemon
RUN chmod 755 /opt/oes/oes-daemon

# Default config (will be overridden by a mount or env)
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

# Configs with secrets
oes.conf
oes.conf.production
*.conf.enc
*.key
*.pem
*.pfx
*.p12

# Documentation
docs/
*.md
!README.md

# Tests (not needed in the production image)
tests/
test/
*_test.cpp
*_test.h
```

---

## docker-compose: dev environment (databases)

```yaml
# docker-compose.dev.yml
# Infrastructure for OES development: databases without the application
# The application itself runs natively (Visual Studio / CLion)

version: '3.8'

services:
  # === PostgreSQL (primary or alternative backend) ===
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

  # === Firebird Server (alternative to embedded for dev) ===
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

  # === MySQL (if testing the MySQL backend) ===
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

  # === Adminer - web UI for all databases ===
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
# Bring up dev databases
docker compose -f docker-compose.dev.yml up -d

# Bring up only PostgreSQL
docker compose -f docker-compose.dev.yml up -d postgres

# Stop
docker compose -f docker-compose.dev.yml down

# Remove data (full reset)
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
      # Configuration (mount, do not bake into the image)
      - /etc/oes/oes.conf:/etc/oes/oes.conf:ro
      # Data (databases, uploaded files)
      - oes_data:/var/lib/oes
      # Logs
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
    # Do NOT publish the port externally - access only from the docker network

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
# Log in
echo $GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin

# Build and push the build image (rarely changes)
docker build -f Dockerfile.build \
  -t ghcr.io/org/oes-builder:wx3.3.2 \
  -t ghcr.io/org/oes-builder:latest \
  --build-arg WX_VERSION=3.3.2 .
docker push ghcr.io/org/oes-builder:wx3.3.2
docker push ghcr.io/org/oes-builder:latest

# Build and push the daemon image (per release)
VERSION=$(git describe --tags --abbrev=0)  # e.g.: v1.2.3
COMMIT=$(git rev-parse --short HEAD)

docker build -f Dockerfile.daemon \
  -t ghcr.io/org/oes-daemon:${VERSION} \
  -t ghcr.io/org/oes-daemon:${COMMIT} \
  -t ghcr.io/org/oes-daemon:latest .

docker push ghcr.io/org/oes-daemon:${VERSION}
docker push ghcr.io/org/oes-daemon:${COMMIT}
docker push ghcr.io/org/oes-daemon:latest

# On the server - update
docker pull ghcr.io/org/oes-daemon:latest
OES_VERSION=latest docker compose up -d oes-daemon
```

---

## GitHub Actions: CI build in a container

```yaml
# .github/workflows/build.yml
name: Build OES

on:
  push:
    branches: [master, develop]   # master = production, develop = integration
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

## Useful commands

```bash
# === Build ===
docker compose build                          # Build everything
docker compose build --no-cache oes-daemon    # No cache
docker compose build oes-daemon               # Daemon only

# === Run ===
docker compose up -d                          # Start in background
docker compose up -d --build                  # Rebuild and start

# === Status ===
docker compose ps                             # Container status
docker compose logs -f oes-daemon             # Logs in real time
docker compose logs --tail=100 oes-daemon     # Last 100 lines

# === Stop ===
docker compose down                           # Stop
docker compose down -v                        # Stop + remove volumes

# === Debugging ===
docker compose exec oes-daemon bash           # Shell into the container
docker compose exec postgres psql -U oes_user -d oes_db

# === Cleanup ===
docker system prune -f                        # Remove unused resources
docker image prune -a -f                      # Remove all unused images
docker volume prune -f                        # Remove unused volumes
```

---

## When Docker vs native install for OES

```
Docker is NOT suitable for:
  - Desktop mode (wxWidgets GUI - cannot be containerized without X11/Wayland)
  - Windows-native OES with Firebird embedded (complex integration)
  - OES installer (NSIS/WiX - only native on Windows)

Docker IS suitable for:
  - C++ build environment (reproducible builds in CI/CD)
  - OES Daemon (headless, Linux, server mode)
  - Dev environment (PostgreSQL, Firebird Server, MySQL for developers)
  - Vendor License Server and Update Server

Recommendation for OES:
  - Build container in CI/CD (GitHub Actions) - ALWAYS
  - Dev databases via docker-compose.dev.yml - ALWAYS
  - OES Daemon in production - optional (Docker or systemd are both fine)
  - Desktop client - native installation (NSIS/WiX installer)
```
