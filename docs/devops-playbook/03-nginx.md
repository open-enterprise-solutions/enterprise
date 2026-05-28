# 03. Nginx — reverse proxy for OES Daemon

> Nginx is used as a reverse proxy in OES server (daemon) mode.
> In desktop mode (regular install), Nginx is not needed.

---

## When Nginx is needed in the context of OES

```
Desktop mode (regular):
  - OES runs as a GUI application directly
  - Nginx is NOT needed

Daemon / Service mode:
  - OES runs as a headless service (Windows Service or Linux systemd)
  - Provides an HTTP API for thin clients / web interface
  - Nginx is needed as:
    * TLS termination (HTTPS in front of OES daemon)
    * Reverse proxy (request forwarding)
    * Rate limiting (API protection)
    * Static files (if there is a web client for OES)

Vendor update / license servers:
  - Dedicated servers for license validation and update distribution
  - An Nginx frontend is mandatory
```

---

## Base structure

```bash
# Main config
/etc/nginx/nginx.conf

# Sites
/etc/nginx/sites-available/   # All configs
/etc/nginx/sites-enabled/     # Active (symlinks)

# Snippets (reusable blocks)
/etc/nginx/snippets/

# Logs
/var/log/nginx/access.log
/var/log/nginx/error.log
```

## Main config nginx.conf

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
    # === Base settings ===
    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;
    keepalive_timeout 65;
    types_hash_max_size 2048;
    server_tokens off;             # Do not expose the nginx version
    client_max_body_size 100M;     # Upload size (OES can transfer large files/reports)

    include /etc/nginx/mime.types;
    default_type application/octet-stream;

    # === Logging ===
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

    # === Upstream: License Server (if on the same host) ===
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

## Snippets

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

## Config: OES Daemon (main)

```bash
sudo nano /etc/nginx/sites-available/oes-daemon
```

```nginx
# HTTP -> HTTPS redirect
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

    # === Main OES Daemon API ===
    location /api/ {
        limit_req zone=oes_api burst=40 nodelay;
        limit_conn addr 20;

        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;

        # OES transfers binary data (reports, DB files)
        proxy_buffering on;
        proxy_buffer_size 16k;
        proxy_buffers 8 64k;
        proxy_busy_buffers_size 128k;
    }

    # === File uploads (data import, templates) ===
    location /api/import {
        client_max_body_size 512M;
        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;
        proxy_read_timeout 600s;
    }

    # === Report downloads ===
    location /api/export {
        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;
        proxy_read_timeout 600s;
        # Large reports - do not buffer
        proxy_buffering off;
    }

    # === Health check (no rate limit) ===
    location /health {
        proxy_pass http://oes_daemon;
        include snippets/proxy-params.conf;
        access_log off;
    }

    # === OES web-client static (if present) ===
    location /static/ {
        alias /opt/oes/web-static/;
        expires 30d;
        add_header Cache-Control "public, no-transform";
        access_log off;
    }

    # === Deny access to dotfiles ===
    location ~ /\. {
        deny all;
        access_log off;
        log_not_found off;
    }
}
```

```bash
# Activate
sudo ln -s /etc/nginx/sites-available/oes-daemon /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t
sudo systemctl reload nginx
```

---

## Config: License Server

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

    # Strict rate limit - protect against license key brute force
    location /api/v1/validate {
        limit_req zone=oes_license burst=10 nodelay;
        limit_conn addr 5;

        proxy_pass http://oes_license_server;
        include snippets/proxy-params.conf;

        # Log every request to the license server
        access_log /var/log/nginx/license-access.log main;
    }

    location /api/v1/activate {
        limit_req zone=oes_license burst=3 nodelay;

        proxy_pass http://oes_license_server;
        include snippets/proxy-params.conf;

        access_log /var/log/nginx/license-access.log main;
    }

    # Health check for monitoring
    location /health {
        proxy_pass http://oes_license_server;
        access_log off;
    }
}
```

---

## Config: Update Server

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

    # === Check for updates (JSON manifest) ===
    location /api/v1/check {
        limit_req zone=oes_update burst=5 nodelay;
        proxy_pass http://oes_update_server;
        include snippets/proxy-params.conf;
    }

    # === Distribution downloads (files may exceed 500MB) ===
    location /releases/ {
        # Serve files directly from a directory (or proxy to a CDN)
        alias /opt/oes-releases/;
        autoindex off;

        # Cache on the client side (releases are immutable)
        expires 7d;
        add_header Cache-Control "public, immutable";

        # Throttle download speed per client (overload protection)
        limit_rate 10m;          # 10 MB/s per connection
        limit_rate_after 100m;   # Start limiting after 100MB

        access_log /var/log/nginx/downloads-access.log main;
    }

    # Or - redirect to a CDN (Cloudflare R2, S3):
    # location /releases/ {
    #     return 302 https://cdn.oes-vendor.com$request_uri;
    # }
}
```

---

## Long-polling / WebSocket for OES Daemon API

```nginx
# If OES daemon uses WebSocket for realtime notifications
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

# If OES daemon uses SSE for push notifications
location /api/events/ {
    proxy_pass http://oes_daemon;
    proxy_http_version 1.1;
    proxy_set_header Connection '';
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;

    # Critical for SSE:
    proxy_buffering off;
    proxy_cache off;
    chunked_transfer_encoding on;
    proxy_read_timeout 86400s;
    gzip off;
}
```

---

## Load balancing across multiple OES Daemon instances

```nginx
# Several daemon instances on different ports
upstream oes_daemon {
    least_conn;
    server 127.0.0.1:8765 max_fails=3 fail_timeout=30s;
    server 127.0.0.1:8766 max_fails=3 fail_timeout=30s;
    server 127.0.0.1:8767 backup;    # Backup
    keepalive 64;
}
```

---

## Restricting access to trusted networks only

```nginx
# For an internal OES daemon - allow only from the corporate network
location /api/ {
    # Allow only from corporate VPN / office IPs
    allow 10.0.0.0/8;
    allow 192.168.0.0/16;
    deny all;

    proxy_pass http://oes_daemon;
    include snippets/proxy-params.conf;
}
```

---

## Useful commands

```bash
# Validate the config
sudo nginx -t

# Reload (no downtime)
sudo systemctl reload nginx

# View logs
sudo tail -f /var/log/nginx/access.log
sudo tail -f /var/log/nginx/error.log

# License server logs
sudo tail -f /var/log/nginx/license-access.log

# Current connections
sudo ss -tlnp | grep nginx

# Status
sudo systemctl status nginx

# Enable a site
sudo ln -s /etc/nginx/sites-available/oes-daemon /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx

# Disable a site
sudo rm /etc/nginx/sites-enabled/oes-daemon
sudo nginx -t && sudo systemctl reload nginx
```

---

## Certbot (if not using Cloudflare)

```bash
# Install
sudo apt install -y certbot python3-certbot-nginx

# Obtain certificates
sudo certbot --nginx \
  -d oes.example.com \
  -d license.oes-vendor.com \
  -d updates.oes-vendor.com

# Auto-renewal (already configured via a systemd timer)
sudo certbot renew --dry-run
```

---

## Optionality of Nginx

If the OES daemon runs only on a local network or serves client applications directly without a web interface, Nginx is not required. In that case:

- TLS termination must be implemented in the daemon itself (via OpenSSL/Boost.Asio or a built-in HTTP server with TLS)
- Or the OES daemon runs behind a corporate proxy/VPN with no external access
- Rate limiting can be implemented at the application level

```
Scenarios WITHOUT Nginx:
  - Desktop-only installation (GUI, no daemon)
  - Daemon only on a local network behind a corporate firewall
  - Daemon with built-in TLS and direct client connections

Scenarios WITH Nginx:
  - Daemon with a public HTTPS API
  - Multiple daemon instances (load balancing)
  - Vendor license / update servers
  - OES web client
```
