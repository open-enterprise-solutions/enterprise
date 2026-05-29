// =============================================================================
// OES Enterprise — paging RAM-backed table contract tests (scaffold)
//
// Covers ibValueModelRamTableBase invariants that the 2026-05-07 TabularSection
// arc relies on:
//
//   * GetRow(item) returns the RAW m_nodeValues index (not view position).
//     SaveData iterates `for (long i=0; i<GetRowCount(); i++) GetItem(i)`
//     and needs every row including filtered-out ones — view-based GetRow
//     would silently skip hidden rows on save.
//
//   * BuildVisibleView() snapshot reflects m_filterRow + m_sortOrder.  Get*Fetch
//     slices THIS view, not raw m_nodeValues, so filter / sort actually shows
//     up in the rendered buffer.
//
//   * Mutation primitives (Insert at index N, Append at end, Remove by ptr)
//     update m_nodeValues in place.  notify=false skips the m_modelProvider
//     fire so unit tests don't need a control attached.
//
// What's NOT covered here (separate test target with frontend / wx init
// needed):
//
//   * ItemToRowJob / FindNode / FindChildByItem walker pointer-identity
//     semantics — those are in datavgen.cpp anonymous namespace.
//   * Narrow ItemAdded / ItemDeleted control-side path.
//   * ibDataViewItem refcount across copy/move (covered partially by the
//     refcount-aware refactor's existing call-site smoke tests).
// =============================================================================

#include <gtest/gtest.h>

#include "backend/tableInfo.h"
#include "backend/compiler/value.h"

namespace {

// ibValueTableRow is now a nested type of ibValueModelTableBase; this alias
// keeps the test body terse after the nesting refactor.
using ibValueTableRow = ibValueModelTableBase::ibValueTableRow;

// ---------------------------------------------------------------------------
// Minimal test subclass.  ibValueModelRamTableBase has many pure virtuals
// inherited from ibValueModel / ibDataViewModel / ibValueModelTableBase that
// the production concrete classes (TabularSection, ibValueTable, register
// record sets) implement against their meta-tables.  For unit testing we
// stub out everything that BuildVisibleView / Get*Fetch / GetRow / GetItem
// / Insert / Append / Remove don't actually call.
//
// `GetValueByMetaID` IS exercised by BuildVisibleView's filter+sort
// comparator — we read each row's stored ibValue out of its own
// m_nodeValues map directly (which is exactly how ibValueTableRow is
// already laid out), so the stub forwards through the row.
// ---------------------------------------------------------------------------
class TestRamModel : public ibValueModelRamTableBase {
public:
	// Expose protected m_nodeValues for tests to inspect ordering.
	const std::vector<ibValueTableRow*>& Nodes() const { return m_nodeValues; }

	// === pure virtuals on ibValueModel ============================
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem&) override {
		return nullptr;
	}
	virtual ibValueModelColumnCollection* GetColumnCollection() const override {
		return nullptr;
	}
	virtual bool SetValueByMetaID(const ibDataViewItem& item,
	                              const ibMetaID& id,
	                              const ibValue& value) override {
		auto* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr) return false;
		return node->SetValue(id, value, /*notify*/false);
	}
	virtual bool GetValueByMetaID(const ibDataViewItem& item,
	                              const ibMetaID& id,
	                              ibValue& out) const override {
		auto* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr) return false;
		if (!node->HasColumnValue(id)) return false;
		out = node->GetTableValue(id);
		return true;
	}

	// === pure virtuals on ibValueModelTableBase ===================
	virtual void GetValueByRow(wxVariant& variant,
	                           const ibDataViewItem& item,
	                           unsigned int col) const override {
		auto* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr) return;
		if (node->HasColumnValue(col)) node->GetValue(col, variant);
	}
	virtual bool SetValueByRow(const wxVariant& variant,
	                           const ibDataViewItem& item,
	                           unsigned int col) override {
		auto* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr) return false;
		return node->SetValue(col, variant, /*notify*/false);
	}

	// === pure virtuals on ibTabularObject =========================
	// Not exercised by BuildVisibleView / Get*Fetch / GetRow / mutation —
	// the RAM table operates on rows directly, no source metaobject needed.
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const override {
		return nullptr;
	}
	virtual ibClassID GetSourceClassType() const override {
		return ibClassID();
	}
};

// Build a row with a single (col, value) pair — enough to exercise filter
// and sort comparators.  Caller transfers ownership to the model via
// Insert / Append (notify=false to skip m_modelProvider chain).
ibValueTableRow* MakeRow(const ibMetaID& col, const ibValue& v) {
	auto* row = new ibValueTableRow();
	row->AppendTableValue(col, v);
	return row;
}

// Sentinel meta-id for our single test column.  Real metaIDs come from
// the metadata registry; for a unit test any unique non-zero int works.
constexpr unsigned int kTestColID = 12345;

} // namespace

// ===========================================================================
// Sanity 1 — GetRow returns RAW m_nodeValues index, not view position
// ===========================================================================
TEST(PagingRamTable, GetRow_ReturnsRawIndex_NotViewPosition) {
	TestRamModel m;
	auto* r0 = MakeRow(kTestColID, ibValue(static_cast<int>(10)));
	auto* r1 = MakeRow(kTestColID, ibValue(static_cast<int>(20)));
	auto* r2 = MakeRow(kTestColID, ibValue(static_cast<int>(30)));
	m.Append(r0, /*notify*/false);
	m.Append(r1, /*notify*/false);
	m.Append(r2, /*notify*/false);

	EXPECT_EQ(m.GetRowCount(), 3L);
	EXPECT_EQ(m.GetRow(ibDataViewItem(r0)), 0L);
	EXPECT_EQ(m.GetRow(ibDataViewItem(r1)), 1L);
	EXPECT_EQ(m.GetRow(ibDataViewItem(r2)), 2L);

	// GetItem mirrors the raw index — save loop relies on this round-trip.
	EXPECT_EQ(m.GetItem(0).GetID(), r0);
	EXPECT_EQ(m.GetItem(1).GetID(), r1);
	EXPECT_EQ(m.GetItem(2).GetID(), r2);
}

// ===========================================================================
// Sanity 2 — Insert at index puts the new row at m_nodeValues[index]
// ===========================================================================
TEST(PagingRamTable, Insert_AtIndex_PushesExistingDown) {
	TestRamModel m;
	auto* r0 = MakeRow(kTestColID, ibValue(static_cast<int>(1)));
	auto* r1 = MakeRow(kTestColID, ibValue(static_cast<int>(2)));
	auto* r2 = MakeRow(kTestColID, ibValue(static_cast<int>(3)));
	m.Append(r0, false);
	m.Append(r1, false);
	m.Append(r2, false);

	auto* inserted = MakeRow(kTestColID, ibValue(static_cast<int>(99)));
	m.Insert(inserted, /*row*/1, false);

	// Expected layout: [r0, inserted, r1, r2]
	ASSERT_EQ(m.Nodes().size(), 4u);
	EXPECT_EQ(m.Nodes()[0], r0);
	EXPECT_EQ(m.Nodes()[1], inserted);
	EXPECT_EQ(m.Nodes()[2], r1);
	EXPECT_EQ(m.Nodes()[3], r2);

	// GetRow tracks the new positions.
	EXPECT_EQ(m.GetRow(ibDataViewItem(inserted)), 1L);
	EXPECT_EQ(m.GetRow(ibDataViewItem(r1)), 2L);
}

// ===========================================================================
// Sanity 3 — BuildVisibleView with no filter/sort returns m_nodeValues identity
// ===========================================================================
TEST(PagingRamTable, BuildVisibleView_NoFilterNoSort_IsIdentity) {
	TestRamModel m;
	auto* r0 = MakeRow(kTestColID, ibValue(static_cast<int>(10)));
	auto* r1 = MakeRow(kTestColID, ibValue(static_cast<int>(20)));
	auto* r2 = MakeRow(kTestColID, ibValue(static_cast<int>(30)));
	m.Append(r0, false);
	m.Append(r1, false);
	m.Append(r2, false);

	const auto view = m.BuildVisibleView();
	ASSERT_EQ(view.size(), 3u);
	EXPECT_EQ(view[0], r0);
	EXPECT_EQ(view[1], r1);
	EXPECT_EQ(view[2], r2);
}
