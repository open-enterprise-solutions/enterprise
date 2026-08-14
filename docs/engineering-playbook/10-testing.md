# 10. Testing

## Philosophy

Tests are not bureaucracy — they protect against regressions. OES historically had no tests, which is a risk during every refactor. We introduce testing gradually, starting with critical business logic. One good test is worth more than ten formal ones.

**Strategy:** new code — covered by tests right away. Old code — covered when it's refactored or when a bug is fixed.

---

## Current test suite (as built)

> This section is **ground truth** — it describes the suite that actually exists
> in `enterprise/tests/`. The rest of this document is guidance and templates
> (some examples reference illustrative classes, not real OES code).

Google Test, fetched via CMake `FetchContent` (v1.14.0), gated behind
`option(BUILD_TESTING)`. ~850 tests across one main target plus a few isolated
targets. Tests are flat `test_*.cpp` files compiled into the targets below;
there is no `unit/` / `integration/` / `mocks/` directory split.

### Build & run (Windows)

`cmake` is not on `PATH` — use the copy bundled with Visual Studio 2022 (locate
the VS install with `vswhere -latest -property installationPath`):

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
# the build dir is already configured at enterprise/build (VS17 2022, BUILD_TESTING=ON; gtest already fetched)
& $cmake --build enterprise/build --config Debug --target oes_tests
& "enterprise/build/bin/Debug/oes_tests.exe"                       # run all
& "enterprise/build/bin/Debug/oes_tests.exe" --gtest_filter=Md5*   # one suite
```

Editing a backend header recompiles backend (slow); editing a single test file
relinks fast. Tests define `OES_TESTING`, which opens `ib::AppDataCtorToken` so a
test can build app-owned subsystems directly without a full `ibApplicationData`
bring-up.

### Targets

| Target | Scope |
|---|---|
| `oes_tests` | the main suite (unit + in-process integration) |
| `oes_temp_db_sqlite_test` | L3-door integration on embedded SQLite (brings up appData + a SQLite `:memory:` pool; `GTEST_SKIP`s if the headless env can't start) |
| `oes_query_parity_test` / `oes_query_join_parity_test` | RAM-core vs live SQLite parity (NULL three-valued logic, joins) |
| `oes_query_totals_test` | TOTALS-tree fold, RAM-only |
| `oes_string_tests` | `ibString`/`fstring` only (links wxBase, not backend) |

The isolated targets are kept out of `oes_tests` so an appData bring-up (or an
optional driver being OFF) can't break the main run.

### Frontend GUI tests (`oes_frontend_runtime_test`)

A separate CMake target links **frontend.dll** (desktop wx) + backend into a
gtest binary — the first target to build the frontend under CMake on Windows
(the desktop DLL had only ever been built via the `.sln`). Files:
`tests/frontendFix.h` (GUI wxApp + runtime appData env + SQLite `:memory:` pool),
`tests/frontendFormFix.h` (adds a minimal bound main frame so
`ibSession::CurrentFrame()` resolves), and
`test_frontend{Runtime,Form,DocView,VisualHost,Controls}.cpp`.

Key facts:

- Desktop controls are real `wxWindow` (`ibVisualHost : wxScrolledCanvas`), so
  the harness brings up a live GUI `wxApp` via `wxEntryStart` — not just wxBase.
- **Headless must suppress modal dialogs.** A wxASSERT or a CRT heap/assert
  report in a Debug build otherwise pops a blocking window and hangs the run.
  The GUI environment routes them to stderr: `wxSetAssertHandler(nullptr)`,
  `_CrtSetReportMode(..., _CRTDBG_MODE_FILE)` + `_CRTDBG_FILE_STDERR`, and
  `SetErrorMode(SEM_NOGPFAULTERRORBOX | ...)`.
- A form **cannot** be `new`ed — `ibValueForm`'s ctor needs frame/session
  context and throws "Context functions are not available!". Build forms with
  `ibBackendValueForm::CreateNewForm()` on `FrontendFormFix`.
- On a headless CI box with no display the fixtures `GTEST_SKIP` rather than
  fail.

**Green as of 2026-08-02: 26/26 in ~2 s.** The 17 `DISABLED_` tests were re-enabled once
the cause turned out to be two bugs in the tests rather than one in the engine. Three more
things had to be fixed before the target would even build or run at a sane speed — worth
reading, because each had been invisible precisely because nobody built this target.

**It did not compile at all.** The harness had drifted behind the API: `ibFrontendMainFrame`
now takes an `ibSessionHolder&&` (the session comes in with the window and the frame wires
the back-link itself), `ibGUISession::AttachFrame` is gone, and `g_controlButtonCLSID`
became a global in `widgets.h` — so the test's own local copy made every use ambiguous.

**Every form test cost ~30 seconds.** The fixture went through the registered session path,
which makes the registry own `sys_session` I/O; this harness runs on a bare SQLite
`:memory:` database with no system schema, so each session failed its INSERT on a missing
table and paid a connection timeout for a row no assertion reads. The fix is to **imitate
the session, not create the table**: `ibSessionRegistry::MintUnlisted` hands back a session
the registry never takes in — no row, no cluster refresh, no disconnect audit — the same
shape a background job's rented read uses. **573 s → 2 s for the suite.** (`SetUnlisted` is
private with a short friend list, which is why this is a registry factory and not a call in
the test: the registry owns the listing rule.)

The two test bugs proper:

- **A control needs a PARENT.** The tests called `form->NewObject(clsid)` with no
  parent, and `ibValueFrame::Init` only calls `AddChild` when it gets one — so the
  control was built but belonged to no tree: absent from `GetControlList`, invisible
  to the visual-host walker, and everything downstream that assumes a parented
  control went off the map. Pass the form (or a container control):
  `form->NewObject(clsid, form)`. This is not a test-only nicety — it is how a real
  form builds its tree.
- **A document must be heap-allocated.** `ibDocument::OnChangedViewList` does
  `delete this` when the last view detaches (a document exists only while something
  views it), and the doc/view test held one on the STACK. It now allocates on the
  heap and attaches two views, so removing one leaves the document alive and
  observable.

Found alongside: the `Init` gate checked `lSizeArray < 2` while reading
`paParams[2]` — an out-of-bounds read that never fired only because the single live
caller passes 3. Fixed to `< 3`.

```powershell
& $cmake --build enterprise/build --config Debug --target oes_frontend_runtime_test
& "enterprise/build/bin/Debug/oes_frontend_runtime_test.exe"
```

### What is covered

- **Crypto / auth:** PBKDF2 password hashing (+ legacy-MD5 upgrade), MD5 and
  SHA-256 known vectors.
- **Core value system:** `ibNumber` (exact decimal, 60+ cases), `ibValue`
  (primitives, NULL vs EMPTY, coercion, hash key, const-ref), `ibValueArray` /
  `ibValueContainer` (Map) / `ibValueStructure`, the geometric value types.
- **Utilities / types:** `stringUtils`, `ibGuid`, `ibUniqueKey`, `clsid` (FNV-1a),
  `ibRowValues` (flat map), the binary wire codec (`ibReaderMemory`/`ibWriterMemory`),
  `ibTypeDescription` / `ibMetaDescription`, the dialect dictionary,
  `ibRawDBColumn` / column layout.
- **Query stack:** L2 renderer, L4 lexer/parser/rewrite, predicate / column-expr
  tree construction, the LINQ method table (name <-> enum lock-step), TOTALS, the
  composer, DB/RAM parity, the lambda recorder, the column codec.
- **DB / transactions:** the cross-driver transaction contract on embedded SQLite
  (read-your-writes, nested commit, inner-rollback-poison); the connection-pool
  lifecycle; the metadata-serialization byte-for-byte round trip; the audit
  logger; the Firebird lease.
- **L3-door integration (SQLite):** write-door round trip (Insert -> read ->
  Delete, with read-your-writes), value fidelity through the read door,
  `WhereLike` / `Where` / `WhereCompare` filters, and `Sum`/`Min`/`Max`/`Count`
  aggregates.
- **Register totals — NUMERIC parity (SQLite, added 2026-08-02):**
  `test_totalsNumericParity.cpp` installs the REAL rendered maintenance bundle
  (`RenderMaterialization` -> `Apply`) on an in-memory database and compares the
  trigger-kept totals against the movements re-aggregated directly — the same
  key-by-key, both-directions check `ibDerivedState::VerifyLastPeriod` makes, with
  the live aggregation as the oracle. Covers accumulation, month truncation,
  updates (quantity / side / across both key columns), deletes, backdated entries,
  fractional values and a mixed run; also pins the storage behaviour that is NOT a
  disagreement — an emptied key leaves a zero row, not a missing one.
- **Form binding — the hop walk:** `test_sourceDescription` (path passport +
  metadata-free serialize), `test_tabularHop` (the table starts the walk),
  `test_sourceExplorer` (design-time `WalkColumns`), and `test_sourceHopChain`
  (added 2026-08-02 — the RUNTIME chain table -> reference -> field at one and two
  hops, plus the non-owning-cell rule: a cell holding a source must be a
  `TYPE_CONST_REFFER` and must outlive it).
- **Serialization providers:** the binary provider's byte-identical round trip
  (`test_dataNode`), and `test_jsonProvider` (added 2026-08-02) — what the JSON
  view preserves AND, deliberately, what it does not (Fields/Properties flatten,
  Date degrades to String, `TypeDesc` is synthetic), so the lossy-by-design
  boundary is a decision on the record rather than a surprise.

### A test that reads the SOURCE, because no run can see this (added 2026-08-14)

`tests/test_propertySerialized.cpp` (in `oes_tests`) walks
`src/engine/backend/metaCollection/partial/`, and for every `<name>.h` that has a
`<name>Metadata.cpp` beside it, collects the `m_property*` identifiers the header DECLARES and
requires each to appear in that translation unit. No database, no session, no metadata — the sources
are read as text (`wxTextFile`), with the directory resolved from `__FILE__` rather than the cwd (the
same trick `test_scriptCorpus.cpp` uses for its corpus, since CMake runs the binary from the build
tree).

**Why text and not behaviour: a property written nowhere round-trips perfectly.** Both sides simply
lack it, so a byte-for-byte serialisation round trip agrees, and so does any test of what the object
does in memory. The fact being checked is "somebody wrote this line", which no amount of running the
code can establish.

The cost that bought it: the accounting register's `Correspondence` and `SplitTotals` were declared,
edited, and used to BUILD THE SCHEMA, but named in neither `ReadData` nor `WriteData`. They came back
at their defaults whenever the saved configuration was re-read — and that re-read produces the
BASELINE the next apply diffs against, so a switch turned OFF applied, returned ON, and the next
apply compared ON with ON and emitted nothing. It looked intermittent, because breakage depended on
which way the setting happened to differ from the default
([../register-shared-machinery.md § 4d](../register-shared-machinery.md)).

Two details worth copying into any test of this shape:

- **The exclusions are named, not guessed.** Properties whose VALUE is not part of the configuration
  are skipped: `*DefForm*` (default-form bindings, stored by id through their own path) and
  `*Module*` (they carry the module object, not a value).
- **It asserts that it checked something** — `EXPECT_GT(checked, 0u)` with "the layout must have
  moved". A source-reading test whose directory is renamed out from under it otherwise passes by
  finding nothing, which is the one failure mode this class of test has.

### Fixtures & doubles actually in use

- `MockDatabaseLayer` (`tests/mock_database_layer.h`) — minimal `ibDatabaseLayer`
  for connection-pool lifecycle tests.
- `TempDbSqliteFix` (in `test_tempDbSqlite.cpp`) — brings up the global appData
  env + a SQLite `:memory:` pool (maxSize=1) so the L3 door / temp-table manager
  runs end-to-end; `GTEST_SKIP`s when the headless env is unavailable.
- `TestCol` / `TestQueryable` — minimal `ibBackendQueryColumn` /
  `ibBackendQueryable` for the RAM-core and routing-gate tests (no metadata).

### Not yet covered (next)

- The write-door **Upsert** + **reference-as-key** (`_RTRef`/`_RRRef`) round trip
  needs a real metaobject (a queryable with a primary key and a reference
  column) — i.e. a small metadata fixture on SQLite; the temp-table harness has
  neither a PK nor reference columns.
- Multi-holder pool TX isolation (needs a file-backed DB; `:memory:` clones are
  separate databases).
- Sorted + paged reads through the door (the keyset cursor needs an identity
  sort the temp table does not provide).

---

## Test levels

```
          ┌─────────────────┐
          │   System / E2E   │  Few tests, slow, expensive
          │  (manual or      │  (currently — manual testing only)
          │  automation)     │
          ├─────────────────┤
          │  Integration     │  Mid count, mid speed
          │  (Google Test +  │  (tests against real SQLite/Firebird)
          │   real DB)       │
          ├─────────────────┤
          │    Unit          │  Many tests, fast, cheap
          │  (Google Test +  │  (isolated logic, mocks via
          │   Google Mock)   │   Google Mock)
          └─────────────────┘
```

---

## Tools

> **Current reality (OES repo):** Google Test is fetched via CMake
> `FetchContent` (GIT_TAG v1.14.0), **not** vcpkg or `find_package(GTest)`.
> Tests are flat files in `enterprise/tests/` (`test_*.cpp` + `bench_*.cpp`),
> compiled into a single `oes_tests` target — there is no `tests/unit` /
> `tests/integration` / `tests/mocks` split yet. DB-touching tests use the
> always-embedded SQLite and the dynamically-loaded Firebird/PG clients (no
> link-time DB dependency). The examples below show the recommended shape; the
> directory structure they imply is aspirational, not the current on-disk one.

### Google Test and Google Mock

```cmake
# CMakeLists.txt — wire up Google Test
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
        oes_backend     # src/engine/backend/ — ibDatabaseLayer, ibCompileCode, etc.
        oes_frontend    # src/engine/frontend/ — ibValueForm, etc.
)

# CTest integration
include(GoogleTest)
gtest_discover_tests(oes_tests)
```

### vcpkg for Google Test

```bash
# Installing via vcpkg
vcpkg install gtest:x64-windows
vcpkg install gtest:x64-linux

# Or via CMake FetchContent (no vcpkg)
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)
```

### CTest for running tests

```bash
# Build and run all tests
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

# Run with a filter
ctest --test-dir build -R "DBQueryBuilder"

# Run in parallel
ctest --test-dir build -j4 --output-on-failure

# Verbose output
ctest --test-dir build -V
```

---

## Unit tests

Test **one** class/function in isolation from its dependencies.

**What to test:**
- Business logic (calculations, rules, transformations)
- Utility classes
- Parsers and formatters
- Query builders
- Validation logic

### Test file structure

```cpp
// tests/unit/test_discount_calculator.cpp
#include <gtest/gtest.h>
#include "core/DiscountCalculator.h"

// Fixture (shared setUp for a test group)
class DiscountCalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        calculator = std::make_unique<DiscountCalculator>();
    }

    std::unique_ptr<DiscountCalculator> calculator;
};

// Test: happy path
TEST_F(DiscountCalculatorTest, ApplyPercentDiscount) {
    // Arrange
    Money price{10000};           // 100.00 RUB
    Discount discount{15, DiscountType::Percent};

    // Act
    Money result = calculator->apply(price, discount);

    // Assert
    EXPECT_EQ(result.cents(), 8500);  // 85.00 RUB
}

TEST_F(DiscountCalculatorTest, ApplyFixedDiscount) {
    Money price{10000};
    Discount discount{2000, DiscountType::Fixed};

    Money result = calculator->apply(price, discount);

    EXPECT_EQ(result.cents(), 8000);
}

TEST_F(DiscountCalculatorTest, DiscountCannotProduceNegativeTotal) {
    Money price{1000};
    Discount discount{5000, DiscountType::Fixed};  // Bigger than the price

    Money result = calculator->apply(price, discount);

    EXPECT_EQ(result.cents(), 0);  // Min 0, not negative
}

TEST_F(DiscountCalculatorTest, ThrowsOnInvalidDiscountType) {
    Money price{10000};
    Discount discount{10, static_cast<DiscountType>(999)};  // Invalid type

    EXPECT_THROW(calculator->apply(price, discount), std::invalid_argument);
}

// Parameterized tests
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
        std::make_tuple(10000, 0,   10000),  // 0% discount
        std::make_tuple(10000, 100, 0),      // 100% discount
        std::make_tuple(10000, 50,  5000),   // 50% discount
        std::make_tuple(1,     10,  0)       // Rounding down to 0
    )
);
```

### Google Mock to isolate dependencies

```cpp
// tests/mocks/MockDatabaseLayer.h
// ibDatabaseLayer (src/engine/backend/databaseLayer/databaseLayer.h) —
// the abstract base class for every OES DB backend.
// We mock it to isolate business logic from the real DB.
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

// UserService — example service that uses ibDatabaseLayer through MockDatabaseLayer.
// MockDatabaseLayer mocks ibDatabaseLayer::PrepareStatement().
// MockPreparedStatement mocks ibPreparedStatement.

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
    // Set expectation: PrepareStatement + SetParamInt + RunQuery
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

    EXPECT_CALL(*mockRs, Next()).WillOnce(Return(false));  // 0 rows
    EXPECT_CALL(*mockStmt, SetParamInt(1, 999));
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(_)).WillOnce(Return(mockStmt.get()));

    auto user = service->findById(999);

    EXPECT_FALSE(user.has_value());
}

TEST_F(UserServiceTest, SqlQueryDoesNotContainRawPassword) {
    // Security: the password is passed via SetParamString, not concatenated into SQL
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    auto mockRs   = std::make_unique<MockDatabaseResultSet>();
    wxString capturedSql;

    EXPECT_CALL(*mockRs, Next()).WillRepeatedly(Return(false));
    // Expect SetParamString — password must NOT appear in the SQL text
    EXPECT_CALL(*mockStmt, SetParamString(2, _));
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(::testing::SaveArg<0>(&capturedSql)))
        .WillOnce(Return(mockStmt.get()));

    service->authenticate("user@test.com", "mypassword");

    // SQL must not contain the password verbatim
    EXPECT_THAT(capturedSql.ToStdString(), Not(HasSubstr("mypassword")));
}
```

---

## Integration tests

Test **interactions with a real DB** — the full cycle: create/read/update/delete.

**What to test:**
- CRUD operations through DBLayer
- Transactions and rollbacks
- Schema application (migrations)
- Query performance

### Example integration test with Firebird

```cpp
// tests/integration/test_firebird_crud.cpp
#include <gtest/gtest.h>
#include "engine/backend/databaseLayer/firebird/databaseLayerFirebird.h"
#include "TestDbConfig.h"  // TestDbConfig::Get() — test DB config

// Fixture with a real DB connection
class FirebirdCrudTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Create the test DB once for the whole suite
        // ibDatabaseLayerFirebird — concrete ibDatabaseLayer implementation for Firebird
        s_db = std::make_shared<ibDatabaseLayerFirebird>();
        ASSERT_TRUE(s_db->Open(TestDbConfig::GetPath(),
                               TestDbConfig::GetUser(),
                               TestDbConfig::GetPassword()));
        // RECREATE TABLE — Firebird-equivalent of "DROP IF EXISTS + CREATE"
        // (Firebird does not support CREATE TABLE IF NOT EXISTS)
        ASSERT_TRUE(s_db->Execute(R"(
            RECREATE TABLE TEST_USERS (
                ID INTEGER NOT NULL PRIMARY KEY,
                EMAIL VARCHAR(255) NOT NULL,
                NAME VARCHAR(255)
            )
        )").IsSuccess());
    }

    static void TearDownTestSuite() {
        s_db->Execute("DROP TABLE TEST_USERS");  // Firebird: no IF EXISTS for DROP TABLE
        s_db->Disconnect();
    }

    void SetUp() override {
        // Clean up data before each test
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
    // ibTransactionGuard — RAII, rolls back on scope exit without Commit()
    {
        ibTransactionGuard tx(s_db.get());

        ibPreparedStatement* stmt = s_db->PrepareStatement(
            "INSERT INTO TEST_USERS (ID, EMAIL) VALUES (?, ?)"
        );
        stmt->SetParamInt(1, 1);
        stmt->SetParamString(2, "first@test.com");
        stmt->RunQuery();

        // tx is destroyed without Commit() — automatic rollback
    }

    // Confirm the row is not inserted
    ibPreparedStatement* countStmt = s_db->PrepareStatement(
        "SELECT COUNT(*) FROM TEST_USERS"
    );
    ibDatabaseResultSet* rs = countStmt->RunQuery();
    ASSERT_TRUE(rs && rs->Next());
    EXPECT_EQ(rs->GetResultInt(0), 0);
}

TEST_F(FirebirdCrudTest, ParametrizedQueryPreventsInjection) {
    // Attempt SQL injection through a parameter
    wxString maliciousInput = "'; DROP TABLE TEST_USERS; --";

    ibPreparedStatement* stmt = s_db->PrepareStatement(
        "SELECT * FROM TEST_USERS WHERE EMAIL = ?"
    );
    stmt->SetParamString(1, maliciousInput);
    ibDatabaseResultSet* rs = stmt->RunQuery();

    // The query must succeed (no rows found, no crash)
    ASSERT_TRUE(rs != nullptr);
    EXPECT_FALSE(rs->Next());  // 0 rows — injection did not work

    // The table must still exist
    ibPreparedStatement* checkStmt = s_db->PrepareStatement(
        "SELECT COUNT(*) FROM TEST_USERS"
    );
    ibDatabaseResultSet* checkRs = checkStmt->RunQuery();
    EXPECT_TRUE(checkRs != nullptr);
}
```

### Test DB configuration

```cpp
// tests/TestDbConfig.h
#pragma once
#include "database/DatabaseConfig.h"
#include <cstdlib>
#include <string>

// Platform-dependent default path to the test DB:
#ifdef _WIN32
#   define TEST_DB_DEFAULT_PATH "C:\\Temp\\oes_test.fdb"
#else
#   define TEST_DB_DEFAULT_PATH "/tmp/oes_test.fdb"
#endif

class TestDbConfig {
public:
    static DatabaseConfig Get() {
        DatabaseConfig config;
        // Read from env vars (for CI/CD) or fall back to defaults
        config.type = DatabaseType::Firebird;
        config.host = getEnvOrDefault("TEST_DB_HOST", "localhost");
        config.port = std::stoi(getEnvOrDefault("TEST_DB_PORT", "3050"));
        config.database = getEnvOrDefault(
            "TEST_DB_PATH",
            TEST_DB_DEFAULT_PATH  // defined via #ifdef _WIN32 above
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

## When testing is mandatory

| Area | Must be tested | Why |
|---------|------------------------|--------|
| SQL queries | Parameterization, correctness of WHERE | Security + correctness |
| Business logic | Calculations, rules, transformations | Errors cost money |
| Data parsing | XML, JSON, file formats | Crash on invalid data |
| Authorization | Permission checks | Security |
| Migrations | Schema apply and rollback | Data loss is irreversible |
| Critical paths | Opening a document, saving | Core functionality |

## When you can skip it

| Area | Can be skipped for now | Why |
|---------|-----------------------------|--------|
| wxWidgets UI | Dialog rendering, wxGrid | Verified visually |
| Configs | Reading INI files | Verified at startup |
| Trivial getters/setters | `getName()`, `setName()` | No logic |
| Generated code | Designer-generated forms | Verified by integration tests |

---

## Test-first: for bug fixes

For every bug, write a test that reproduces the problem first:

```cpp
// 1. Write a test that FAILS (reproduces the bug)
TEST_F(OrderServiceTest, PromoCodeAppliedToTotalNotSubtotal) {
    // Bug: promo code was applied to subtotal, ignoring shipping
    Order order;
    order.setSubtotal(10000);  // 100 RUB
    order.setShipping(500);    // 5 RUB
    // total = 10500

    PromoCode promo{"SALE15", 15};  // 15% discount

    // Test fails — bug found
    order.applyPromoCode(promo);
    EXPECT_EQ(order.getTotal(), 8925);  // 15% of 10500 = 1575, total 8925
}

// 2. Fix the code so the test PASSES

// 3. Make sure other tests didn't break
// ctest --test-dir build --output-on-failure

// 4. Commit: "fix: apply promo code to order total including shipping"
```

This approach ensures the bug does not return in the future.

---

## CI: tests on every PR

### GitHub Actions

```yaml
# .github/workflows/test.yml
name: Tests

on:
  pull_request:
    branches: [master, develop]

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
          # Set SYSDBA password (random by default after package install)
          sudo systemctl start firebird3.0
          echo "masterkey" | sudo /usr/bin/gsec -user SYSDBA -password masterkey \
            -modify SYSDBA -pw masterkey || true
          # Allow TCP connections (needed for ibDatabaseLayer)
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

### Rule: a PR is not merged if tests fail

In GitHub settings: Settings → Branches → Branch protection rules:
- Require status checks to pass before merging
- Select the checks "Tests / test-windows" and "Tests / test-linux"

---

## AI writes tests

AI is a good fit for generating tests, but a human must verify:

### What AI does well

- Generates boilerplate (SetUp, TearDown, MOCK_METHOD)
- Covers standard scenarios (success, error, validation)
- Builds the TEST_F/TEST structure

### What the human verifies

1. **Edge cases** — AI often misses boundary cases:
   - Empty strings, nullptr, zero values
   - Maximum values (INTEGER overflow)
   - Multithreaded scenarios
   - DB connection errors

2. **Real verification** — the test actually checks behaviour:
```cpp
// BAD test — checks that the Mock was called, not the result
TEST_F(UserServiceTest, BadTest) {
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    EXPECT_CALL(*mockDb, PrepareStatement(_)).Times(1).WillOnce(Return(mockStmt.get()));
    service->findById(42);
    // This test checks NOTHING about the return value!
}

// GOOD test — checks behaviour
TEST_F(UserServiceTest, GoodTest) {
    auto mockStmt = std::make_unique<MockPreparedStatement>();
    auto mockRs   = std::make_unique<MockDatabaseResultSet>();
    EXPECT_CALL(*mockRs, Next()).WillOnce(Return(false));  // 0 rows — empty result
    EXPECT_CALL(*mockStmt, RunQuery()).WillOnce(Return(mockRs.get()));
    EXPECT_CALL(*mockDb, PrepareStatement(_)).WillOnce(Return(mockStmt.get()));

    auto user = service->findById(999);
    EXPECT_FALSE(user.has_value());  // Verify what was returned
}
```

3. **Mocks are correct** — AI may produce a mock that always returns success:
```cpp
// Make sure there are tests for failure scenarios
// ibBackendCoreException — the real OES exception for engine/DB errors
EXPECT_CALL(*mockDb, PrepareStatement(_))
    .WillOnce(Throw(ibBackendCoreException("Connection lost")));

EXPECT_THROW(service->findById(1), ibBackendCoreException);
```

---

## Naming convention

### Test structure

```cpp
// What we're testing (class or module)
class DiscountCalculatorTest : public ::testing::Test { };

// Test name: MethodName_Scenario_ExpectedBehavior
TEST_F(DiscountCalculatorTest, Apply_PercentDiscount_ReturnsCorrectAmount) { }
TEST_F(DiscountCalculatorTest, Apply_ZeroDiscount_ReturnOriginalPrice) { }
TEST_F(DiscountCalculatorTest, Apply_DiscountExceedsPrice_ReturnsZero) { }
TEST_F(DiscountCalculatorTest, Apply_InvalidDiscountType_ThrowsException) { }

// Or: should_ExpectedBehavior_when_Scenario (also acceptable)
TEST_F(DiscountCalculatorTest, ShouldReturnZero_WhenDiscountExceedsPrice) { }
```

### Naming rules

- Fixture class: `<TestedClass>Test`
- Test name: describes behaviour, not implementation
- Test language: **English**
- Use `ASSERT_*` if continuing the test makes no sense on failure
- Use `EXPECT_*` if you want to keep checking

### Test files

```
tests/
├── unit/
│   ├── test_discount_calculator.cpp    ← tests DiscountCalculator
│   ├── test_query_builder.cpp          ← tests QueryBuilder
│   ├── test_config_parser.cpp          ← tests ConfigParser
│   └── test_report_formatter.cpp       ← tests ReportFormatter
├── integration/
│   ├── test_firebird_crud.cpp          ← CRUD through real Firebird
│   ├── test_postgresql_crud.cpp        ← CRUD through real PostgreSQL
│   └── test_sqlite_crud.cpp            ← CRUD through real SQLite
├── mocks/
│   ├── MockDatabaseLayer.h
│   └── MockConfigProvider.h
└── helpers/
    ├── TestDbConfig.h                  ← test DB configuration
    └── TestDataFactory.h               ← test object factories
```

---

## Helpful utilities for tests

### Test data factories

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
// Using in a test
TEST_F(OrderServiceTest, ApplyVipDiscount) {
    auto user = TestFactory::CreateTestUser(1, "vip@test.com");
    user.setRole(UserRole::VIP);

    auto product = TestFactory::CreateTestProduct(1, "Premium", 10000);

    auto price = service->calculatePriceForUser(product, user);
    EXPECT_EQ(price.cents(), 8000);  // 20% VIP discount
}
```

### Custom matchers

```cpp
// tests/matchers/DatabaseMatchers.h
#include <gmock/gmock.h>

// Matcher: SQL uses a parameter, not concatenation
MATCHER_P(SqlUsesParameter, paramPlaceholder,
          "SQL query uses parameterized placeholder") {
    return arg.find(paramPlaceholder) != std::string::npos &&
           arg.find("'" ) == std::string::npos;  // No literal strings in SQL
}

// Usage
EXPECT_CALL(*mockDb, Execute(SqlUsesParameter("?"), _))
    .Times(1);
```

---

## Code coverage

```cmake
# CMakeLists.txt — enable coverage for the Debug build
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(oes_tests PRIVATE --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(oes_tests PRIVATE --coverage)
endif()
```

```bash
# Generate a coverage report (Linux, gcov + lcov)
cmake --build build --config Debug
ctest --test-dir build

# Collect coverage data
lcov --capture --directory build --output-file coverage.info
lcov --remove coverage.info '/usr/*' 'tests/*' --output-file coverage.info

# HTML report
genhtml coverage.info --output-directory coverage_html
# Open coverage_html/index.html in a browser
```

#### Coverage on Windows (OpenCppCoverage + MSVC)

On Windows gcov/lcov aren't available. Use **OpenCppCoverage** — a free tool with MSVC and Visual Studio support.

```powershell
# Install via Chocolatey
choco install opencppcoverage -y

# Run tests with coverage instrumentation
OpenCppCoverage.exe `
    --sources src\ `
    --excluded_sources tests\ `
    --export_type html:coverage_html `
    -- ctest.exe --test-dir build -C Debug --output-on-failure

# Open coverage_html\index.html
```

In GitHub Actions (Windows runner):
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

**Coverage targets:**
- Business logic (core/) — at least 70%
- DBLayer — at least 60%
- UI (wxWidgets) — not tested automatically
