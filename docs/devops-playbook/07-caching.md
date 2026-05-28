# 07. Caching in OES

> OES is a desktop/server C++ application. Redis is optional and only relevant in daemon/server mode.
> In desktop mode, built-in in-process caching is used without external dependencies.

---

## Overview: caching strategies per mode

```
Desktop mode (GUI, wxWidgets):
  - Cache lives in process memory (std::unordered_map, LRU)
  - Disk cache in the application directory
  - Redis is NOT needed

Daemon / Server mode (headless, many clients):
  - Built-in in-process cache (first tier)
  - Redis - optional external cache (second tier, if needed)
  - Firebird/PostgreSQL with prepared statements and a connection pool
```

---

## In-process caching (C++)

### LRU cache in memory

```cpp
// include/cache/LRUCache.h - example implementation in OES
#pragma once
#include <list>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <chrono>

template<typename Key, typename Value>
class LRUCache {
public:
    struct Entry {
        Value value;
        std::chrono::steady_clock::time_point expires;
    };

    explicit LRUCache(size_t capacity, std::chrono::seconds ttl = std::chrono::seconds(300))
        : capacity_(capacity), ttl_(ttl) {}

    // Get value from cache
    std::optional<Value> get(const Key& key) {
        std::lock_guard lock(mutex_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) return std::nullopt;

        // Check TTL
        if (std::chrono::steady_clock::now() > it->second->expires) {
            lru_list_.erase(it->second);
            cache_map_.erase(it);
            return std::nullopt;
        }

        // Move to front (LRU)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return it->second->value;
    }

    // Put value into cache
    void put(const Key& key, Value value) {
        std::lock_guard lock(mutex_);
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            lru_list_.erase(it->second);
            cache_map_.erase(it);
        }
        if (cache_map_.size() >= capacity_) {
            // Evict the least recently used
            cache_map_.erase(lru_list_.back().first);
            lru_list_.pop_back();
        }
        auto expires = std::chrono::steady_clock::now() + ttl_;
        lru_list_.emplace_front(key, Entry{std::move(value), expires});
        cache_map_[key] = lru_list_.begin();
    }

    void invalidate(const Key& key) {
        std::lock_guard lock(mutex_);
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            lru_list_.erase(it->second);
            cache_map_.erase(it);
        }
    }

    void clear() {
        std::lock_guard lock(mutex_);
        lru_list_.clear();
        cache_map_.clear();
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return cache_map_.size();
    }

private:
    using ListEntry = std::pair<Key, Entry>;
    size_t capacity_;
    std::chrono::seconds ttl_;
    mutable std::mutex mutex_;
    std::list<ListEntry> lru_list_;
    std::unordered_map<Key, typename std::list<ListEntry>::iterator> cache_map_;
};

// Usage in OES:
// LRUCache<std::string, UserRecord> user_cache(1000, std::chrono::minutes(30));
// LRUCache<int, ReportData> report_cache(100, std::chrono::hours(1));
```

### Disk cache (files)

```cpp
// Disk cache for reports and large objects
// src/cache/DiskCache.cpp

class DiskCache {
public:
    explicit DiskCache(const std::filesystem::path& cache_dir,
                       size_t max_size_mb = 512,
                       std::chrono::hours ttl = std::chrono::hours(24))
        : cache_dir_(cache_dir), max_size_bytes_(max_size_mb * 1024 * 1024), ttl_(ttl)
    {
        std::filesystem::create_directories(cache_dir_);
    }

    // Cache report data
    void store(const std::string& key, const std::vector<uint8_t>& data) {
        auto path = get_path(key);
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
        evict_if_needed();
    }

    std::optional<std::vector<uint8_t>> load(const std::string& key) {
        auto path = get_path(key);
        if (!std::filesystem::exists(path)) return std::nullopt;

        // Check the file age
        // NOTE: std::chrono::file_clock::to_sys() is C++20. Workaround for C++17:
        // convert file_time_type to system_clock via time_t.
        auto file_time = std::filesystem::last_write_time(path);
        auto file_time_t = decltype(file_time)::clock::to_time_t(file_time);
        auto file_sys_time = std::chrono::system_clock::from_time_t(file_time_t);
        auto age = std::chrono::system_clock::now() - file_sys_time;
        if (age > ttl_) {
            std::filesystem::remove(path);
            return std::nullopt;
        }

        std::ifstream f(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
    }

private:
    std::filesystem::path get_path(const std::string& key) {
        // Hash the key into a file name
        auto hash = std::hash<std::string>{}(key);
        return cache_dir_ / (std::to_string(hash) + ".cache");
    }

    void evict_if_needed() {
        // Remove old files if the cache size is exceeded
        // ... (implementation)
    }

    std::filesystem::path cache_dir_;
    size_t max_size_bytes_;
    std::chrono::hours ttl_;
};

// Disk cache path:
// Windows: C:\ProgramData\OES\Cache\
// macOS:   ~/Library/Caches/OES/  (desktop)  or  /var/cache/oes/ (daemon)
// Linux:   /var/cache/oes/  or  ~/.cache/oes/ (desktop)
```

---

## Redis (optional, daemon/server mode)

Redis is useful in OES daemon when:
- Multiple daemon instances share a common cache
- Distributed user sessions are needed
- Pub/sub is required to synchronize between nodes
- The cache must survive a daemon restart

In most cases, **the built-in in-process cache is enough**.

### Installing Redis

```bash
sudo apt install -y redis-server

# Verify
sudo systemctl status redis-server
redis-cli ping
# PONG

redis-cli INFO server | grep redis_version
```

### Redis security

```bash
sudo nano /etc/redis/redis.conf
```

```
# Listen on localhost only (daemon and Redis on the same host)
bind 127.0.0.1 ::1

# Password
requirepass GENERATED_PASSWORD

# Disable dangerous commands
rename-command FLUSHALL ""
rename-command FLUSHDB ""
rename-command CONFIG ""
rename-command SHUTDOWN REDIS_SHUTDOWN_SECRET

# Protected mode
protected-mode yes

# Maximum clients (reasonable limit for OES daemon)
maxclients 100
```

```bash
sudo systemctl restart redis-server
sudo systemctl enable redis-server

# Verify with password
redis-cli -a PASSWORD ping
# PONG
```

### maxmemory configuration for OES cache

```
# redis.conf
maxmemory 256mb          # 256 MB for cache (daemon mode)

# allkeys-lru - evict least recently used keys (recommended for caching)
maxmemory-policy allkeys-lru
```

### Using Redis from C++ (hiredis)

```bash
# Install hiredis
sudo apt install -y libhiredis-dev
# CMakeLists.txt: find_package(hiredis REQUIRED)
```

```cpp
// src/cache/RedisCache.cpp
#include <hiredis/hiredis.h>
#include <string>
#include <optional>

class RedisCache {
public:
    RedisCache(const std::string& host, int port, const std::string& password)
    {
        context_ = redisConnect(host.c_str(), port);
        if (!context_ || context_->err) {
            throw std::runtime_error("Redis connection failed: " +
                std::string(context_ ? context_->errstr : "nullptr"));
        }
        // AUTH
        if (!password.empty()) {
            auto* reply = static_cast<redisReply*>(
                redisCommand(context_, "AUTH %s", password.c_str()));
            bool ok = reply && reply->type == REDIS_REPLY_STATUS;
            freeReplyObject(reply);
            if (!ok) throw std::runtime_error("Redis AUTH failed");
        }
    }

    ~RedisCache() {
        if (context_) redisFree(context_);
    }

    // SET key value with TTL in seconds
    bool set(const std::string& key, const std::string& value, int ttl_sec = 300) {
        auto* reply = static_cast<redisReply*>(
            redisCommand(context_, "SETEX %s %d %s", key.c_str(), ttl_sec, value.c_str()));
        bool ok = reply && reply->type == REDIS_REPLY_STATUS;
        freeReplyObject(reply);
        return ok;
    }

    // GET key
    std::optional<std::string> get(const std::string& key) {
        auto* reply = static_cast<redisReply*>(
            redisCommand(context_, "GET %s", key.c_str()));
        if (!reply || reply->type != REDIS_REPLY_STRING) {
            freeReplyObject(reply);
            return std::nullopt;
        }
        std::string result(reply->str, reply->len);
        freeReplyObject(reply);
        return result;
    }

    // DEL key
    bool del(const std::string& key) {
        auto* reply = static_cast<redisReply*>(
            redisCommand(context_, "DEL %s", key.c_str()));
        bool ok = reply && reply->integer > 0;
        freeReplyObject(reply);
        return ok;
    }

private:
    redisContext* context_ = nullptr;
};

// Example usage in OES daemon:
// RedisCache cache("127.0.0.1", 6379, getenv("REDIS_PASSWORD"));
//
// // Cache the result of a DB query
// std::string cache_key = "report:" + std::to_string(report_id);
// if (auto cached = cache.get(cache_key)) {
//     return deserialize_report(*cached);
// }
// auto report = db.generate_report(report_id);
// cache.set(cache_key, serialize_report(report), 3600);  // 1 hour
// return report;
```

### Redis monitoring

```bash
# General information
redis-cli -a PASSWORD INFO

# Memory usage
redis-cli -a PASSWORD INFO memory | grep used_memory_human

# Key count
redis-cli -a PASSWORD DBSIZE

# Cache hit statistics
redis-cli -a PASSWORD INFO stats | grep -E "keyspace_hits|keyspace_misses"
# keyspace_hits:15234
# keyspace_misses:892
# Hit ratio = hits / (hits + misses) * 100 = should be > 80%

# Slow commands
redis-cli -a PASSWORD SLOWLOG GET 10
```

### Redis backups (if used)

```bash
#!/bin/bash
# /opt/scripts/redis-backup.sh
# Makes sense ONLY if Redis stores important data (not a pure cache)

REDIS_PASS="${REDIS_PASSWORD}"
BACKUP_DIR="/var/backups/redis"
DATE=$(date +%Y-%m-%d_%H-%M-%S)
RETENTION_DAYS=7

mkdir -p "$BACKUP_DIR"

# Save a snapshot
redis-cli -a "$REDIS_PASS" BGSAVE 2>/dev/null
sleep 3

# Copy
cp /var/lib/redis/dump.rdb "${BACKUP_DIR}/redis_${DATE}.rdb"
gzip "${BACKUP_DIR}/redis_${DATE}.rdb"

find "$BACKUP_DIR" -name "*.rdb.gz" -mtime +$RETENTION_DAYS -delete

echo "Redis backup: redis_${DATE}.rdb.gz"
```

---

## Database connection pool

An efficient connection pool is the most important "caching" mechanism in OES daemon, reducing latency.

### Firebird connection pool (C++)

```cpp
// src/db/FirebirdConnectionPool.h
#include <queue>
#include <mutex>
#include <condition_variable>
#include <ibase.h>  // Firebird C API

class FirebirdConnectionPool {
public:
    FirebirdConnectionPool(const std::string& dsn,
                           const std::string& user,
                           const std::string& password,
                           size_t pool_size = 10)
        : dsn_(dsn), user_(user), password_(password), pool_size_(pool_size)
    {
        for (size_t i = 0; i < pool_size; ++i) {
            connections_.push(create_connection());
        }
    }

    // RAII wrapper: returns the connection to the pool automatically
    class Connection {
    public:
        Connection(isc_db_handle handle, FirebirdConnectionPool& pool)
            : handle_(handle), pool_(pool) {}
        ~Connection() { pool_.release(handle_); }
        isc_db_handle get() { return handle_; }
    private:
        isc_db_handle handle_;
        FirebirdConnectionPool& pool_;
    };

    Connection acquire() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !connections_.empty(); });
        auto handle = connections_.front();
        connections_.pop();
        return Connection(handle, *this);
    }

    size_t available() const {
        std::lock_guard lock(mutex_);
        return connections_.size();
    }

private:
    void release(isc_db_handle handle) {
        std::lock_guard lock(mutex_);
        connections_.push(handle);
        cv_.notify_one();
    }

    isc_db_handle create_connection() {
        // ... Firebird API connection setup
        isc_db_handle handle = 0;
        // isc_attach_database(...)
        return handle;
    }

    std::string dsn_, user_, password_;
    size_t pool_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<isc_db_handle> connections_;
};

// Recommended pool sizes for OES daemon:
// Desktop:    1-3 connections (single user)
// Daemon:    10-20 connections (several clients)
// Enterprise: 20-50 connections (high load)
```

---

## Query and result caching

### Prepared statements (Firebird / PostgreSQL)

```cpp
// Prepared queries significantly reduce SQL parsing overhead
// Important for OES daemon under high RPS

// Firebird via libfbclient:
isc_stmt_handle stmt = 0;
isc_dsql_alloc_statement2(status, &db_handle, &stmt);
isc_dsql_prepare(status, &tr_handle, &stmt, 0,
    "SELECT id, name, value FROM config WHERE section = ?",
    SQL_DIALECT_V6, nullptr);
// Reuse stmt many times, changing the parameter

// PostgreSQL via libpq:
PGconn* conn = PQconnectdb("host=localhost dbname=oes_db user=oes_user password=...");
PGresult* res = PQprepare(conn, "get_config",
    "SELECT id, name, value FROM config WHERE section = $1", 1, nullptr);
// Usage:
const char* params[] = { "database" };
PGresult* result = PQexecPrepared(conn, "get_config", 1, params, nullptr, nullptr, 0);
```

### Cache invalidation strategy in OES

```
Cache:
  - Reference data (user list, groups, roles) - TTL 5-30 minutes
  - Application configuration - TTL until restart (invalidate on change)
  - Compiled report templates - TTL 1 hour
  - Table metadata - TTL until a DDL operation

Do NOT cache (or very short TTL):
  - Transactional data (orders, payments)
  - Real-time data
  - User sessions (only if synchronization is not required)

Invalidation:
  - By event (data change -> remove cache entry)
  - By TTL (max age of an entry)
  - On OES daemon restart (full in-process cache flush)
```

---

## docker-compose for Redis (dev environment)

```yaml
# Add to docker-compose.dev.yml
  redis:
    image: redis:7-alpine
    container_name: oes-dev-redis
    restart: unless-stopped
    command: redis-server --requirepass dev_redis_pass
    ports:
      - "6379:6379"
    volumes:
      - redis_data:/data
    healthcheck:
      test: ["CMD", "redis-cli", "-a", "dev_redis_pass", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5
```

---

## When OES needs Redis

```
Redis is NOT needed:
  - Desktop mode (OES.exe + embedded DB)
  - A single daemon instance with low load
  - When the in-process LRU cache is enough

Redis IS needed:
  - Multiple daemon instances sharing a cache
  - A cache that must survive a daemon restart
  - Distributed rate limiting across instances
  - Pub/Sub notifications between OES nodes
  - A large cache (>1GB) that should not live in the C++ heap

Redis alternatives for OES:
  - Built-in LRU cache (std::unordered_map + TTL) - for most cases
  - memcached - simpler than Redis if only a key-value cache is needed
  - Shared memory (boost::interprocess) - cache across processes on one host
  - SQLite in-memory - structured cache with no external dependencies
```

---

## Cache monitoring script

```bash
#!/bin/bash
# /opt/scripts/cache-monitor.sh
echo "=== OES Cache Monitor ==="

# Redis (if used)
if redis-cli -a "$REDIS_PASSWORD" ping 2>/dev/null | grep -q PONG; then
  echo ""
  echo "--- Redis ---"
  redis-cli -a "$REDIS_PASSWORD" INFO memory 2>/dev/null | \
    grep -E "used_memory_human|maxmemory_human"
  HITS=$(redis-cli -a "$REDIS_PASSWORD" INFO stats 2>/dev/null | \
    grep keyspace_hits | cut -d: -f2 | tr -d '[:space:]')
  MISSES=$(redis-cli -a "$REDIS_PASSWORD" INFO stats 2>/dev/null | \
    grep keyspace_misses | cut -d: -f2 | tr -d '[:space:]')
  if [ -n "$HITS" ] && [ -n "$MISSES" ] && [ "$((HITS + MISSES))" -gt 0 ]; then
    HIT_RATIO=$(echo "scale=1; $HITS * 100 / ($HITS + $MISSES)" | bc)
    echo "Hit ratio: ${HIT_RATIO}% (hits: $HITS, misses: $MISSES)"
  fi
  echo "Keys: $(redis-cli -a "$REDIS_PASSWORD" DBSIZE 2>/dev/null)"
else
  echo "Redis: not running (using in-process cache)"
fi

# OES disk cache
DISK_CACHE="/var/cache/oes"
if [ -d "$DISK_CACHE" ]; then
  echo ""
  echo "--- OES disk cache ---"
  echo "Size: $(du -sh $DISK_CACHE | cut -f1)"
  echo "Files: $(find $DISK_CACHE -type f | wc -l)"
fi
```
