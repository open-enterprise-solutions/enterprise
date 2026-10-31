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
#include "backend/query/queryColumn.h"    // ibBackendColumnRawDB + the ibFieldTypes_* wire tags

// Every output's label is `out_` and the hash of the author's whole name, Latin or not: ASCII,
// twelve characters, the same for the writer and the reader. Firebird does not take a Cyrillic
// alias unquoted, and quoting is off because a quoted name becomes case-sensitive.
TEST(ColumnLayout, EveryAliasIsTheSameAsciiLabel)
{
	const auto label = [](const ibString& alias) {
		if (alias.length() != 12 || !alias.StartsWith(wxT("out_")))
			return false;
		for (const wxUniChar c : alias.Mid(4))
			if (!((c >= wxT('0') && c <= wxT('9')) || (c >= wxT('a') && c <= wxT('f'))))
				return false;
		return true;
	};

	const ibString date = ibSqlAliasOf(wxT("Date"));
	EXPECT_TRUE(label(date)) << date.ToStdString();
	EXPECT_EQ(date, ibSqlAliasOf(wxT("Date")));
	EXPECT_NE(date, ibSqlAliasOf(wxT("Flag")));

	const ibString flag = ibSqlAliasOf(wxT("Флаг"));
	EXPECT_TRUE(label(flag)) << flag.ToStdString();
	EXPECT_NE(flag, date);

	// A long name is the same twelve characters; a hyphen is not carried into the statement.
	EXPECT_TRUE(label(ibSqlAliasOf(wxT("AccountingCurrencyAmountCorrespondingTurnoverCr"))));
	EXPECT_TRUE(label(ibSqlAliasOf(wxT("a-b"))));

	// A name that ends like a field suffix is still the author's name: `Price_N` is not field N of `Price`.
	EXPECT_TRUE(label(ibSqlAliasOf(wxT("Price_N"))));
	EXPECT_NE(ibSqlAliasOf(wxT("Price_N")), ibSqlAliasOf(wxT("Price")) + wxT("_N"));
	EXPECT_TRUE(ibSqlAliasOf(ibString()).IsEmpty());
}

// The role -> physical-suffix table. These suffixes are the on-disk column names AND the wire spread;
// they must never change silently. Raw carries no suffix (the column is its own single field).
TEST(ColumnLayout, FieldSuffix_RoleTable)
{
	EXPECT_EQ(ibFieldSuffix(ibColumnRole::Raw),           ibString());
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
	const ibBackendColumnRawDB col = ibBackendColumnRawDB::Guid(wxT("uuid"));

	const std::vector<ibColumnSlot> layout = DescribeColumnLayout(&col);
	ASSERT_EQ(layout.size(), 1u);
	EXPECT_EQ(layout[0].m_name, wxT("uuid"));
	EXPECT_EQ(layout[0].m_role, ibColumnRole::Raw);

	const std::vector<ibString> fields = ColumnFieldNames(&col);
	ASSERT_EQ(fields.size(), 1u);
	EXPECT_EQ(fields[0], wxT("uuid"));
}
