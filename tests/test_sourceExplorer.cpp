// =============================================================================
// OES Enterprise — ibSourceExplorer path-resolution tests
//
// ibSourceExplorer (backend/srcDataObject.h) is the design-time source tree the
// binding walks: each hop is a FindById on the current node's children, so a
// bound path [attr, section, column] resolves node-by-node (the twin of the
// runtime GetValueByMetaID). Its nodes are built from PLAIN VALUES — no
// metaobject, no DB — so the resolver is testable directly, no metadata bring-up.
//
// Guards: the per-hop FindById (hit / miss), the table-section flag that gates a
// sub-table, and the reference variant a node carries — IsReference is read
// straight from the type's clsid (kind byte), no ibMetaData lookup, across the
// scalar / single-reference / composite cases.
// Pure — no DB / appData / metadata.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include "backend/srcDataObject.h"        // ibSourceExplorer
#include "backend/compiler/value.h"       // g_valueNumber/StringCLSID
#include "backend/clsid.h"                // reference_to_clsid / IsReference

// ---------------------------------------------------------------------------
// One hop: FindById resolves a column by id; a miss returns null (so a caller
// tells "found" from "broken binding")
// ---------------------------------------------------------------------------
TEST(SourceExplorer, FindByIdResolvesColumnOrNull) {
    const ibTypeDescription num(g_valueNumberCLSID), str(g_valueStringCLSID);
    ibSourceExplorer root(wxT("Root"), 1, num);
    root.AppendColumn(wxT("Code"), 10, str);
    root.AppendColumn(wxT("Sum"),  11, num);

    EXPECT_EQ(root.GetHelperCount(), 2u);

    const ibSourceExplorer* code = root.FindById(10);
    ASSERT_NE(code, nullptr);
    EXPECT_TRUE(code->GetSourceName() == wxT("Code"));
    EXPECT_TRUE(code->GetTypeDesc().ContainType(g_valueStringCLSID));

    EXPECT_EQ(root.FindById(999), nullptr);   // a miss is null, not a crash
}

// ---------------------------------------------------------------------------
// Multi-hop: a table section is its own sub-tree, gated by IsTableSection —
// the [attr, section, column] path resolves node-by-node
// ---------------------------------------------------------------------------
TEST(SourceExplorer, TableSectionResolvesNestedColumn) {
    const ibTypeDescription num(g_valueNumberCLSID), str(g_valueStringCLSID);
    ibSourceExplorer root(wxT("Doc"), 1, num);
    ibSourceExplorer& lines = root.AppendTable(wxT("Lines"), wxT("Lines"), 20, num);
    lines.AppendColumn(wxT("Item"),  21, str);
    lines.AppendColumn(wxT("Price"), 22, num);

    const ibSourceExplorer* section = root.FindById(20);   // hop 1: attr -> section
    ASSERT_NE(section, nullptr);
    EXPECT_TRUE(section->IsTableSection());
    EXPECT_EQ(section->GetHelperCount(), 2u);

    const ibSourceExplorer* price = section->FindById(22); // hop 2: section -> column
    ASSERT_NE(price, nullptr);
    EXPECT_FALSE(price->IsTableSection());
    EXPECT_TRUE(price->GetTypeDesc().ContainType(g_valueNumberCLSID));

    EXPECT_EQ(section->FindById(10), nullptr);   // a root-level id is not a child of the section
}

// ---------------------------------------------------------------------------
// A node carries its reference variant, classified from the clsid ALONE
// (no metaData): scalar -> not a reference, single -> one target, composite ->
// several targets, all references
// ---------------------------------------------------------------------------
TEST(SourceExplorer, ReferenceVariantClassifiedFromClsid) {
    const ibTypeDescription str(g_valueStringCLSID);
    const ibClassID refA = reference_to_clsid(1001);
    const ibClassID refB = reference_to_clsid(1002);

    ibSourceExplorer root(wxT("Root"), 1, str);
    root.AppendColumn(wxT("Name"),  30, str);                                              // scalar
    root.AppendColumn(wxT("Owner"), 31, ibTypeDescription(refA));                          // single reference
    root.AppendColumn(wxT("AnyOf"), 32, ibTypeDescription(std::vector<ibClassID>{ refA, refB })); // composite

    const ibSourceExplorer* name = root.FindById(30);
    ASSERT_NE(name, nullptr);
    ASSERT_FALSE(name->GetClsidList().empty());
    EXPECT_FALSE(IsReference(name->GetClsidList().front()));

    const ibSourceExplorer* owner = root.FindById(31);
    ASSERT_NE(owner, nullptr);
    ASSERT_EQ(owner->GetClsidList().size(), 1u);
    EXPECT_TRUE(IsReference(owner->GetClsidList().front()));

    const ibSourceExplorer* anyOf = root.FindById(32);
    ASSERT_NE(anyOf, nullptr);
    EXPECT_EQ(anyOf->GetClsidList().size(), 2u);
    for (const ibClassID& c : anyOf->GetClsidList())
        EXPECT_TRUE(IsReference(c));
}

namespace {

// A mock source whose explorer we populate BY HAND — simulating a "specific type
// with attributes" materialised in the explorer, so the REAL design-time dot-walk
// resolver (ibSourceDataObject::WalkColumns) can step a path through it with no
// metadata. WalkColumns descends a node-with-children (a section) into the SAME
// explorer, which is exactly the metadata-free branch we exercise here.
class MockSource : public ibSourceDataObject {
    ibSourceExplorer m_explorer;
public:
    MockSource() {
        const ibTypeDescription num(g_valueNumberCLSID), str(g_valueStringCLSID);
        ibSourceExplorer& line = m_explorer.AppendTable(wxT("Line"), wxT("Line"), 20, num);
        line.AppendColumn(wxT("Item"),  21, str);
        line.AppendColumn(wxT("Price"), 22, num);
    }
    const ibSourceExplorer* GetSourceExplorer() const override { return &m_explorer; }

    // Rest of the source interface — trivial; a section-only WalkColumns never touches these.
    void SourceIncrRef() override {}
    void SourceDecrRef() override {}
    bool IsEmpty() const override { return false; }
    ibUniqueKey GetGuid() const override { return ibUniqueKey(); }
    const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return nullptr; }
    const ibMetaData* GetSourceMetaData() const override { return nullptr; }
    ibClassID GetSourceClassType() const override { return 0; }
    wxString GetSourceCaption() const override { return wxEmptyString; }
};

} // namespace

// ---------------------------------------------------------------------------
// The REAL dot-walk resolver: WalkColumns steps an ibSourceDescription path
// [Line, Price] hop-by-hop through the explorer, building the dotted name;
// a broken hop (at any depth) returns false
// ---------------------------------------------------------------------------
TEST(SourceExplorer, WalkColumnsStepsDotPath) {
    MockSource src;
    const ibBackendSourceColumn* leaf = nullptr;
    wxString text;

    EXPECT_TRUE(src.WalkColumns(std::vector<ibSourceId>{ 20, 22 }, 0, leaf, &text));
    EXPECT_TRUE(text == wxT(".Line.Price"));

    text.clear();
    EXPECT_FALSE(src.WalkColumns(std::vector<ibSourceId>{ 20, 999 }, 0, leaf, &text));   // no such field in the type

    text.clear();
    EXPECT_FALSE(src.WalkColumns(std::vector<ibSourceId>{ 999, 22 }, 0, leaf, &text));   // no such type at the root
}
