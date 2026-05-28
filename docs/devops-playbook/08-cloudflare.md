# 08. Cloudflare — CDN and protection for OES infrastructure

> Cloudflare in the OES context is used for:
> - **Update Server** — CDN for distributing OES releases and updates
> - **License Server** — protecting license validation endpoints
> - **Vendor portal** — documentation, marketing site
> - **OES Daemon** — when public access to server mode is required

---

## DNS setup (OES vendor infrastructure)

### Add the domain

```
1. Cloudflare Dashboard -> Add a Site -> enter oes-vendor.com
2. Choose a plan (Free is enough for most cases)
3. Cloudflare shows NS servers - change them at the registrar
4. Wait 5-60 minutes
```

### A-records for OES infrastructure

```
Type   Name                       Value             Proxy   TTL
A      oes-vendor.com             203.0.113.10      Proxied Auto
A      updates.oes-vendor.com     203.0.113.10      Proxied Auto    <- Update Server
A      license.oes-vendor.com     203.0.113.10      Proxied Auto    <- License Server
A      portal.oes-vendor.com      203.0.113.11      Proxied Auto    <- Vendor portal
A      daemon.customer.com        203.0.113.20      Proxied Auto    <- OES Daemon (if public)
CNAME  www                        oes-vendor.com    Proxied Auto
```

### Proxied vs DNS Only for OES

```
Proxied (orange cloud) - USE for:
  - updates.oes-vendor.com (CDN, caching large files)
  - license.oes-vendor.com (DDoS protection, rate limiting)
  - portal.oes-vendor.com (vendor site)

DNS Only (gray cloud) - USE for:
  - build-server.internal (SSH access to the build server)
  - Internal servers that should not go through Cloudflare
  - MX records always DNS Only
```

---

## SSL/TLS

### Full (Strict) — recommended

```
Cloudflare Dashboard -> SSL/TLS -> Overview -> Full (Strict)
```

### Origin Certificate for each server

```bash
# For Update Server
# SSL/TLS -> Origin Server -> Create Certificate
# Select: *.oes-vendor.com, oes-vendor.com
# Validity: 15 years

sudo mkdir -p /etc/ssl/cloudflare
sudo nano /etc/ssl/cloudflare/cert.pem    # Paste Origin Certificate
sudo nano /etc/ssl/cloudflare/key.pem     # Paste Private Key
sudo chmod 600 /etc/ssl/cloudflare/key.pem
sudo chmod 644 /etc/ssl/cloudflare/cert.pem
```

### SSL settings

```
SSL/TLS -> Edge Certificates:
  - Always Use HTTPS: ON
  - Minimum TLS Version: TLS 1.2
  - TLS 1.3: ON
  - Automatic HTTPS Rewrites: ON
  - HSTS: Enable (max-age: 6 months, includeSubDomains)
```

---

## Firewall Rules (WAF) for OES

### Protecting the License Server

```
Security -> WAF -> Custom Rules -> Create Rule

Name: Block License Brute Force Countries
Expression:
  (http.request.uri.path contains "/api/v1/license/" and
   ip.geoip.country in {"KP" "XX"})
Action: Block

Name: License API Allowed Methods
Expression:
  (http.request.uri.path contains "/api/v1/license/" and
   not http.request.method in {"POST" "GET"})
Action: Block

Name: License API Bot Protection
Expression:
  (http.request.uri.path contains "/api/v1/license/" and
   cf.client.bot)
Action: Challenge
```

### Protecting the Update Server

```
Name: Update Server Allowed Methods
Expression:
  (http.host eq "updates.oes-vendor.com" and
   not http.request.method in {"GET" "HEAD"})
Action: Block

Name: Block Update Server Hotlinking
Expression:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path matches "^/releases/.*\.(exe|msi|zip|tar\.gz)$" and
   not http.referer contains "oes-vendor.com" and
   not http.referer eq "")
Action: Challenge
```

### Blocking scanners / bots

```
Name: Block Bad Bots
Expression:
  (cf.client.bot) or
  (http.user_agent eq "") or
  (http.user_agent contains "sqlmap") or
  (http.user_agent contains "nikto")
Action: Block
```

### Allow only Cloudflare to reach the server

```bash
# UFW on Update Server / License Server - allow HTTP/HTTPS from Cloudflare only
for ip in $(curl -s https://www.cloudflare.com/ips-v4); do
  sudo ufw allow from $ip to any port 80
  sudo ufw allow from $ip to any port 443
done

# IPv6
for ip in $(curl -s https://www.cloudflare.com/ips-v6); do
  sudo ufw allow from $ip to any port 80
  sudo ufw allow from $ip to any port 443
done

# Remove general rules
sudo ufw delete allow 80/tcp
sudo ufw delete allow 443/tcp

sudo ufw reload
```

---

## Rate Limiting for OES

```
Security -> WAF -> Rate Limiting Rules -> Create Rule

Name: License Validation Rate Limit
Expression:
  (http.request.uri.path eq "/api/v1/license/validate")
Characteristics: IP
Period: 1 minute
Limit: 10 requests
Action: Block (duration: 300 seconds)
Comment: OES licenses are validated rarely; 10/min is enough

Name: License Activation Rate Limit
Expression:
  (http.request.uri.path eq "/api/v1/license/activate")
Characteristics: IP
Period: 1 hour
Limit: 5 requests
Action: Block (duration: 3600 seconds)
Comment: Activations should be infrequent

Name: Update Check Rate Limit
Expression:
  (http.request.uri.path eq "/api/v1/updates/check")
Characteristics: IP
Period: 1 hour
Limit: 30 requests
Action: Block (duration: 300 seconds)

Name: Release Download Rate Limit
Expression:
  (http.request.uri.path matches "^/releases/.*\.(exe|msi|zip|tar\.gz)$")
Characteristics: IP
Period: 1 hour
Limit: 5 downloads
Action: Block (duration: 3600 seconds)
```

---

## Caching for Update Server

### Cache Rules (recommended)

```
Caching -> Cache Rules -> Create Rule

Name: Cache OES Release Files
Expression:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path matches "^/releases/.*\.(exe|msi|zip|tar\.gz|sig|sha256)$")
Action:
  Cache Status: Eligible for cache
  Edge TTL: 30 days         <- Releases do not change
  Browser TTL: 7 days

Name: Cache Update Manifest
Expression:
  (http.host eq "updates.oes-vendor.com" and
   http.request.uri.path eq "/api/v1/updates/manifest.json")
Action:
  Cache Status: Eligible for cache
  Edge TTL: 5 minutes       <- Manifest may change with new version releases
  Browser TTL: 1 minute

Name: Bypass Cache for License API
Expression:
  (http.host eq "license.oes-vendor.com")
Action:
  Cache Status: Bypass cache  <- Never cache license requests
```

---

## Redirect Rules

### Redirect old update URLs

```
Rules -> Redirect Rules -> Create Rule

Name: Legacy Update URL redirect
Expression:
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
Free on all plans:
  - L3/L4 DDoS Protection (automatic)
  - L7 DDoS Protection (automatic)

Security -> Settings:
  - Security Level: Medium
  - Browser Integrity Check: ON
  - For License Server: Security Level: High

During an attack on the License Server:
  1. Security -> Settings -> Under Attack Mode: ON
  2. This adds a JS challenge - the OES client must handle it (or bypass)
  3. Or add a WAF rule allowing requests with an OES User-Agent without a challenge

Important for OES: the OES client (C++) makes HTTP requests without a browser.
A C++ HTTP client (libcurl) will not pass the Cloudflare JS challenge.
Use Rate Limiting instead of Under Attack Mode for API endpoints.
```

---

## Cloudflare API (automation)

### Setup

```bash
# Create an API token: Profile -> API Tokens -> Create Token
# Template: Zone:DNS:Edit, Zone:Cache Purge:Purge, Zone:Zone:Read

ZONE_ID="your_zone_id"     # Dashboard -> domain -> Overview -> Zone ID
CF_TOKEN="your_api_token"
```

### Purge cache after an OES release

```bash
#!/bin/bash
# /opt/scripts/purge-update-cache.sh
# Run after publishing a new OES release

CF_ZONE_ID="${CF_ZONE_ID}"
CF_TOKEN="${CF_TOKEN}"
VERSION="${1:-latest}"

echo "Purging Cloudflare cache after OES release ${VERSION}..."

# Purge the update manifest (so clients see the new release)
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
  echo "Manifest cache purged - clients will see release ${VERSION}"
else
  echo "ERROR purging cache: $(echo $RESULT | jq -r '.errors')"
  exit 1
fi
```

```bash
# CI/CD integration (GitHub Actions)
# .github/workflows/release.yml
- name: Purge Cloudflare cache after release
  env:
    CF_ZONE_ID: ${{ secrets.CF_ZONE_ID }}
    CF_TOKEN: ${{ secrets.CF_TOKEN }}
  run: |
    chmod +x ./scripts/purge-update-cache.sh
    ./scripts/purge-update-cache.sh "${{ github.ref_name }}"
```

### Managing DNS via API

```bash
# List DNS records
curl -s "https://api.cloudflare.com/client/v4/zones/${ZONE_ID}/dns_records" \
  -H "Authorization: Bearer ${CF_TOKEN}" | jq '.result[] | {name, type, content}'

# Update the update-server IP when hosting changes
RECORD_ID="updates_record_id"
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

## Cloudflare Workers (optional)

### Routing requests by OES version

```javascript
// workers/oes-updates-router.js
// Route Update Server requests depending on the client version

export default {
  async fetch(request) {
    const url = new URL(request.url);
    const clientVersion = request.headers.get('X-OES-Version') || '0.0.0';

    // Redirect outdated clients to the legacy API
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
// Block license activations from prohibited jurisdictions

export default {
  async fetch(request) {
    const country = request.cf?.country || 'XX';
    const BLOCKED_COUNTRIES = ['KP', 'XX'];  // North Korea, unknown

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
# Deploy a Worker
# NOTE: wrangler is a Cloudflare tool that runs on Node.js. Node.js is not part of
# the OES stack (C++). Install Node.js separately: https://nodejs.org/
npm install -g wrangler
wrangler login
wrangler deploy workers/oes-updates-router.js --name oes-updates-router

# Bind to a route:
# Workers Routes -> updates.oes-vendor.com/* -> oes-updates-router
```

---

## Analytics for the OES vendor

```
Analytics -> Traffic:
  - Number of requests to /api/v1/updates/check (update-check frequency)
  - Number of downloads from /releases/ (download statistics)
  - Number of requests to /api/v1/license/validate (license activity)

Useful metrics:
  - Unique IPs on /releases/... -> approximate count of unique installations
  - 429 errors on the License API -> attacks or rate-limit overruns
  - Top source countries -> geographic base of OES users

Security:
  - Threats Blocked -> DDoS attacks against License/Update servers
  - Top Threat Countries
```

---

## Useful settings for OES infrastructure

```
Speed -> Optimization:
  - Brotli: ON          <- compress ZIPs with releases (additional to gzip)
  - Early Hints: ON
  - Rocket Loader: OFF  <- not needed, no web application

Network:
  - HTTP/2: ON
  - HTTP/3 (QUIC): ON   <- OES client (libcurl) supports HTTP/3

Caching -> Configuration:
  - Caching Level: Standard (for the update server)
  - Browser Cache TTL: Respect Existing Headers
```

---

## Cloudflare setup checklist for OES

```
[ ] Domain added, NS delegated
[ ] A-records for updates.oes-vendor.com and license.oes-vendor.com
[ ] SSL/TLS: Full (Strict)
[ ] Origin Certificates created and installed on servers
[ ] Always Use HTTPS: ON
[ ] Minimum TLS: 1.2
[ ] WAF: License API protection (rate limit, bot protection)
[ ] WAF: Update Server protection (hotlink protection, method check)
[ ] Cache Rules: long cache TTL for releases
[ ] Cache Rules: short TTL for update manifest
[ ] Cache Rules: bypass for License API
[ ] API Token created (Zone:Cache Purge + DNS:Edit)
[ ] Token saved in GitHub Secrets (CF_TOKEN, CF_ZONE_ID)
[ ] purge-cache script after release deploy configured
[ ] UFW on servers: allow HTTP/HTTPS only from Cloudflare IPs
[ ] Analytics: track downloads and activations
```
