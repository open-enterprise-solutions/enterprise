# 03. Nginx — reverse proxy для OES Daemon

> Nginx используется как reverse proxy в серверном (daemon) режиме OES.
> В desktop-режиме (обычная установка) Nginx не нужен.

---

## Когда нужен Nginx в контексте OES

```
Desktop режим (обычный):
  — OES запускается как GUI-приложение напрямую
  — Nginx НЕ нужен

Daemon / Service режим:
  — OES запускается как headless-сервис (Windows Service или Linux systemd)
  — Предоставляет HTTP API для тонких клиентов / web-интерфейса
  — Nginx нужен как:
    * TLS termination (HTTPS перед OES daemon)
    * Reverse proxy (перенаправление запросов)
    * Rate limiting (защита API)
    * Статика (если есть web-клиент OES)

Update / License серверы вендора:
  — Выделенные серверы для проверки лицензий и раздачи обновлений
  — Nginx как фронтенд обязателен
```

---

## Базовая структура

```bash
# Основной конфиг
/etc/nginx/nginx.conf

# Сайты
/etc/nginx/sites-available/   # Все конфиги
/etc/nginx/sites-enabled/     # Активные (симлинки)

# Сниппеты (переиспользуемые блоки)
/etc/nginx/snippets/

# Логи
/var/log/nginx/access.log
/var/log/nginx/error.log
```

## Основной конфиг nginx.conf

```bash
sudo nano /etc/nginx/nginx.conf
```

```nginx
user www-data;
worker_processes auto;
pid /run/nginx.pid;
include /etc/nginx/modules-enabled/*.conf;

events {
    worker_connections 1024;
    multi_accept on;
}

http {
    # === Базовые настройки ===
    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;
    keepalive_timeout 65;
    types_hash_max_size 2048;
    server_tokens off;             # Не показывать версию nginx
    client_max_body_size 100M;     # Размер загрузки (OES может передавать большие файлы/отчёты)

    include /etc/nginx/mime.types;
    default_type application/octet-stream;

    # === Логирование ===
    log_format main '$remote_addr - $remote_user [$time_local] '
                    '"$request" $status $body_bytes_sent '
                    '"$http_referer" "$http_user_agent" '
                    '$request_time';

    access_log /var/log/nginx/access.log main;
    error_log /var/log/nginx/error.log warn;

    # === Gzip ===
    gzip on;
    gzip_vary on;
    gzip_proxied any;
    gzip_comp_level 6;
    gzip_min_length 1000;
    gzip_types
        text/plain
        text/css
        application/json
        application/javascript
        application/xml
        application/octet-stream;

    # === Rate Limiting ===
    limit_req_zone $binary_remote_addr zone=oes_api:10m rate=20r/s;
    limit_req_zone $binary_remote_addr zone=oes_license:10m rate=5r/s;
    limit_req_zone $binary_remote_addr zone=oes_update:10m rate=3r/s;
    limit_conn_zone $binary_remote_addr zone=addr:10m;

    # === Upstream: OES Daemon ===
    upstream oes_daemon {
        server 127.0.0.1:8765;
        keepalive 32;
    }

    # === Upstream: License Server (если на том же хосте) ===
    upstream oes_license_server {
        server 127.0.0.1:8766;
    }

    # === Upstream: Update Server ===
    upstream oes_update_server {
        server 127.0.0.1:8767;
    }

    include /etc/nginx/conf.d/*.conf;
    include /etc/nginx/sites-enabled/*;
}
```

---

## Сниппеты

### Security Headers

```bash
sudo nano /etc/nginx/snippets/security-headers.conf
```

```nginx
# /etc/nginx/snippets/security-headers.conf
add_header X-Frame-Options "SAMEORIGIN" always;
add_header X-Content-Type-Options "nosniff" always;
add_header X-XSS-Protection "1; mode=block" always;
add_header Referrer-Policy "strict-origin-when-cross-origin" always;
add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;
```

### Proxy Params

```bash
sudo nano /etc/nginx/snippets/proxy-params.conf
```

```nginx
# /etc/nginx/snippets/proxy-params.conf
proxy_http_version 1.1;
proxy_set_header Connection "";
proxy_set_header Host $host;
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
proxy_set_header X-Forwarded-Proto $scheme;
proxy_read_timeout 300s;
proxy_connect_timeout 30s;
proxy_send_timeout 300s;
```

### SSL (Cloudflare Origin)

```bash
sudo nano /etc/nginx/snippets/ssl-cloudflare.conf
```

```nginx
# /etc/nginx/snippets/ssl-cloudflare.conf
ssl_certificate /etc/ssl/cloudflare/cert.pem;
ssl_certificate_key /etc/ssl/cloudflare/key.pem;
ssl_protocols TLSv1.2 TLSv1.3;
ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384;
ssl_prefer_server_ciphers off;
ssl_session_cache shared:SSL:10m;
ssl_session_timeout 1d;
ssl_session_tickets off;
```

---

## Конфиг: OES Daemon (основной)

```bash
sudo nano /etc/nginx/sites-available/oes-daemon
```

```nginx
# HTTP → HTTPS редирект
server {
    listen 80;
    server_name oes.example.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name oes.example.com;

    include snippets/ssl-cloudflare.conf;
    include snippets/security-headers.conf;

    # === Основной API OES Daemon ===
    location /api/ {
        limit_req zone=oes_api burst=40 nodelay;
        limit_conn addr 20;

        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;

        # OES передаёт бинарные данные (отчёты, файлы БД)
        proxy_buffering on;
        proxy_buffer_size 16k;
        proxy_buffers 8 64k;
        proxy_busy_buffers_size 128k;
    }

    # === Загрузка файлов (импорт данных, шаблонов) ===
    location /api/import {
        client_max_body_size 512M;
        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;
        proxy_read_timeout 600s;
    }

    # === Выгрузка отчётов ===
    location /api/export {
        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;
        proxy_read_timeout 600s;
        # Большие отчёты — не буферизовать
        proxy_buffering off;
    }

    # === Health check (без rate limit) ===
    location /health {
        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;
        access_log off;
    }

    # === Статика web-клиента OES (если есть) ===
    location /static/ {
        alias /opt/oes/web-static/;
        expires 30d;
        add_header Cache-Control "public, no-transform";
        access_log off;
    }

    # === Закрыть доступ к системным путям ===
    location ~ /\. {
        deny all;
        access_log off;
        log_not_found off;
    }
}
```

```bash
# Активировать
sudo ln -s /etc/nginx/sites-available/oes-daemon /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t
sudo systemctl reload nginx
```

---

## Конфиг: License Server

```bash
sudo nano /etc/nginx/sites-available/oes-license
```

```nginx
server {
    listen 80;
    server_name license.oes-vendor.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name license.oes-vendor.com;

    include snippets/ssl-cloudflare.conf;
    include snippets/security-headers.conf;

    # Строгий rate limit — защита от брутфорса лицензионных ключей
    location /api/v1/validate {
        limit_req zone=oes_license burst=10 nodelay;
        limit_conn addr 5;

        proxy_pass http://oes_license_server;
        include snippets/proxy-params.conf;

        # Логировать все запросы к license server
        access_log /var/log/nginx/license-access.log main;
    }

    location /api/v1/activate {
        limit_req zone=oes_license burst=3 nodelay;

        proxy_pass http://oes_license_server;
        include snippets/proxy-params.conf;

        access_log /var/log/nginx/license-access.log main;
    }

    # Health check для мониторинга
    location /health {
        proxy_pass http://oes_license_server;
        access_log off;
    }
}
```

---

## Конфиг: Update Server

```bash
sudo nano /etc/nginx/sites-available/oes-updates
```

```nginx
server {
    listen 80;
    server_name updates.oes-vendor.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name updates.oes-vendor.com;

    include snippets/ssl-cloudflare.conf;
    include snippets/security-headers.conf;

    # === Проверка наличия обновлений (JSON манифест) ===
    location /api/v1/check {
        limit_req zone=oes_update burst=5 nodelay;
        proxy_pass http://oes_update_server;
        include snippets/proxy-params.conf;
    }

    # === Скачивание дистрибутива (файлы могут быть > 500MB) ===
    location /releases/ {
        # Отдавать файлы напрямую из директории (или проксировать на CDN)
        alias /opt/oes-releases/;
        autoindex off;

        # Кэшировать на стороне клиента (релизы неизменны)
        expires 7d;
        add_header Cache-Control "public, immutable";

        # Ограничить скорость отдачи на клиента (защита от перегрузки)
        limit_rate 10m;          # 10 MB/s на соединение
        limit_rate_after 100m;   # Начать лимит после 100MB

        access_log /var/log/nginx/downloads-access.log main;
    }

    # Или — редирект на CDN (Cloudflare R2, S3):
    # location /releases/ {
    #     return 302 https://cdn.oes-vendor.com$request_uri;
    # }
}
```

---

## Long-polling / WebSocket для OES Daemon API

```nginx
# Если OES daemon использует WebSocket для realtime-уведомлений
location /api/ws/ {
    proxy_pass http://oes_daemon;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_read_timeout 86400s;
    proxy_send_timeout 86400s;
}

# Если OES daemon использует SSE для push-уведомлений
location /api/events/ {
    proxy_pass http://oes_daemon;
    proxy_http_version 1.1;
    proxy_set_header Connection '';
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;

    # Критично для SSE:
    proxy_buffering off;
    proxy_cache off;
    chunked_transfer_encoding on;
    proxy_read_timeout 86400s;
    gzip off;
}
```

---

## Балансировка нескольких инстансов OES Daemon

```nginx
# Несколько инстансов daemon на разных портах
upstream oes_daemon {
    least_conn;
    server 127.0.0.1:8765 max_fails=3 fail_timeout=30s;
    server 127.0.0.1:8766 max_fails=3 fail_timeout=30s;
    server 127.0.0.1:8767 backup;    # Резерв
    keepalive 64;
}
```

---

## Ограничение доступа только из доверенных сетей

```nginx
# Для внутреннего OES daemon — разрешить только из корпоративной сети
location /api/ {
    # Разрешить только с корпоративного VPN / офисных IP
    allow 10.0.0.0/8;
    allow 192.168.0.0/16;
    deny all;

    proxy_pass http://oes_daemon;
    include snippets/proxy-params.conf;
}
```

---

## Полезные команды

```bash
# Проверить конфиг
sudo nginx -t

# Перезагрузить (без даунтайма)
sudo systemctl reload nginx

# Посмотреть логи
sudo tail -f /var/log/nginx/access.log
sudo tail -f /var/log/nginx/error.log

# Логи license-сервера
sudo tail -f /var/log/nginx/license-access.log

# Текущие подключения
sudo ss -tlnp | grep nginx

# Статус
sudo systemctl status nginx

# Активировать сайт
sudo ln -s /etc/nginx/sites-available/oes-daemon /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx

# Деактивировать сайт
sudo rm /etc/nginx/sites-enabled/oes-daemon
sudo nginx -t && sudo systemctl reload nginx
```

---

## Certbot (если не используете Cloudflare)

```bash
# Установить
sudo apt install -y certbot python3-certbot-nginx

# Получить сертификаты
sudo certbot --nginx \
  -d oes.example.com \
  -d license.oes-vendor.com \
  -d updates.oes-vendor.com

# Автообновление (уже настроено через systemd timer)
sudo certbot renew --dry-run
```

---

## Опциональность Nginx

Если OES daemon работает только в локальной сети или напрямую обслуживает клиентские приложения без web-интерфейса, Nginx не обязателен. В этом случае:

- TLS termination нужно реализовать в самом daemon (через OpenSSL/Boost.Asio или встроенный HTTP-сервер с TLS)
- Или OES daemon работает за корпоративным прокси/VPN без внешнего доступа
- Rate limiting можно реализовать на уровне приложения

```
Сценарии БЕЗ Nginx:
  — Desktop-только инсталляция (GUI, без daemon)
  — Daemon только в локальной сети за корпоративным файрволом
  — Daemon с встроенным TLS и прямым подключением клиентов

Сценарии С Nginx:
  — Daemon с публичным HTTPS API
  — Несколько инстансов daemon (балансировка)
  — License / Update серверы вендора
  — Веб-клиент OES
```
