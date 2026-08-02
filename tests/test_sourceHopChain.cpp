// =============================================================================
// OES Enterprise — the RUNTIME hop chain: table -> reference -> field.
//
// form-attribute-binding.md § Open edges listed this as the gap the harness did
// not cover: test_tabularHop.cpp stops where the table hands over (it proves the
// walk correctly REFUSES to dot into a primitive cell), and test_sourceExplorer
// covers the DESIGN-TIME twin (WalkColumns over typed-empty values). What was
// missing is the live chain — a row cell that IS a source, dotted one and two
// hops deep through ibSourceDataObject::ResolvePath.
//
// A MockSource is `: ibValue, ibSourceDataObject` (ibValue FIRST — the walk finds
// it via ibValue::ConvertToValue, a dynamic_cast off the value; see
// reference_ibvalue_first_base_pmf). It answers hops from a plain id -> value map,
// so the PLUMBING is what is under test: the table starts the walk, the source
// objects continue it, and each step self-describes the next. No metadata, no DB,
// no model.
// =============================================================================

#include <gtest/gtest.h>
#include <map>
#include <vector>

#include "backend/tabularDataObject.h"     // ibTabularDataObject
#include "backend/srcDataObject.h"         // ibSourceDataObject + ResolvePath
#include "backend/modelView.h"             // ibDataViewItem
#include "backend/compiler/value.h"        // ibValue
#include "backend/sourceDescription.h"     // ibSourceHop

namespace {

// A scalar source: answers a hop from its own field map. Stands in for a reference
// value (the real one is ibValueReferenceDataObject, which is also `ibValue +
// ibSourceDataObject`), minus every metadata concern the walk does not touch.
class MockSource : public ibValue, public ibSourceDataObject {
	std::map<ibSourceId, ibValue> m_fields;
public:
	MockSource() : ibValue(ibValueTypes::TYPE_VALUE) {}

	void SetField(ibSourceId id, const ibValue& v) { m_fields[id] = v; }

	// THE hop gate. A pure lookup: no pinned-type twin, no composite fork — those
	// belong to the reference implementation, not to the walk being tested here.
	bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override {
		const auto it = m_fields.find(hop.m_id);
		if (it == m_fields.end())
			return false;
		out = it->second;
		return true;
	}

	// ibSourceDataObject / ibSourceObject contract — inert for a metadata-free walk.
	void SourceIncrRef() override {}
	void SourceDecrRef() override {}
	bool IsEmpty() const override { return m_fields.empty(); }
	ibUniqueKey GetGuid() const override { return ibUniqueKey(); }
	const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return nullptr; }
	const ibSourceExplorer* GetSourceExplorer() const override { return nullptr; }
	const ibMetaData* GetSourceMetaData() const override { return nullptr; }
	ibClassID GetSourceClassType() const override { return 0; }
	wxString GetSourceCaption() const override { return wxEmptyString; }
};

// One row of cells by column id — same shape as test_tabularHop's mock, reused here
// so the chain starts where a real tablebox row starts.
class MockRow : public ibTabularDataObject {
	std::map<ibSourceId, ibValue> m_cells;
public:
	void SetCell(ibSourceId id, const ibValue& v) { m_cells[id] = v; }

	bool GetValueBySourceHop(const ibDataViewItem& /*item*/, const ibSourceHop& hop, ibValue& out) const override {
		const auto it = m_cells.find(hop.m_id);
		if (it == m_cells.end())
			return false;
		out = it->second;
		return true;
	}

	const ibValueMetaObjectCompositeData* GetSourceMetaObject() const override { return nullptr; }
	const ibMetaData* GetSourceMetaData() const override { return nullptr; }
	ibClassID GetSourceClassType() const override { return 0; }
};

// Wrap a source as a NON-OWNING cell value (TYPE_CONST_REFFER). This is the shape
// the docs call out as the hazard: capturing a source as an OWNING reffer takes a
// stack/member object's refcount 0 -> 1, and the release 1 -> 0 deletes it. Read
// access is all a walk needs, so every cell here is a const reffer.
ibValue Cell(const MockSource& src) {
	ibValue v;
	v = static_cast<const ibValue*>(&src);
	return v;
}

} // namespace

// The chain the harness did not cover: row cell -> reference -> field. The table
// resolves hop 0 off the row, then TRANSFERS the tail to the source object.
TEST(SourceHopChain, TableToReferenceToField) {
	MockSource ref;
	ref.SetField(31, ibValue(wxString(wxT("Kyiv"))));

	MockRow row;
	row.SetCell(21, Cell(ref));            // the cell IS a source, not a primitive

	ibDataViewItem item;
	ibValue out;
	ASSERT_TRUE(row.GetValueByPath(item, std::vector<ibSourceHop>{ {21}, {31} }, 0, out));
	EXPECT_TRUE(out.GetString() == wxT("Kyiv"));
}

// Depth is not special-cased: reference -> reference -> field walks the same loop.
TEST(SourceHopChain, TableToReferenceToReferenceToField) {
	MockSource city;
	city.SetField(41, ibValue(wxString(wxT("UA"))));

	MockSource partner;
	partner.SetField(31, Cell(city));

	MockRow row;
	row.SetCell(21, Cell(partner));

	ibDataViewItem item;
	ibValue out;
	ASSERT_TRUE(row.GetValueByPath(item, std::vector<ibSourceHop>{ {21}, {31}, {41} }, 0, out));
	EXPECT_TRUE(out.GetString() == wxT("UA"));
}

// A miss at a DEEPER hop is a broken binding — false, not a crash and not a
// half-resolved value. (test_tabularHop covers the miss at hop 0.)
TEST(SourceHopChain, MissAtDeepHopFails) {
	MockSource ref;
	ref.SetField(31, ibValue(1));

	MockRow row;
	row.SetCell(21, Cell(ref));

	ibDataViewItem item;
	ibValue out;
	EXPECT_FALSE(row.GetValueByPath(item, std::vector<ibSourceHop>{ {21}, {999} }, 0, out));
}

// Dotting THROUGH a source into a primitive still ends the walk: the second hop
// lands on a string, the third has nothing to step into.
TEST(SourceHopChain, CannotDotPastAPrimitiveLeaf) {
	MockSource ref;
	ref.SetField(31, ibValue(wxString(wxT("leaf"))));

	MockRow row;
	row.SetCell(21, Cell(ref));

	ibDataViewItem item;
	ibValue out;
	EXPECT_FALSE(row.GetValueByPath(item, std::vector<ibSourceHop>{ {21}, {31}, {41} }, 0, out));
}

// ResolvePath is a STATIC over a starting VALUE — the tablebox renderer feeds it a
// row's reference cell directly, without going through a table at all.
TEST(SourceHopChain, ResolvePathWalksFromABareValue) {
	MockSource city;
	city.SetField(41, ibValue(wxString(wxT("UA"))));
	MockSource partner;
	partner.SetField(31, Cell(city));

	ibValue start = Cell(partner);
	ibValue out;
	ASSERT_TRUE(ibSourceDataObject::ResolvePath(start, std::vector<ibSourceHop>{ {31}, {41} }, 0, out));
	EXPECT_TRUE(out.GetString() == wxT("UA"));

	// from == path.size() is the degenerate walk: nothing to step, so the START
	// value comes straight back out.
	ibValue same;
	ASSERT_TRUE(ibSourceDataObject::ResolvePath(start, std::vector<ibSourceHop>{ {31} }, 1, same));
	ibSourceDataObject* echoed = nullptr;
	EXPECT_TRUE(same.ConvertToValue(echoed));
	EXPECT_EQ(echoed, static_cast<ibSourceDataObject*>(&partner));
}

// A source walks its OWN first hop: GetValueByPath off the source feeds hop 0 to
// itself, then delegates the tail to ResolvePath.
TEST(SourceHopChain, SourceWalksItsOwnFirstHop) {
	MockSource city;
	city.SetField(41, ibValue(wxString(wxT("UA"))));
	MockSource partner;
	partner.SetField(31, Cell(city));

	ibValue out;
	ASSERT_TRUE(partner.GetValueByPath(std::vector<ibSourceHop>{ {31}, {41} }, out));
	EXPECT_TRUE(out.GetString() == wxT("UA"));
}

// The member-cell hazard, pinned as a test: a cell holding a source must NOT own
// it. Wrapping a stack source as a const reffer and letting the wrapper die must
// leave the source alive — an OWNING reffer would take refcount 0 -> 1 and delete
// a stack object on release.
TEST(SourceHopChain, CellReffersAreNonOwning) {
	MockSource src;
	src.SetField(31, ibValue(wxString(wxT("alive"))));

	{
		ibValue cell = Cell(src);
		EXPECT_TRUE(cell.IsConstReference()) << "a source cell is a READ-ONLY view, never an owning reffer";
	}   // cell dies here — src must survive it

	ibValue out;
	ASSERT_TRUE(src.GetValueByPath(std::vector<ibSourceHop>{ {31} }, out));
	EXPECT_TRUE(out.GetString() == wxT("alive")) << "the source outlived the cell that viewed it";
}
