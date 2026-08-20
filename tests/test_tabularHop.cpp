// =============================================================================
// OES Enterprise — ibTabularDataObject hop-walk tests
//
// ibTabularDataObject (backend/tabularDataObject.h) is the TABLE side of the
// metadata-free hop walk. Its gate GetValueBySourceHop(item, hop, out) is a DIRECT
// translation (row cell by id); GetValueByPath STARTS the walk at the row (first
// hop) and TRANSFERS the deeper hops to ibSourceDataObject::ResolvePath — the SAME
// scalar walk the reference side uses. One hop vector, computation switches
// table -> source object. No model / DB / metadata involved.
//
// A MockTable supplies one row's cells (id -> ibValue) so the plumbing is exercised
// directly: the translation, the from-index windowing, and the switch to the source
// walk (which correctly refuses to dot into a primitive cell). Pure — no DB.
// =============================================================================

#include <gtest/gtest.h>
#include <map>
#include <vector>
#include "backend/tabularDataObject.h"     // ibTabularDataObject (gate + GetValueByPath)
#include "backend/tabularModelView.h"      // ibDataViewItem (the row handle the gate carries)
#include "backend/compiler/value.h"        // ibValue
#include "backend/sourceDescription.h"     // ibSourceHop

namespace {

// A mock table — ONE row's cells keyed by column id. GetValueBySourceHop is the pure DIRECT translation the
// real ibValueModel does (id -> cell value, no reference checks), so GetValueByPath's metadata-free plumbing
// is exercised without a model / DB / metadata.
class MockTable : public ibTabularDataObject {
    std::map<ibSourceId, ibValue> m_cells;
public:
    void SetCell(ibSourceId id, const ibValue& v) { m_cells[id] = v; }

    bool GetValueBySourceHop(const ibDataViewItem& /*item*/, const ibSourceHop& hop, ibValue& out) const override {
        auto it = m_cells.find(hop.m_id);
        if (it == m_cells.end())
            return false;                     // not a column of this row
        out = it->second;
        return true;                          // pure translation — hand the cell back
    }

    // ibTabularObject contract — trivial (a metadata-free walk never reads these).
    const ibValueMetaObjectCompositeData* GetSourceMetaObject() const override { return nullptr; }
    const ibMetaData* GetSourceMetaData() const override { return nullptr; }
    ibClassID GetSourceClassType() const override { return 0; }
};

} // namespace

// A single row-relative hop returns the cell straight back (from == last hop).
TEST(TabularHop, SingleHopReturnsCell) {
    MockTable t;
    t.SetCell(21, ibValue(wxString(wxT("Widget"))));
    ibDataViewItem row;    // the mock ignores the row handle
    ibValue out;
    EXPECT_TRUE(t.GetValueByPath(row, std::vector<ibSourceHop>{ {21} }, 0, out));
    EXPECT_TRUE(out.GetString() == wxT("Widget"));
}

// A hop whose id is not a column of the row does not resolve (a broken binding, not a crash).
TEST(TabularHop, MissingColumnFails) {
    MockTable t;
    t.SetCell(21, ibValue(42));
    ibDataViewItem row;
    ibValue out;
    EXPECT_FALSE(t.GetValueByPath(row, std::vector<ibSourceHop>{ {999} }, 0, out));
}

// The transition: the table STARTS the walk (first hop), then hands the rest to the source object. A cell that
// is a PRIMITIVE (nothing to step into) can't be dotted into — the deeper hop stops the walk, no crash. This
// proves the switch table -> ibSourceDataObject::ResolvePath (which refuses a non-source value mid-path).
TEST(TabularHop, CannotDotIntoPrimitiveCell) {
    MockTable t;
    t.SetCell(21, ibValue(wxString(wxT("Widget"))));   // a string cell — not a source
    ibDataViewItem row;
    ibValue out;
    EXPECT_FALSE(t.GetValueByPath(row, std::vector<ibSourceHop>{ {21}, {22} }, 0, out));
}

// Guards: an empty path, or a start index past the end, resolve to nothing (not UB).
TEST(TabularHop, OutOfRangeGuards) {
    MockTable t;
    t.SetCell(21, ibValue(1));
    ibDataViewItem row;
    ibValue out;
    EXPECT_FALSE(t.GetValueByPath(row, std::vector<ibSourceHop>{}, 0, out));
    EXPECT_FALSE(t.GetValueByPath(row, std::vector<ibSourceHop>{ {21} }, 5, out));
}

// The walk can START at a non-zero index — the tablebox passes its own `prefix` so the tail is row-relative.
TEST(TabularHop, StartsAtFromIndex) {
    MockTable t;
    t.SetCell(21, ibValue(wxString(wxT("A"))));
    t.SetCell(22, ibValue(wxString(wxT("B"))));
    ibDataViewItem row;
    ibValue out;
    // path = [prefix-hop, 22]; from = 1 -> reads column 22, ignores the prefix hop
    EXPECT_TRUE(t.GetValueByPath(row, std::vector<ibSourceHop>{ {21}, {22} }, 1, out));
    EXPECT_TRUE(out.GetString() == wxT("B"));
}
