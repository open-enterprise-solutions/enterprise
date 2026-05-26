// =============================================================================
// OES Enterprise — ibConnectionPool unit tests
//
// Exercises the pool's public lifecycle on a directly-constructed instance
// (no global ibApplicationData required). Concurrency / Checkout-deleter
// behaviour is integration-test scope — needs real holders + sessions wired
// up against a live driver — and is not covered here.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "mock_database_layer.h"

// ---------------------------------------------------------------------------
// Lifecycle: IsInitialised flips on Init, off on Shutdown
// ---------------------------------------------------------------------------

TEST(ConnectionPool, IsInitialisedFalseBeforeInit) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    EXPECT_FALSE(pool.IsInitialised());
}

TEST(ConnectionPool, IsInitialisedAfterInit) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, /*maxSize=*/8, /*minIdle=*/2);
    EXPECT_TRUE(pool.IsInitialised());
    pool.Shutdown();
}

TEST(ConnectionPool, ShutdownClearsInitialised) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, 8, 2);
    pool.Shutdown();
    EXPECT_FALSE(pool.IsInitialised());
}

// ---------------------------------------------------------------------------
// Sizing: minIdle pre-warms, maxSize caps
// ---------------------------------------------------------------------------

TEST(ConnectionPool, MaxAndMinIdleConfigured) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, /*maxSize=*/16, /*minIdle=*/4);
    EXPECT_EQ(pool.MaxSize(), 16u);
    EXPECT_EQ(pool.MinIdle(), 4u);
    pool.Shutdown();
}

TEST(ConnectionPool, LiveSizeAtLeastMinIdleAfterInit) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, /*maxSize=*/8, /*minIdle=*/3);
    // Init should pre-warm to at least minIdle entries (master + clones).
    EXPECT_GE(pool.LiveSize(), 3u);
    pool.Shutdown();
}

TEST(ConnectionPool, LiveSizeNeverExceedsMax) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, /*maxSize=*/2, /*minIdle=*/0);
    EXPECT_LE(pool.LiveSize(), 2u);
    pool.Shutdown();
}

TEST(ConnectionPool, ShutdownIsIdempotent) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, 4, 1);
    pool.Shutdown();
    pool.Shutdown();   // second call must be a no-op, not a crash
    EXPECT_FALSE(pool.IsInitialised());
}

TEST(ConnectionPool, ReinitializeReplacesPrimary) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto first  = std::make_shared<MockDatabaseLayer>();
    auto second = std::make_shared<MockDatabaseLayer>();
    pool.Init(first, 4, 1);
    pool.Init(second, 6, 2);   // second Init wins
    EXPECT_EQ(pool.MaxSize(), 6u);
    EXPECT_EQ(pool.MinIdle(), 2u);
    pool.Shutdown();
}

// ---------------------------------------------------------------------------
// Idle / live counts symmetry
// ---------------------------------------------------------------------------

TEST(ConnectionPool, IdleSizeDoesNotExceedLive) {
    ibConnectionPool pool(ib::AppDataCtorToken{});
    auto primary = std::make_shared<MockDatabaseLayer>();
    pool.Init(primary, 8, 4);
    EXPECT_LE(pool.IdleSize(), pool.LiveSize());
    pool.Shutdown();
}
