// =============================================================================
// OES Enterprise — ibRowValues tests
//
// ibRowValues<Key,T> (backend/rowValues.h) is the flat-map (sorted vector)
// std::map drop-in behind ibRowMetaValues = the keyed value set of every record
// object / table row (millions loaded). It must honour the std::map contract the
// codebase relies on: sorted iteration, at()-throws-on-miss, insert-rejects-dup,
// insert_or_assign-updates, and operator< (so it can be a std::map key). Pure.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include <stdexcept>
#include "backend/rowValues.h"

using Row = ibRowValues<int, int>;

TEST(RowValues, EmptyDefault) {
    Row r;
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.size(), 0u);
}

TEST(RowValues, SubscriptInsertsAndReads) {
    Row r;
    r[5] = 50;
    EXPECT_EQ(r.size(), 1u);
    EXPECT_EQ(r.at(5), 50);
}

TEST(RowValues, FindHitAndMiss) {
    Row r; r[1] = 10;
    EXPECT_NE(r.find(1), r.end());
    EXPECT_EQ(r.find(2), r.end());
}

TEST(RowValues, Count) {
    Row r; r[7] = 1;
    EXPECT_EQ(r.count(7), 1u);
    EXPECT_EQ(r.count(8), 0u);
}

TEST(RowValues, InsertRejectsDuplicate) {
    Row r;
    EXPECT_TRUE(r.insert({3, 30}).second);
    EXPECT_FALSE(r.insert({3, 99}).second);   // key already present
    EXPECT_EQ(r.at(3), 30);                    // original value kept
}

TEST(RowValues, InsertOrAssignUpdates) {
    Row r; r[3] = 30;
    r.insert_or_assign(3, 99);
    EXPECT_EQ(r.at(3), 99);
}

TEST(RowValues, Erase) {
    Row r; r[1] = 1; r[2] = 2;
    EXPECT_EQ(r.erase(1), 1u);
    EXPECT_EQ(r.size(), 1u);
    EXPECT_EQ(r.erase(99), 0u);                // missing key
}

TEST(RowValues, AtThrowsOnMiss) {
    Row r;
    EXPECT_THROW(r.at(42), std::out_of_range);
}

TEST(RowValues, SortedIterationRegardlessOfInsertOrder) {
    Row r; r[3] = 0; r[1] = 0; r[2] = 0;
    std::vector<int> keys;
    for (const auto& kv : r) keys.push_back(kv.first);
    EXPECT_EQ(keys, (std::vector<int>{1, 2, 3}));
}

TEST(RowValues, EqualityAndOrdering) {
    Row a; a[1] = 1; a[2] = 2;
    Row b; b[1] = 1; b[2] = 2;
    Row c; c[1] = 1; c[2] = 3;
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a < c);                         // lexicographic over sorted pairs
}

TEST(RowValues, ClearEmpties) {
    Row r; r[1] = 1;
    r.clear();
    EXPECT_TRUE(r.empty());
}
