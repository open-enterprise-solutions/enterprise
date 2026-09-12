// =============================================================================
// OES Enterprise — ibQueryRamTable: rows handed on, re-keyed and reordered
//
// The RAM table is where every stitch of a composed read lands: a union's
// branches, a nested query publishing its inner columns under its own, a sort's
// rebuild. Those stitches MOVE rows rather than copy them, and a move under new
// column ids has rules of its own — which cell lands where, what happens to a
// cell the list does not name, what a named cell the row never held means. They
// are pinned here, so the row container underneath can change without the
// stitch changing what it says.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/query/queryRamTable.h"

namespace {

enum : ibMetaID { kA = 1, kB = 2, kC = 3, kX = 11, kY = 12 };

ibQueryRamTable Table(std::initializer_list<ibMetaID> ids)
{
    ibQueryRamTable t;
    for (const ibMetaID id : ids)
        t.AddColumn(id, wxString::Format(wxT("c%d"), static_cast<int>(id)), ibTypeDescription());
    return t;
}

void Put(ibQueryRamTable& t, long row, ibMetaID id, const wxString& text) { t.SetCell(row, id, ibValue(text)); }

wxString Text(const ibQueryRamTable& t, long row, ibMetaID id) { return t.GetCell(row, id).GetString(); }

} // namespace

// The named cells land under their new ids; a cell the list does not name rides along under its own,
// and the source is left with no rows.
TEST(RamTableRekey, NamedCellsMoveAndRidersStay) {
    ibQueryRamTable src = Table({ kA, kB, kC });
    const long r = src.AppendRow();
    Put(src, r, kA, wxT("a"));
    Put(src, r, kB, wxT("b"));
    Put(src, r, kC, wxT("c"));

    ibQueryRamTable dst = Table({ kX, kY });
    dst.AppendRowsRekeyed(src, { { kA, kX }, { kB, kY } });

    ASSERT_EQ(dst.RowCount(), 1);
    EXPECT_EQ(Text(dst, 0, kX), wxT("a"));
    EXPECT_EQ(Text(dst, 0, kY), wxT("b"));
    EXPECT_EQ(Text(dst, 0, kC), wxT("c"));    // the rider, unread but carried
    EXPECT_EQ(dst.FindCell(0, kA), nullptr);   // moved away, not copied
    EXPECT_EQ(src.RowCount(), 0);
}

// Two pairs that trade ids must not meet each other half-way: A -> B with B -> A is a swap.
TEST(RamTableRekey, PairsThatTradeIdsSwap) {
    ibQueryRamTable src = Table({ kA, kB });
    const long r = src.AppendRow();
    Put(src, r, kA, wxT("a"));
    Put(src, r, kB, wxT("b"));

    ibQueryRamTable dst = Table({ kA, kB });
    dst.AppendRowsRekeyed(src, { { kA, kB }, { kB, kA } });

    EXPECT_EQ(Text(dst, 0, kA), wxT("b"));
    EXPECT_EQ(Text(dst, 0, kB), wxT("a"));
}

// An id a pair names as its TARGET belongs to that pair. A rider standing there gives way to the cell
// moved in — and when the pair's own cell was never in the row, the id reads empty: the rider does not
// answer for a column somebody else was asked for.
TEST(RamTableRekey, AClaimedIdIsNeverARiders) {
    ibQueryRamTable src = Table({ kA, kB, kX, kY });
    const long r = src.AppendRow();
    Put(src, r, kB, wxT("b"));             // kA is never set
    Put(src, r, kX, wxT("rider x"));
    Put(src, r, kY, wxT("rider y"));

    ibQueryRamTable dst = Table({ kX, kY });
    dst.AppendRowsRekeyed(src, { { kA, kX }, { kB, kY } });

    EXPECT_EQ(dst.FindCell(0, kX), nullptr);
    EXPECT_EQ(Text(dst, 0, kY), wxT("b"));
}

// Into a table that already holds rows, the new ones go after them, in their own order.
TEST(RamTableRows, HandedOnAfterWhatIsThere) {
    ibQueryRamTable dst = Table({ kA });
    Put(dst, dst.AppendRow(), kA, wxT("first"));
    ibQueryRamTable src = Table({ kA });
    Put(src, src.AppendRow(), kA, wxT("second"));
    Put(src, src.AppendRow(), kA, wxT("third"));

    dst.AppendRowsFrom(src);

    ASSERT_EQ(dst.RowCount(), 3);
    EXPECT_EQ(Text(dst, 0, kA), wxT("first"));
    EXPECT_EQ(Text(dst, 1, kA), wxT("second"));
    EXPECT_EQ(Text(dst, 2, kA), wxT("third"));
    EXPECT_EQ(src.RowCount(), 0);
}

// Rows reordered where they stand: row i becomes what was row order[i], and only the first `keep` stay.
TEST(RamTableRows, ReorderFollowsThePermutationAndKeepsTheHead) {
    ibQueryRamTable t = Table({ kA });
    for (const wxString& s : { wxString(wxT("r0")), wxString(wxT("r1")), wxString(wxT("r2")), wxString(wxT("r3")) })
        Put(t, t.AppendRow(), kA, s);

    t.ReorderRows({ 2, 0, 3, 1 }, 3);

    ASSERT_EQ(t.RowCount(), 3);
    EXPECT_EQ(Text(t, 0, kA), wxT("r2"));
    EXPECT_EQ(Text(t, 1, kA), wxT("r0"));
    EXPECT_EQ(Text(t, 2, kA), wxT("r3"));
}
