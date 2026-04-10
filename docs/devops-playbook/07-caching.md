# 07. Кэширование в OES

> OES — desktop/server приложение на C++. Redis опционален только в daemon/server режиме.
> В desktop-режиме используется встроенное in-process кэширование без внешних зависимостей.

---

## Обзор: стратегии кэширования по режиму

```
Desktop режим (GUI, wxWidgets):
  — Кэш хранится в памяти процесса (std::unordered_map, LRU)
  — Дисковый кэш в директории приложения
  — Redis НЕ нужен

Daemon / Server режим (headless, много клиентов):
  — Встроенный in-process кэш (первый уровень)
  — Redis — опциональный внешний кэш (второй уровень, если нужен)
  — Firebird/PostgreSQL с prepared statements и пулом соединений
```

---

## In-Process кэширование (C++)

### LRU-кэш в памяти

```cpp
// include/cache/LRUCache.h — пример реализации в OES
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

    // Получить значение из кэша
    std::optional<Value> get(const Key& key) {
        std::lock_guard lock(mutex_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) return std::nullopt;

        // Проверить TTL
        if (std::chrono::steady_clock::now() > it->second->expires) {
            lru_list_.erase(it->second);
            cache_map_.erase(it);
            return std::nullopt;
        }

        // Переместить в начало (LRU)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return it->second->value;
    }

    // Поместить значение в кэш
    void put(const Key& key, Value value) {
        std::lock_guard lock(mutex_);
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            lru_list_.erase(it->second);
            cache_map_.erase(it);
        }
        if (cache_map_.size() >= capacity_) {
            // Удалить наименее используемый
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

// Использование в OES:
// LRUCache<std::string, UserRecord> user_cache(1000, std::chrono::minutes(30));
// LRUCache<int, ReportData> report_cache(100, std::chrono::hours(1));
```

### Дисковый кэш (файлы)

```cpp
// Дисковый кэш для отчётов и больших объектов
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

    // Кэшировать данные отчёта
    void store(const std::string& key, const std::vector<uint8_t>& data) {
        auto path = get_path(key);
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
        evict_if_needed();
    }

    std::optional<std::vector<uint8_t>> load(const std::string& key) {
        auto path = get_path(key);
        if (!std::filesystem::exists(path)) return std::nullopt;

        // Проверить возраст файла
        // NOTE: std::chrono::file_clock::to_sys() — C++20. Обходной путь для C++17:
        // конвертируем file_time_type в system_clock через time_t.
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
        // Хэш ключа в имя файла
        auto hash = std::hash<std::string>{}(key);
        return cache_dir_ / (std::to_string(hash) + ".cache");
    }

    void evict_if_needed() {
        // Удалить старые файлы если превышен размер кэша
        // ... (реализация)
    }

    std::filesystem::path cache_dir_;
    size_t max_size_bytes_;
    std::chrono::hours ttl_;
};

// Путь к дисковому кэшу:
// Windows: C:\ProgramData\OES\Cache\
// macOS:   ~/Library/Caches/OES/  (desktop)  или  /var/cache/oes/ (daemon)
// Linux:   /var/cache/oes/  или  ~/.cache/oes/ (desktop)
```

---

## Redis (опционально, daemon/server режим)

Redis полезен в OES daemon когда:
- Несколько инстансов daemon разделяют общий кэш
- Нужны распределённые сессии пользователей
- Требуется pub/sub для синхронизации между узлами
- Кэш должен переживать перезапуск daemon

В большинстве случаев **встроенного in-process кэша достаточно**.

### Установка Redis

```bash
sudo apt install -y redis-server

# Проверить
sudo systemctl status redis-server
redis-cli ping
# PONG

redis-cli INFO server | grep redis_version
```

### Безопасность Redis

```bash
sudo nano /etc/redis/redis.conf
```

```
# Слушать только localhost (daemon и Redis на одном хосте)
bind 127.0.0.1 ::1

# Пароль
requirepass СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ

# Отключить опасные команды
rename-command FLUSHALL ""
rename-command FLUSHDB ""
rename-command CONFIG ""
rename-command SHUTDOWN REDIS_SHUTDOWN_SECRET

# Защищённый режим
protected-mode yes

# Максимум клиентов (разумный лимит для OES daemon)
maxclients 100
```

```bash
sudo systemctl restart redis-server
sudo systemctl enable redis-server

# Проверить с паролем
redis-cli -a ПАРОЛЬ ping
# PONG
```

### Настройка maxmemory для кэша OES

```
# redis.conf
maxmemory 256mb          # 256 MB для кэша (daemon режим)

# allkeys-lru — удалять наименее используемые ключи (рекомендуется для кэша)
maxmemory-policy allkeys-lru
```

### Использование Redis из C++ (hiredis)

```bash
# Установить hiredis
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

    // SET ключ значение с TTL в секундах
    bool set(const std::string& key, const std::string& value, int ttl_sec = 300) {
        auto* reply = static_cast<redisReply*>(
            redisCommand(context_, "SETEX %s %d %s", key.c_str(), ttl_sec, value.c_str()));
        bool ok = reply && reply->type == REDIS_REPLY_STATUS;
        freeReplyObject(reply);
        return ok;
    }

    // GET ключ
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

    // DEL ключ
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

// Пример использования в OES daemon:
// RedisCache cache("127.0.0.1", 6379, getenv("REDIS_PASSWORD"));
//
// // Кэшировать результат запроса к БД
// std::string cache_key = "report:" + std::to_string(report_id);
// if (auto cached = cache.get(cache_key)) {
//     return deserialize_report(*cached);
// }
// auto report = db.generate_report(report_id);
// cache.set(cache_key, serialize_report(report), 3600);  // 1 час
// return report;
```

### Мониторинг Redis

```bash
# Основная информация
redis-cli -a ПАРОЛЬ INFO

# Использование памяти
redis-cli -a ПАРОЛЬ INFO memory | grep used_memory_human

# Количество ключей
redis-cli -a ПАРОЛЬ DBSIZE

# Статистика попаданий в кэш
redis-cli -a ПАРОЛЬ INFO stats | grep -E "keyspace_hits|keyspace_misses"
# keyspace_hits:15234
# keyspace_misses:892
# Hit ratio = hits / (hits + misses) * 100 = должен быть > 80%

# Медленные команды
redis-cli -a ПАРОЛЬ SLOWLOG GET 10
```

### Бэкапы Redis (если используется)

```bash
#!/bin/bash
# /opt/scripts/redis-backup.sh
# Имеет смысл ТОЛЬКО если Redis хранит важные данные (не чистый кэш)

REDIS_PASS="${REDIS_PASSWORD}"
BACKUP_DIR="/var/backups/redis"
DATE=$(date +%Y-%m-%d_%H-%M-%S)
RETENTION_DAYS=7

mkdir -p "$BACKUP_DIR"

# Сохранить снапшот
redis-cli -a "$REDIS_PASS" BGSAVE 2>/dev/null
sleep 3

# Скопировать
cp /var/lib/redis/dump.rdb "${BACKUP_DIR}/redis_${DATE}.rdb"
gzip "${BACKUP_DIR}/redis_${DATE}.rdb"

find "$BACKUP_DIR" -name "*.rdb.gz" -mtime +$RETENTION_DAYS -delete

echo "Redis бэкап: redis_${DATE}.rdb.gz"
```

---

## Пул соединений к базам данных

Эффективный пул соединений — важнейший механизм "кэширования" в OES daemon, снижающий latency.

### Пул соединений Firebird (C++)

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

    // RAII wrapper: автоматически возвращает соединение в пул
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

// Рекомендуемые размеры пула для OES daemon:
// Desktop:    1-3 соединения (один пользователь)
// Daemon:    10-20 соединений (несколько клиентов)
// Enterprise: 20-50 соединений (высокая нагрузка)
```

---

## Кэширование запросов и результатов

### Prepared Statements (Firebird / PostgreSQL)

```cpp
// Подготовленные запросы значительно снижают нагрузку на парсинг SQL
// Важно для OES daemon при высоком RPS

// Firebird через libfbclient:
isc_stmt_handle stmt = 0;
isc_dsql_alloc_statement2(status, &db_handle, &stmt);
isc_dsql_prepare(status, &tr_handle, &stmt, 0,
    "SELECT id, name, value FROM config WHERE section = ?",
    SQL_DIALECT_V6, nullptr);
// Используем stmt многократно, меняя параметр

// PostgreSQL через libpq:
PGconn* conn = PQconnectdb("host=localhost dbname=oes_db user=oes_user password=...");
PGresult* res = PQprepare(conn, "get_config",
    "SELECT id, name, value FROM config WHERE section = $1", 1, nullptr);
// Использование:
const char* params[] = { "database" };
PGresult* result = PQexecPrepared(conn, "get_config", 1, params, nullptr, nullptr, 0);
```

### Стратегия инвалидации кэша в OES

```
Кэшировать:
  — Справочники (список пользователей, групп, ролей) — TTL 5-30 минут
  — Конфигурация приложения — TTL до перезапуска (invalidate при изменении)
  — Скомпилированные шаблоны отчётов — TTL 1 час
  — Метаданные таблиц — TTL до DDL-операции

НЕ кэшировать (или очень короткий TTL):
  — Транзакционные данные (заказы, платежи)
  — Данные реального времени
  — Пользовательские сессии (только если не нужна синхронизация)

Инвалидация:
  — По событию (изменение данных → удалить кэш)
  — По TTL (максимальный возраст записи)
  — При перезапуске OES daemon (полная очистка in-process кэша)
```

---

## docker-compose для Redis (dev-окружение)

```yaml
# Добавить в docker-compose.dev.yml
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

## Когда нужен Redis для OES

```
Redis НЕ нужен:
  — Desktop режим (OES.exe + встроенная БД)
  — Один daemon инстанс с небольшой нагрузкой
  — Когда in-process LRU кэша достаточно

Redis НУЖЕН:
  — Несколько daemon инстансов с общим кэшем
  — Нужен кэш, переживающий перезапуск daemon
  — Distributed rate limiting между инстансами
  — Pub/Sub уведомления между узлами OES
  — Большой кэш (>1GB), который не хочется держать в heap C++

Альтернативы Redis для OES:
  — Встроенный LRU-кэш (std::unordered_map + TTL) — для большинства случаев
  — memcached — проще Redis, если нужен только key-value кэш
  — Shared memory (boost::interprocess) — кэш между процессами на одном хосте
  — SQLite in-memory — структурированный кэш без внешних зависимостей
```

---

## Скрипт мониторинга кэша

```bash
#!/bin/bash
# /opt/scripts/cache-monitor.sh
echo "=== OES Cache Monitor ==="

# Redis (если используется)
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
  echo "Ключей: $(redis-cli -a "$REDIS_PASSWORD" DBSIZE 2>/dev/null)"
else
  echo "Redis: не запущен (используется in-process кэш)"
fi

# Дисковый кэш OES
DISK_CACHE="/var/cache/oes"
if [ -d "$DISK_CACHE" ]; then
  echo ""
  echo "--- Дисковый кэш OES ---"
  echo "Размер: $(du -sh $DISK_CACHE | cut -f1)"
  echo "Файлов: $(find $DISK_CACHE -type f | wc -l)"
fi
```
