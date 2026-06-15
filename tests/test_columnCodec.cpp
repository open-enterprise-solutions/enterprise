// Column-layout tier invariants — the LOAD-BEARING role -> physical-suffix and role -> persisted-tag
// tables, plus the raw-column layout, that the value codec (ibColumnCodec), the wire codec
// (ibDataMover::Binary*) and the DDL builder ALL share off DescribeColumnLayout. If any of these drift
// (a renamed suffix, a re-numbered tag), the writer and the reader — and the generated DDL — silently
// disagree on a column's physical field spread, and stored data no longer round-trips. These are pure
// (no DB, no appData): they pin the contract the byte-compatibility rests on.
//
// The full VALUE round-trip (WriteValue an ibValue, ReadValue it back equal; BinaryFromResult ->
// BinaryToStatement) is integration scope — it needs a real cursor/statement (a SQLite ibDatabaseLayer)
// and a typed metadata column. The running app exercises it on every save / dump; a CI integration
// target is the place to assert it mechanically.

#include <gtest/gtest.h>

#include "backend/query/columnLayout.h"   // ibFieldSuffix / ibPersistedTypeTag / DescribeColumnLayout / ColumnFieldNames / ibColumnRole / ibColumnSlot
#include "backend/query/queryColumn.h"    // ibRawDBColumn + the ibFieldTypes_* wire tags

// The role -> physical-suffix table. These suffixes are the on-disk column names AND the wire spread;
// they must never change silently. Raw carries no suffix (the column is its own single field).
TEST(ColumnLayout, FieldSuffix_RoleTable)
{
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Raw),           wxString());
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Discriminator), wxT("_TYPE"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Boolean),       wxT("_B"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Number),        wxT("_N"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Date),          wxT("_D"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::String),        wxT("_S"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Enum),          wxT("_E"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::ReferenceType), wxT("_RTRef"));
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::ReferenceId),   wxT("_RRRef"));
}

// The role -> persisted variant tag (_TYPE discriminator value). The codec writes this tag and switches
// on it when reading; the reference pair shares the single Reference tag. Raw / Discriminator carry none.
TEST(ColumnLayout, PersistedTypeTag_RoleTable)
{
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::Boolean),       ibFieldTypes_Boolean);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::Number),        ibFieldTypes_Number);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::Date),          ibFieldTypes_Date);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::String),        ibFieldTypes_String);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::Enum),          ibFieldTypes_Enum);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::ReferenceType), ibFieldTypes_Reference);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::ReferenceId),   ibFieldTypes_Reference);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::Raw),           ibFieldTypes_Empty);
	EXPECT_EQ(ibPersistedTypeTag(ibColumnRole::Discriminator), ibFieldTypes_Empty);
}

// A raw column lowers to exactly ONE physical field (its own name), role Raw — no _TYPE spread. This is
// the scaffold path (uuid / rowData) and the seed key resolution depend on it.
TEST(ColumnLayout, RawColumn_SingleSlot)
{
	const ibRawDBColumn col = ibRawDBColumn::Guid(wxT("uuid"));

	const std::vector<ibColumnSlot> layout = DescribeColumnLayout(&col, nullptr);
	ASSERT_EQ(layout.size(), 1u);
	EXPECT_EQ(layout[0].m_name, wxT("uuid"));
	EXPECT_EQ(layout[0].m_role, ibColumnRole::Raw);

	const std::vector<wxString> fields = ColumnFieldNames(&col, nullptr);
	ASSERT_EQ(fields.size(), 1u);
	EXPECT_EQ(fields[0], wxT("uuid"));
}
