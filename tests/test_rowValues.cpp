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

// swap exchanges the contents whole — what a table's rows are reordered by, where they stand.
TEST(RowValues, SwapExchangesContents) {
    Row a; a[1] = 10; a[2] = 20;
    Row b; b[7] = 70;
    a.swap(b);
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(a.at(7), 70);
    EXPECT_EQ(b.size(), 2u);
    EXPECT_EQ(b.at(2), 20);
}

// swap_sorted lays a whole row down at once and hands back what was there; the entries it is handed
// are already in key order, and afterwards the row answers exactly as one built entry by entry.
TEST(RowValues, SwapSortedTakesAWholeRowAndHandsBackTheOld) {
    Row r; r[9] = 90;
    Row::container_type whole{ {1, 10}, {4, 40}, {6, 60} };
    r.swap_sorted(whole);
    EXPECT_EQ(r.size(), 3u);
    EXPECT_EQ(r.find(9), r.end());              // the old contents are gone from the row…
    ASSERT_EQ(whole.size(), 1u);                 // …and are in the caller's hands
    EXPECT_EQ(whole[0].first, 9);
    EXPECT_EQ(r.at(4), 40);
    r[5] = 50;                                   // …and the order holds for what comes after
    std::vector<int> keys;
    for (const auto& kv : r) keys.push_back(kv.first);
    EXPECT_EQ(keys, (std::vector<int>{1, 4, 5, 6}));
}

// find_value answers with the value where it lies — writable through the pointer — or null.
TEST(RowValues, FindValuePointsAtTheValueOrIsNull) {
    Row r; r[2] = 20; r[8] = 80;
    int* v = r.find_value(8);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 80);
    *v = 81;
    EXPECT_EQ(r.at(8), 81);
    EXPECT_EQ(r.find_value(5), nullptr);         // between two keys
    EXPECT_EQ(r.find_value(99), nullptr);        // past the last
    const Row& c = r;
    EXPECT_EQ(*c.find_value(2), 20);
}

// Keys arriving out of order still land in order — the insert in the middle, not only at the end.
TEST(RowValues, InsertOrAssignInTheMiddleKeepsTheOrder) {
    Row r;
    r.insert_or_assign(5, 50);
    r.insert_or_assign(1, 10);
    const auto placed = r.insert_or_assign(3, 30);
    EXPECT_TRUE(placed.second);
    EXPECT_EQ(placed.first->first, 3);
    std::vector<int> keys;
    for (const auto& kv : r) keys.push_back(kv.first);
    EXPECT_EQ(keys, (std::vector<int>{1, 3, 5}));
}
