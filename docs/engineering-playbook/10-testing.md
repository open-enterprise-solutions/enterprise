# 10. Тестирование

## Философия

Тесты — это не бюрократия, а защита от регрессий. OES исторически не имел тестов — это риск при каждом рефакторинге. Мы вводим тестирование постепенно, начиная с критичной бизнес-логики. Один хороший тест важнее десяти формальных.

**Стратегия:** новый код — покрывается тестами сразу. Старый код — покрывается тестами при рефакторинге или при исправлении багов.

---

## Уровни тестирования

```
          ┌─────────────────┐
          │   System / E2E   │  Мало тестов, медленные, дорогие
          │  (ручное или     │  (пока — только ручное тестирование)
          │  автоматизация)  │
          ├─────────────────┤
          │  Integration     │  Средне тестов, средняя скорость
          │  (Google Test +  │  (тесты с реальной SQLite/Firebird)
          │   реальная БД)   │
          ├─────────────────┤
          │    Unit          │  Много тестов, быстрые, дешёвые
          │  (Google Test +  │  (изолированная логика, моки через
          │   Google Mock)   │   Google Mock)
          └─────────────────┘
```

---

## Инструменты

### Google Test и Google Mock

```cmake
# CMakeLists.txt — подключение Google Test
find_package(GTest REQUIRED)

add_executable(oes_tests
    tests/unit/test_db_query_builder.cpp
    tests/unit/test_discount_calculator.cpp
    tests/integration/test_firebird_crud.cpp
)

target_link_libraries(oes_tests
    PRIVATE
        GTest::gtest_main
        GTest::gmock
        oes_backend     # src/engine/backend/ — ibDatabaseLayer, ibCompileCode и др.
        oes_frontend    # src/engine/frontend/ — ibValueForm и др.
)

# Интеграция с CTest
include(GoogleTest)
gtest_discover_tests(oes_tests)
```

### vcpkg для Google Test

```bash
# Установка через vcpkg
vcpkg install gtest:x64-windows
vcpkg install gtest:x64-linux

# Или через CMake FetchContent (без vcpkg)
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)
```

### CTest для запуска тестов

```bash
# Сборка и запуск всех тестов
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

# Запуск с фильтром
ctest --test-dir build -R "DBQueryBuilder"

# Запуск с параллелизмом
ctest --test-dir build -j4 --output-on-failure

# Детальный вывод
ctest --test-dir build -V
```

---

## Unit тесты

Тестируют **один** класс/функцию в изоляции от зависимостей.

**Что тестировать:**
- Бизнес-логика (расчёты, правила, трансформации)
- Утилитарные классы
- Парсеры и форматтеры
- Строители запросов (QueryBuilder)
- Логика валидации

### Структура тестового файла

```cpp
// tests/unit/test_discount_calculator.cpp
#include <gtest/gtest.h>
#include "core/DiscountCalculator.h"

// Фикстура (общий setUp для группы тестов)
class DiscountCalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        calculator = std::make_unique<DiscountCalculator>();
    }

    std::unique_ptr<DiscountCalculator> calculator;
};

// Тест: позитивный сценарий
TEST_F(DiscountCalculatorTest, ApplyPercentDiscount) {
    // Arrange
    Money price{10000};           // 100.00 руб
    Discount discount{15, DiscountType::Percent};

    // Act
    Money result = calculator->apply(price, discount);

    // Assert
    EXPECT_EQ(result.cents(), 8500);  // 85.00 руб
}

TEST_F(DiscountCalculatorTest, ApplyFixedDiscount) {
    Money price{10000};
    Discount discount{2000, DiscountType::Fixed};

    Money result = calculator->apply(price, discount);

    EXPECT_EQ(result.cents(), 8000);
}

TEST_F(DiscountCalculatorTest, DiscountCannotProduceNegativeTotal) {
    Money price{1000};
    Discount discount{5000, DiscountType::Fixed};  // Больше цены

    Money result = calculator->apply(price, discount);

    EXPECT_EQ(result.cents(), 0);  // Минимум 0, не отрицательный
}

TEST_F(DiscountCalculatorTest, ThrowsOnInvalidDiscountType) {
    Money price{10000};
    Discount discount{10, static_cast<DiscountType>(999)};  // Невалидный тип

    EXPECT_THROW(calculator->apply(price, discount), std::invalid_argument);
}

// Параметризованные тесты
class DiscountBoundaryTest : public ::testing::TestWithParam<
    std::tuple<int, int, int>>  // price, discountPct, expected
{};

TEST_P(DiscountBoundaryTest, PercentDiscountCalculation) {
    auto [price, pct, expected] = GetParam();
    DiscountCalculator calc;
    EXPECT_EQ(calc.apply(Money{price}, Discount{pct, DiscountType::Percent}).cents(), expected);
}

INSTANTIATE_TEST_SUITE_P(
    DiscountBoundaryValues,
    DiscountBoundaryTest,
    ::testing::Values(
        std::make_tuple(10000, 0,   10000),  // 0% скидка
        std::make_tuple(10000, 100, 0),      // 100% скидка
        std::make_tuple(10000, 50,  5000),   // 50% скидка
        std::make_tuple(1,     10,  0)       // Округление вниз до 0
    )
);
```

### Google Mock для изоляции зависимостей

```cpp
// tests/mocks/MockDatabaseLayer.h
// ibDatabaseLayer (src/engine/backend/databaseLayer/databaseLayer.h) —
// абстрактный базовый класс для всех СУБД-бэкендов OES.
// Мокируем его для изоляции бизнес-логики от реальной БД.
#include <gmock/gmock.h>
#include "engine/backend/databaseLayer/databaseLayer.h"

class MockDatabaseLayer : public ibDatabaseLayer {
public:
    MOCK_METHOD(bool, Open, (const wxString& dbPath,
                             const wxString& user,
                             const wxString& password), (override));
    MOCK_METHOD(bool, Close, (), (override));
    MOCK_METHOD(ibPreparedStatement*, PrepareStatement,
                (const wxString& sql), (override));
    MOCK_METHOD(bool, BeginTransaction, (), (override));
    MOCK_METHOD(bool, CommitTransaction, (), (override));
    MOCK_METHOD(bool, RollbackTransaction, (), (override));
};
```

```cpp
// tests/unit/test_user_service.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mocks/MockDatabaseLayer.h"
#include "services/UserService.h"

using ::testing::Return;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Not;

// UserService — пример сервиса, использующего ibDatabaseLayer через MockDatabaseLayer.
// MockDatabaseLayer мокирует ibDatabaseLayer::PrepareStatement().
// MockPreparedStatement мокирует ibPreparedStatement.

class UserServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockDb = std::make_shared<MockDatabaseLayer>();
        service = std::make_unique<UserService>(mockDb.get());
    }

    std::shared_ptr<MockDatabaseLayer> mockDb;
    std::unique_ptr<UserService> service;
};

TEST_F(UserServiceTest, FindUserByIdReturnsUser) {
    // Настроить ожидание: PrepareStatement + SetParamInt + RunQuery
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    auto mockRs   = std::make_unique<MockDatabaseResultSet>();

    EXPECT_CALL(*mockRs, Next()).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_CALL(*mockRs, GetResultInt(0)).WillOnce(Return(42));
    EXPECT_CALL(*mockRs, GetResultString("EMAIL"))
        .WillOnce(Return(wxString("john@example.com")));

    EXPECT_CALL(*mockStmt, SetParamInt(1, 42));
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(_)).WillOnce(Return(mockStmt.get()));

    // Act
    auto user = service->findById(42);

    // Assert
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->id, 42);
    EXPECT_EQ(user->email, "john@example.com");
}

TEST_F(UserServiceTest, FindUserByIdReturnsNulloptIfNotFound) {
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    auto mockRs   = std::make_unique<MockDatabaseResultSet>();

    EXPECT_CALL(*mockRs, Next()).WillOnce(Return(false));  // 0 строк
    EXPECT_CALL(*mockStmt, SetParamInt(1, 999));
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(_)).WillOnce(Return(mockStmt.get()));

    auto user = service->findById(999);

    EXPECT_FALSE(user.has_value());
}

TEST_F(UserServiceTest, SqlQueryDoesNotContainRawPassword) {
    // Безопасность: пароль передаётся через SetParamString, не конкатенируется в SQL
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    auto mockRs   = std::make_unique<MockDatabaseResultSet>();
    wxString capturedSql;

    EXPECT_CALL(*mockRs, Next()).WillRepeatedly(Return(false));
    // Ожидаем SetParamString — пароль НЕ должен попасть в SQL-строку
    EXPECT_CALL(*mockStmt, SetParamString(2, _));
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(::testing::SaveArg<0>(&capturedSql)))
        .WillOnce(Return(mockStmt.get()));

    service->authenticate("user@test.com", "mypassword");

    // SQL не должен содержать пароль в явном виде
    EXPECT_THAT(capturedSql.ToStdString(), Not(HasSubstr("mypassword")));
}
```

---

## Integration тесты

Тестируют **взаимодействие с реальной БД** — полный цикл: создание/чтение/обновление/удаление данных.

**Что тестировать:**
- CRUD операции через DBLayer
- Транзакции и откаты
- Применение схемы (миграции)
- Производительность запросов

### Пример интеграционного теста с Firebird

```cpp
// tests/integration/test_firebird_crud.cpp
#include <gtest/gtest.h>
#include "engine/backend/databaseLayer/firebird/databaseLayerFirebird.h"
#include "TestDbConfig.h"  // TestDbConfig::Get() — конфиг тестовой БД

// Фикстура с реальным подключением к БД
class FirebirdCrudTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Создать тестовую БД один раз для всего suite
        // ibDatabaseLayerFirebird — конкретная реализация ibDatabaseLayer для Firebird
        s_db = std::make_shared<ibDatabaseLayerFirebird>();
        ASSERT_TRUE(s_db->Open(TestDbConfig::GetPath(),
                               TestDbConfig::GetUser(),
                               TestDbConfig::GetPassword()));
        // RECREATE TABLE — Firebird-эквивалент "DROP IF EXISTS + CREATE"
        // (Firebird не поддерживает CREATE TABLE IF NOT EXISTS)
        ASSERT_TRUE(s_db->Execute(R"(
            RECREATE TABLE TEST_USERS (
                ID INTEGER NOT NULL PRIMARY KEY,
                EMAIL VARCHAR(255) NOT NULL,
                NAME VARCHAR(255)
            )
        )").IsSuccess());
    }

    static void TearDownTestSuite() {
        s_db->Execute("DROP TABLE TEST_USERS");  // Firebird: нет IF EXISTS для DROP TABLE
        s_db->Disconnect();
    }

    void SetUp() override {
        // Очистить данные перед каждым тестом
        s_db->Execute("DELETE FROM TEST_USERS");
    }

    static std::shared_ptr<ibDatabaseLayerFirebird> s_db;
};

std::shared_ptr<ibDatabaseLayerFirebird> FirebirdCrudTest::s_db;

TEST_F(FirebirdCrudTest, InsertAndSelectRecord) {
    // Insert — ibPreparedStatement + SetParamString/SetParamInt
    ibPreparedStatement* insertStmt = s_db->PrepareStatement(
        "INSERT INTO TEST_USERS (ID, EMAIL, NAME) VALUES (?, ?, ?)"
    );
    insertStmt->SetParamInt(1, 1);
    insertStmt->SetParamString(2, "test@example.com");
    insertStmt->SetParamString(3, "Test User");
    insertStmt->RunQuery();

    // Select — ibPreparedStatement + ibDatabaseResultSet
    ibPreparedStatement* selectStmt = s_db->PrepareStatement(
        "SELECT ID, EMAIL, NAME FROM TEST_USERS WHERE ID = ?"
    );
    selectStmt->SetParamInt(1, 1);
    ibDatabaseResultSet* rs = selectStmt->RunQuery();

    ASSERT_TRUE(rs != nullptr);
    ASSERT_TRUE(rs->Next());
    EXPECT_EQ(rs->GetResultString("EMAIL"), "test@example.com");
    EXPECT_EQ(rs->GetResultString("NAME"), "Test User");
}

TEST_F(FirebirdCrudTest, TransactionRollbackOnError) {
    // ibTransactionGuard — RAII, откатывает при выходе без Commit()
    {
        ibTransactionGuard tx(s_db.get());

        ibPreparedStatement* stmt = s_db->PrepareStatement(
            "INSERT INTO TEST_USERS (ID, EMAIL) VALUES (?, ?)"
        );
        stmt->SetParamInt(1, 1);
        stmt->SetParamString(2, "first@test.com");
        stmt->RunQuery();

        // tx уничтожается без Commit() — автоматический rollback
    }

    // Убедиться что запись не вставлена
    ibPreparedStatement* countStmt = s_db->PrepareStatement(
        "SELECT COUNT(*) FROM TEST_USERS"
    );
    ibDatabaseResultSet* rs = countStmt->RunQuery();
    ASSERT_TRUE(rs && rs->Next());
    EXPECT_EQ(rs->GetResultInt(0), 0);
}

TEST_F(FirebirdCrudTest, ParametrizedQueryPreventsInjection) {
    // Попытка SQL-инъекции через параметр
    wxString maliciousInput = "'; DROP TABLE TEST_USERS; --";

    ibPreparedStatement* stmt = s_db->PrepareStatement(
        "SELECT * FROM TEST_USERS WHERE EMAIL = ?"
    );
    stmt->SetParamString(1, maliciousInput);
    ibDatabaseResultSet* rs = stmt->RunQuery();

    // Запрос должен выполниться успешно (не найти записей, не упасть)
    ASSERT_TRUE(rs != nullptr);
    EXPECT_FALSE(rs->Next());  // 0 строк — инъекция не сработала

    // Таблица должна существовать
    ibPreparedStatement* checkStmt = s_db->PrepareStatement(
        "SELECT COUNT(*) FROM TEST_USERS"
    );
    ibDatabaseResultSet* checkRs = checkStmt->RunQuery();
    EXPECT_TRUE(checkRs != nullptr);
}
```

### Конфигурация тестовой БД

```cpp
// tests/TestDbConfig.h
#pragma once
#include "database/DatabaseConfig.h"
#include <cstdlib>
#include <string>

// Платформо-зависимый путь к тестовой БД по умолчанию:
#ifdef _WIN32
#   define TEST_DB_DEFAULT_PATH "C:\\Temp\\oes_test.fdb"
#else
#   define TEST_DB_DEFAULT_PATH "/tmp/oes_test.fdb"
#endif

class TestDbConfig {
public:
    static DatabaseConfig Get() {
        DatabaseConfig config;
        // Берём из переменных окружения (для CI/CD) или используем defaults
        config.type = DatabaseType::Firebird;
        config.host = getEnvOrDefault("TEST_DB_HOST", "localhost");
        config.port = std::stoi(getEnvOrDefault("TEST_DB_PORT", "3050"));
        config.database = getEnvOrDefault(
            "TEST_DB_PATH",
            TEST_DB_DEFAULT_PATH  // определяется через #ifdef _WIN32 выше
        );
        config.user = getEnvOrDefault("TEST_DB_USER", "SYSDBA");
        config.password = getEnvOrDefault("TEST_DB_PASSWORD", "masterkey");
        return config;
    }

private:
    static std::string getEnvOrDefault(const char* name, const char* def) {
        const char* val = std::getenv(name);
        return (val && val[0]) ? val : def;
    }
};
```

---

## Когда тестировать обязательно

| Область | Обязательно тестировать | Почему |
|---------|------------------------|--------|
| SQL-запросы | Параметризация, правильность WHERE | Безопасность + корректность |
| Бизнес-логика | Расчёты, правила, трансформации | Ошибки стоят денег |
| Парсинг данных | XML, JSON, форматы файлов | Crash при невалидных данных |
| Авторизация | Проверка прав доступа | Безопасность |
| Миграции | Применение и откат схемы | Потеря данных невосполнима |
| Критичные пути | Открытие документа, сохранение | Базовая функциональность |

## Когда можно пропустить

| Область | Можно не тестировать сейчас | Почему |
|---------|-----------------------------|--------|
| wxWidgets UI | Рендеринг диалогов, wxGrid | Визуально проверяется |
| Конфиги | Чтение INI файлов | Проверяется при запуске |
| Тривиальные геттеры/сеттеры | `getName()`, `setName()` | Нет логики |
| Сгенерированный код | Designer-сгенерированные формы | Проверяется интеграционно |

---

## Test-first: для багфиксов

Для каждого бага сначала пишем тест, воспроизводящий проблему:

```cpp
// 1. Написать тест, который ПАДАЕТ (воспроизводит баг)
TEST_F(OrderServiceTest, PromoCodeAppliedToTotalNotSubtotal) {
    // Баг: промокод применялся к subtotal, игнорируя доставку
    Order order;
    order.setSubtotal(10000);  // 100 руб
    order.setShipping(500);    // 5 руб
    // total = 10500

    PromoCode promo{"SALE15", 15};  // 15% скидка

    // Тест падает — баг найден
    order.applyPromoCode(promo);
    EXPECT_EQ(order.getTotal(), 8925);  // 15% от 10500 = 1575, итого 8925
}

// 2. Исправить код, чтобы тест ПРОШЁЛ

// 3. Убедиться что другие тесты не сломались
// ctest --test-dir build --output-on-failure

// 4. Коммит: "fix: apply promo code to order total including shipping"
```

Этот подход гарантирует, что баг не вернётся в будущем.

---

## CI: тесты на каждый PR

### GitHub Actions

```yaml
# .github/workflows/test.yml
name: Tests

on:
  pull_request:
    branches: [master, dev]

jobs:
  test-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Cache vcpkg
        uses: actions/cache@v4
        with:
          path: C:\vcpkg\installed
          key: vcpkg-${{ hashFiles('vcpkg.json') }}

      - name: Install dependencies
        run: vcpkg install --triplet x64-windows

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

      - name: Build
        run: cmake --build build --config Debug

      - name: Start Firebird
        run: |
          choco install firebird -y
          Start-Service FirebirdServerDefaultInstance

      - name: Run tests
        run: ctest --test-dir build -C Debug --output-on-failure
        env:
          TEST_DB_HOST: localhost
          TEST_DB_PORT: 3050
          TEST_DB_PATH: C:\Temp\oes_test.fdb
          TEST_DB_USER: SYSDBA
          TEST_DB_PASSWORD: masterkey

  test-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            build-essential cmake \
            libgtest-dev \
            firebird3.0-server \
            libfbclient2 libib-util

      - name: Configure Firebird for CI
        run: |
          # Установить пароль SYSDBA (по умолчанию после установки пакета он случайный)
          sudo systemctl start firebird3.0
          echo "masterkey" | sudo /usr/bin/gsec -user SYSDBA -password masterkey \
            -modify SYSDBA -pw masterkey || true
          # Разрешить TCP-соединения (необходимо для ibDatabaseLayer)
          sudo sed -i 's/#RemoteServicePort/RemoteServicePort/' /etc/firebird/3.0/firebird.conf || true
          sudo systemctl restart firebird3.0

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

      - name: Build tests
        run: cmake --build build --config Debug

      - name: Run unit tests only (no DB required)
        run: ctest --test-dir build -R "unit_" --output-on-failure

      - name: Run integration tests
        run: ctest --test-dir build -R "integration_" --output-on-failure
        env:
          TEST_DB_HOST: localhost
          TEST_DB_PATH: /tmp/oes_test.fdb
          TEST_DB_USER: SYSDBA
          TEST_DB_PASSWORD: masterkey
```

### Правило: PR не мерджится если тесты падают

В настройках GitHub: Settings → Branches → Branch protection rules:
- Require status checks to pass before merging
- Выбрать checks "Tests / test-windows" и "Tests / test-linux"

---

## AI пишет тесты

AI хорошо подходит для генерации тестов, но человек должен проверить:

### Что AI делает хорошо

- Генерирует boilerplate (SetUp, TearDown, MOCK_METHOD)
- Покрывает стандартные сценарии (success, error, validation)
- Создаёт структуру TEST_F/TEST

### Что человек проверяет

1. **Edge cases** — AI часто пропускает граничные случаи:
   - Пустые строки, nullptr, нулевые значения
   - Максимальные значения (INTEGER overflow)
   - Многопоточные сценарии
   - Ошибки подключения к БД

2. **Реальная проверка** — тест действительно проверяет поведение:
```cpp
// ПЛОХОЙ тест — проверяет что Mock вызван, а не результат
TEST_F(UserServiceTest, BadTest) {
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    EXPECT_CALL(*mockDb, PrepareStatement(_)).Times(1).WillOnce(Return(mockStmt.get()));
    service->findById(42);
    // Этот тест НИЧЕГО не проверяет о возвращаемом значении!
}

// ХОРОШИЙ тест — проверяет поведение
TEST_F(UserServiceTest, GoodTest) {
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    auto mockRs   = std::make_unique<MockDatabaseResultSet>();
    EXPECT_CALL(*mockRs, Next()).WillOnce(Return(false));  // 0 строк — пустой результат
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(_)).WillOnce(Return(mockStmt.get()));

    auto user = service->findById(999);
    EXPECT_FALSE(user.has_value());  // Проверяем что вернулось
}
```

3. **Моки корректны** — AI может создать mock который всегда возвращает успех:
```cpp
// Нужно проверить что есть тесты на ошибочные сценарии
// ibBackendCoreException — реальное исключение OES для ошибок движка/БД
EXPECT_CALL(*mockDb, PrepareStatement(_))
    .WillOnce(Throw(ibBackendCoreException("Connection lost")));

EXPECT_THROW(service->findById(1), ibBackendCoreException);
```

---

## Naming convention

### Структура тестов

```cpp
// Что тестируем (класс или модуль)
class DiscountCalculatorTest : public ::testing::Test { };

// Имя теста: MethodName_Scenario_ExpectedBehavior
TEST_F(DiscountCalculatorTest, Apply_PercentDiscount_ReturnsCorrectAmount) { }
TEST_F(DiscountCalculatorTest, Apply_ZeroDiscount_ReturnOriginalPrice) { }
TEST_F(DiscountCalculatorTest, Apply_DiscountExceedsPrice_ReturnsZero) { }
TEST_F(DiscountCalculatorTest, Apply_InvalidDiscountType_ThrowsException) { }

// Или: should_ExpectedBehavior_when_Scenario (тоже допустимо)
TEST_F(DiscountCalculatorTest, ShouldReturnZero_WhenDiscountExceedsPrice) { }
```

### Правила именования

- Класс фикстуры: `<TestedClass>Test`
- Имя теста: описывает поведение, не реализацию
- Язык тестов: **English**
- Используйте `ASSERT_*` если продолжение теста бессмысленно при провале
- Используйте `EXPECT_*` если нужно продолжить проверки

### Файлы тестов

```
tests/
├── unit/
│   ├── test_discount_calculator.cpp    ← тестирует DiscountCalculator
│   ├── test_query_builder.cpp          ← тестирует QueryBuilder
│   ├── test_config_parser.cpp          ← тестирует ConfigParser
│   └── test_report_formatter.cpp       ← тестирует ReportFormatter
├── integration/
│   ├── test_firebird_crud.cpp          ← CRUD через реальный Firebird
│   ├── test_postgresql_crud.cpp        ← CRUD через реальный PostgreSQL
│   └── test_sqlite_crud.cpp            ← CRUD через реальный SQLite
├── mocks/
│   ├── MockDatabaseLayer.h
│   └── MockConfigProvider.h
└── helpers/
    ├── TestDbConfig.h                  ← Конфигурация тестовой БД
    └── TestDataFactory.h               ← Фабрики тестовых объектов
```

---

## Полезные утилиты для тестов

### Фабрики тестовых данных

```cpp
// tests/helpers/TestDataFactory.h
#pragma once
#include "core/User.h"
#include "core/Order.h"
#include "core/Product.h"

namespace TestFactory {

inline User CreateTestUser(int id = 1,
                            const std::string& email = "test@example.com",
                            const std::string& name = "Test User") {
    User user;
    user.setId(id);
    user.setEmail(email);
    user.setName(name);
    user.setRole(UserRole::Standard);
    return user;
}

inline Product CreateTestProduct(int id = 1,
                                   const std::string& name = "Test Product",
                                   int priceInCents = 9990) {
    Product product;
    product.setId(id);
    product.setName(name);
    product.setPrice(Money{priceInCents});
    product.setActive(true);
    return product;
}

inline Order CreateTestOrder(int id = 1) {
    Order order;
    order.setId(id);
    order.setUser(CreateTestUser());
    order.addItem(CreateTestProduct(), 1);
    return order;
}

} // namespace TestFactory
```

```cpp
// Использование в тесте
TEST_F(OrderServiceTest, ApplyVipDiscount) {
    auto user = TestFactory::CreateTestUser(1, "vip@test.com");
    user.setRole(UserRole::VIP);

    auto product = TestFactory::CreateTestProduct(1, "Premium", 10000);

    auto price = service->calculatePriceForUser(product, user);
    EXPECT_EQ(price.cents(), 8000);  // 20% VIP скидка
}
```

### Кастомные матчеры

```cpp
// tests/matchers/DatabaseMatchers.h
#include <gmock/gmock.h>

// Матчер: SQL содержит параметр, не конкатенацию
MATCHER_P(SqlUsesParameter, paramPlaceholder,
          "SQL query uses parameterized placeholder") {
    return arg.find(paramPlaceholder) != std::string::npos &&
           arg.find("'" ) == std::string::npos;  // Нет литеральных строк в SQL
}

// Использование
EXPECT_CALL(*mockDb, Execute(SqlUsesParameter("?"), _))
    .Times(1);
```

---

## Покрытие кода (Code Coverage)

```cmake
# CMakeLists.txt — включить coverage для Debug сборки
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(oes_tests PRIVATE --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(oes_tests PRIVATE --coverage)
endif()
```

```bash
# Генерация отчёта покрытия (Linux, gcov + lcov)
cmake --build build --config Debug
ctest --test-dir build

# Собрать данные покрытия
lcov --capture --directory build --output-file coverage.info
lcov --remove coverage.info '/usr/*' 'tests/*' --output-file coverage.info

# HTML отчёт
genhtml coverage.info --output-directory coverage_html
# Открыть coverage_html/index.html в браузере
```

#### Покрытие на Windows (OpenCppCoverage + MSVC)

На Windows gcov/lcov недоступны. Используйте **OpenCppCoverage** — бесплатный инструмент с поддержкой MSVC и Visual Studio.

```powershell
# Установка через Chocolatey
choco install opencppcoverage -y

# Запуск тестов с замером покрытия
OpenCppCoverage.exe `
    --sources src\ `
    --excluded_sources tests\ `
    --export_type html:coverage_html `
    -- ctest.exe --test-dir build -C Debug --output-on-failure

# Открыть coverage_html\index.html
```

В GitHub Actions (Windows runner):
```yaml
- name: Coverage (Windows)
  run: |
    choco install opencppcoverage -y
    OpenCppCoverage.exe --sources src\ --export_type cobertura:coverage.xml `
      -- ctest.exe --test-dir build -C Debug --output-on-failure
- name: Upload coverage report
  uses: codecov/codecov-action@v4
  with:
    files: coverage.xml
```

**Цели по покрытию:**
- Бизнес-логика (core/) — минимум 70%
- DBLayer — минимум 60%
- UI (wxWidgets) — не тестируем автоматически
