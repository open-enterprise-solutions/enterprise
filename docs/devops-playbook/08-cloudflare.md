# 08. Cloudflare — CDN и защита для инфраструктуры OES

> Cloudflare в контексте OES используется для:
> - **Update Server** — CDN для дистрибуции релизов и обновлений OES
> - **License Server** — защита endpoint'ов валидации лицензий
> - **Портал вендора** — документация, marketing сайт
> - **OES Daemon** — если публичный доступ к серверному режиму нужен

---

## DNS настройка (инфраструктура OES-вендора)

### Добавить домен

```
1. Cloudflare Dashboard → Add a Site → ввести oes-vendor.com
2. Выбрать план (Free достаточно для большинства)
3. Cloudflare покажет NS-серверы — сменить у регистратора
4. Подождать 5-60 минут
```

### A-записи для инфраструктуры OES

```
Тип    Имя                        Значение          Proxy   TTL
A      oes-vendor.com             203.0.113.10      Proxied Auto
A      updates.oes-vendor.com     203.0.113.10      Proxied Auto    ← Update Server
A      license.oes-vendor.com     203.0.113.10      Proxied Auto    ← License Server
A      portal.oes-vendor.com      203.0.113.11      Proxied Auto    ← Вендорный портал
A      daemon.customer.com        203.0.113.20      Proxied Auto    ← OES Daemon (если публичный)
CNAME  www                        oes-vendor.com    Proxied Auto
```

### Proxied vs DNS Only для OES

```
Proxied (оранжевое облако) — ИСПОЛЬЗОВАТЬ для:
  — updates.oes-vendor.com (CDN, кэш больших файлов)
  — license.oes-vendor.com (DDoS защита, rate limiting)
  — portal.oes-vendor.com (сайт вендора)

DNS Only (серое облако) — ИСПОЛЬЗОВАТЬ для:
  — build-server.internal (SSH-доступ к build-серверу)
  — Внутренние серверы, которые не должны идти через Cloudflare
  — MX записи всегда DNS Only
```

---

## SSL/TLS

### Full (Strict) — рекомендуется

```
Cloudflare Dashboard → SSL/TLS → Overview → Full (Strict)
```

### Origin Certificate для каждого сервера

```bash
# Для Update Server
# SSL/TLS → Origin Server → Create Certificate
# Выбрать: *.oes-vendor.com, oes-vendor.com
# Срок: 15 лет

sudo mkdir -p /etc/ssl/cloudflare
sudo nano /etc/ssl/cloudflare/cert.pem    # Вставить Origin Certificate
sudo nano /etc/ssl/cloudflare/key.pem     # Вставить Private Key
sudo chmod 600 /etc/ssl/cloudflare/key.pem
sudo chmod 644 /etc/ssl/cloudflare/cert.pem
```

### Настройки SSL

```
SSL/TLS → Edge Certificates:
  — Always Use HTTPS: ON
  — Minimum TLS Version: TLS 1.2
  — TLS 1.3: ON
  — Automatic HTTPS Rewrites: ON
  — HSTS: Enable (max-age: 6 months, includeSubDomains)
```

---

## Firewall Rules (WAF) для OES

### Защита License Server

```
Security → WAF → Custom Rules → Create Rule

Имя: Block License Brute Force Countries
Выражение:
  (http.request.uri.path contains "/api/v1/license/" and
   ip.geoip.country in {"KP" "XX"})
Действие: Block

Имя: License API Allowed Methods
Выражение:
  (http.request.uri.path contains "/api/v1/license/" and
   not http.request.method in {"POST" "GET"})
Действие: Block

Имя: License API Bot Protection
Выражение:
  (http.request.uri.path contains "/api/v1/license/" and
   cf.client.bot)
Действие: Challenge
```

### Защита Update Server

```
Имя: Update Server Allowed Methods
Выражение:
  (http.host eq "updates.oes-vendor.com" and
   not http.request.method in {"GET" "HEAD"})
Действие: Block

Имя: Block Update Server Hotlinking
Выражение:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path matches "^/releases/.*\.(exe|msi|zip|tar\.gz)$" and
   not http.referer contains "oes-vendor.com" and
   not http.referer eq "")
Действие: Challenge
```

### Блокировка сканеров / ботов

```
Имя: Block Bad Bots
Выражение:
  (cf.client.bot) or
  (http.user_agent eq "") or
  (http.user_agent contains "sqlmap") or
  (http.user_agent contains "nikto")
Действие: Block
```

### Разрешить только Cloudflare к серверу

```bash
# UFW на Update Server / License Server — разрешить HTTP/HTTPS только от Cloudflare
for ip in $(curl -s https://www.cloudflare.com/ips-v4); do
  sudo ufw allow from $ip to any port 80
  sudo ufw allow from $ip to any port 443
done

# IPv6
for ip in $(curl -s https://www.cloudflare.com/ips-v6); do
  sudo ufw allow from $ip to any port 80
  sudo ufw allow from $ip to any port 443
done

# Удалить общие правила
sudo ufw delete allow 80/tcp
sudo ufw delete allow 443/tcp

sudo ufw reload
```

---

## Rate Limiting для OES

```
Security → WAF → Rate Limiting Rules → Create Rule

Имя: License Validation Rate Limit
Выражение:
  (http.request.uri.path eq "/api/v1/license/validate")
Характеристики: IP
Период: 1 minute
Лимит: 10 requests
Действие: Block (длительность: 300 секунд)
Комментарий: OES лицензии проверяются редко, 10/min достаточно

Имя: License Activation Rate Limit
Выражение:
  (http.request.uri.path eq "/api/v1/license/activate")
Характеристики: IP
Период: 1 hour
Лимит: 5 requests
Действие: Block (длительность: 3600 секунд)
Комментарий: Активаций должно быть мало

Имя: Update Check Rate Limit
Выражение:
  (http.request.uri.path eq "/api/v1/updates/check")
Характеристики: IP
Период: 1 hour
Лимит: 30 requests
Действие: Block (длительность: 300 секунд)

Имя: Release Download Rate Limit
Выражение:
  (http.request.uri.path matches "^/releases/.*\.(exe|msi|zip|tar\.gz)$")
Характеристики: IP
Период: 1 hour
Лимит: 5 downloads
Действие: Block (длительность: 3600 секунд)
```

---

## Caching для Update Server

### Cache Rules (рекомендуется)

```
Caching → Cache Rules → Create Rule

Имя: Cache OES Release Files
Выражение:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path matches "^/releases/.*\.(exe|msi|zip|tar\.gz|sig|sha256)$")
Действие:
  Cache Status: Eligible for cache
  Edge TTL: 30 days         ← Релизы не меняются
  Browser TTL: 7 days

Имя: Cache Update Manifest
Выражение:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path eq "/api/v1/updates/manifest.json")
Действие:
  Cache Status: Eligible for cache
  Edge TTL: 5 minutes       ← Манифест может меняться при выходе версии
  Browser TTL: 1 minute

Имя: Bypass Cache for License API
Выражение:
  (http.host eq "license.oes-vendor.com")
Действие:
  Cache Status: Bypass cache  ← Лицензионные запросы не кэшировать никогда
```

---

## Redirect Rules

### Редирект старых URL обновлений

```
Rules → Redirect Rules → Create Rule

Имя: Legacy Update URL redirect
Выражение:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path starts_with "/download/")
URL Redirect:
  Type: Dynamic
  Expression: concat("https://updates.oes-vendor.com/releases/", substring(http.request.uri.path, 10))
  Status Code: 301
```

---

## DDoS Protection

```
Бесплатно на всех планах:
  — L3/L4 DDoS Protection (автоматически)
  — L7 DDoS Protection (автоматически)

Security → Settings:
  — Security Level: Medium
  — Browser Integrity Check: ON
  — для License Server: Security Level: High

При атаке на License Server:
  1. Security → Settings → Under Attack Mode: ON
  2. Это добавит JS challenge — OES клиент должен уметь обрабатывать (или bypass)
  3. Или добавить правило WAF для разрешения запросов с OES User-Agent без challenge

Важно для OES: OES клиент (C++) делает HTTP запросы без браузера.
JS challenge Cloudflare не пройдёт C++ HTTP клиент (libcurl).
Использовать Rate Limiting вместо Under Attack Mode для API endpoint'ов.
```

---

## API Cloudflare (автоматизация)

### Настройка

```bash
# Создать API токен: Profile → API Tokens → Create Token
# Шаблон: Zone:DNS:Edit, Zone:Cache Purge:Purge, Zone:Zone:Read

ZONE_ID="ваш_zone_id"     # Dashboard → домен → Overview → Zone ID
CF_TOKEN="ваш_api_token"
```

### Purge Cache после выхода релиза OES

```bash
#!/bin/bash
# /opt/scripts/purge-update-cache.sh
# Запускать после публикации нового релиза OES

CF_ZONE_ID="${CF_ZONE_ID}"
CF_TOKEN="${CF_TOKEN}"
VERSION="${1:-latest}"

echo "Очистка кэша Cloudflare после релиза OES ${VERSION}..."

# Очистить манифест обновлений (чтобы клиенты увидели новый релиз)
RESULT=$(curl -s -X POST \
  "https://api.cloudflare.com/client/v4/zones/${CF_ZONE_ID}/purge_cache" \
  -H "Authorization: Bearer ${CF_TOKEN}" \
  -H "Content-Type: application/json" \
  --data '{
    "files": [
      "https://updates.oes-vendor.com/api/v1/updates/manifest.json",
      "https://updates.oes-vendor.com/api/v1/updates/latest.json"
    ]
  }')

SUCCESS=$(echo "$RESULT" | jq -r '.success')
if [ "$SUCCESS" = "true" ]; then
  echo "Кэш манифеста очищен — клиенты увидят релиз ${VERSION}"
else
  echo "ОШИБКА очистки кэша: $(echo $RESULT | jq -r '.errors')"
  exit 1
fi
```

```bash
# Интеграция в CI/CD (GitHub Actions)
# .github/workflows/release.yml
- name: Purge Cloudflare cache after release
  env:
    CF_ZONE_ID: ${{ secrets.CF_ZONE_ID }}
    CF_TOKEN: ${{ secrets.CF_TOKEN }}
  run: |
    chmod +x ./scripts/purge-update-cache.sh
    ./scripts/purge-update-cache.sh "${{ github.ref_name }}"
```

### Управление DNS через API

```bash
# Список DNS записей
curl -s "https://api.cloudflare.com/client/v4/zones/${ZONE_ID}/dns_records" \
  -H "Authorization: Bearer ${CF_TOKEN}" | jq '.result[] | {name, type, content}'

# Обновить IP update-сервера при смене хостинга
RECORD_ID="id_записи_updates"
NEW_IP="198.51.100.20"

curl -X PUT "https://api.cloudflare.com/client/v4/zones/${ZONE_ID}/dns_records/${RECORD_ID}" \
  -H "Authorization: Bearer ${CF_TOKEN}" \
  -H "Content-Type: application/json" \
  --data "{
    \"type\": \"A\",
    \"name\": \"updates.oes-vendor.com\",
    \"content\": \"${NEW_IP}\",
    \"ttl\": 1,
    \"proxied\": true
  }"
```

---

## Cloudflare Workers (опционально)

### Роутинг запросов по версии OES

```javascript
// workers/oes-updates-router.js
// Роутинг запросов к Update Server в зависимости от версии клиента

export default {
  async fetch(request) {
    const url = new URL(request.url);
    const clientVersion = request.headers.get('X-OES-Version') || '0.0.0';

    // Перенаправить устаревшие клиенты на legacy API
    const [major] = clientVersion.split('.').map(Number);
    if (major < 2) {
      url.pathname = '/legacy' + url.pathname;
      return fetch(new Request(url.toString(), request));
    }

    return fetch(request);
  },
};
```

```javascript
// workers/license-geo-check.js
// Блокировать активацию лицензий из запрещённых юрисдикций

export default {
  async fetch(request) {
    const country = request.cf?.country || 'XX';
    const BLOCKED_COUNTRIES = ['KP', 'XX'];  // Северная Корея, неизвестные

    if (request.url.includes('/api/v1/license/activate') &&
        BLOCKED_COUNTRIES.includes(country)) {
      return new Response(
        JSON.stringify({ error: 'License activation not available in your region' }),
        {
          status: 451,  // Unavailable For Legal Reasons
          headers: { 'Content-Type': 'application/json' }
        }
      );
    }

    return fetch(request);
  },
};
```

```bash
# Деплой Worker
# ПРИМЕЧАНИЕ: wrangler — инструмент Cloudflare на Node.js. Node.js не является частью
# стека OES (C++). Установить Node.js отдельно: https://nodejs.org/
npm install -g wrangler
wrangler login
wrangler deploy workers/oes-updates-router.js --name oes-updates-router

# Привязать к маршруту:
# Workers Routes → updates.oes-vendor.com/* → oes-updates-router
```

---

## Аналитика для OES вендора

```
Analytics → Traffic:
  — Количество запросов к /api/v1/updates/check (частота проверок обновлений)
  — Количество скачиваний из /releases/ (статистика загрузок)
  — Количество запросов к /api/v1/license/validate (активность лицензий)

Полезные метрики:
  — Unique IP на /releases/... → приблизительное количество уникальных установок
  — Ошибки 429 на License API → атаки или превышение лимитов
  — Топ стран источников → geography base пользователей OES

Security:
  — Threats Blocked → DDos атаки на License/Update серверы
  — Top Threat Countries
```

---

## Полезные настройки для OES инфраструктуры

```
Speed → Optimization:
  — Brotli: ON          ← сжатие ZIP с релизами (дополнительно к gzip)
  — Early Hints: ON
  — Rocket Loader: OFF  ← не нужен, нет web-приложения

Network:
  — HTTP/2: ON
  — HTTP/3 (QUIC): ON   ← OES клиент libcurl поддерживает HTTP/3

Caching → Configuration:
  — Caching Level: Standard (для update-сервера)
  — Browser Cache TTL: Respect Existing Headers
```

---

## Чеклист настройки Cloudflare для OES

```
[ ] Домен добавлен, NS делегированы
[ ] A-записи для updates.oes-vendor.com и license.oes-vendor.com
[ ] SSL/TLS: Full (Strict)
[ ] Origin Certificates созданы и установлены на серверах
[ ] Always Use HTTPS: ON
[ ] Minimum TLS: 1.2
[ ] WAF: защита License API (rate limit, bot protection)
[ ] WAF: защита Update Server (hotlink protection, method check)
[ ] Cache Rules: долгое кэширование релизов
[ ] Cache Rules: короткий TTL манифеста обновлений
[ ] Cache Rules: bypass для License API
[ ] API Token создан (Zone:Cache Purge + DNS:Edit)
[ ] Token сохранён в GitHub Secrets (CF_TOKEN, CF_ZONE_ID)
[ ] Скрипт purge-cache после деплоя релиза настроен
[ ] UFW на серверах: разрешить HTTP/HTTPS только от Cloudflare IP
[ ] Аналитика: настроить отслеживание скачиваний и активаций
```
