// =============================================================================
// OES Enterprise — ibRawDBColumn tests
//
// ibRawDBColumn (backend/query/queryColumn.h) is a direct physical field — the
// scaffold columns (uuid / rowData) and the door's SetValue(raw, ...) path. A raw
// column is its OWN single physical field (role Raw, no _TYPE spread); the value
// codec, the wire codec and the DDL builder all depend on that. Pure.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include "backend/query/queryColumn.h"    // ibRawDBColumn
#include "backend/query/columnLayout.h"   // DescribeColumnLayout / ColumnFieldNames / ibColumnRole / ibColumnSlot

TEST(RawDBColumn, FactoryCarriesFieldName) {
    EXPECT_EQ(ibRawDBColumn::String(wxT("code")).GetName(), wxT("code"));
    EXPECT_EQ(ibRawDBColumn::Number(wxT("qty")).GetName(),  wxT("qty"));
    EXPECT_EQ(ibRawDBColumn::Guid(wxT("uuid")).GetName(),   wxT("uuid"));
    EXPECT_EQ(ibRawDBColumn::Boolean(wxT("flag")).GetName(),wxT("flag"));
}

TEST(RawDBColumn, RawLowersToOneFieldNamedAfterItself) {
    const ibRawDBColumn col = ibRawDBColumn::String(wxT("name"));
    const std::vector<wxString> fields = ColumnFieldNames(&col);
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0], wxT("name"));
}

TEST(RawDBColumn, RawLayoutSlotIsRoleRaw) {
    const ibRawDBColumn col = ibRawDBColumn::Number(wxT("qty"));
    const std::vector<ibColumnSlot> layout = DescribeColumnLayout(&col);
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_EQ(layout[0].m_name, wxT("qty"));
    EXPECT_EQ(layout[0].m_role, ibColumnRole::Raw);
}
