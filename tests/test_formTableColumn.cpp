// =============================================================================
// OES Enterprise — a table's column: a field in a row, its kind a view (docs/private/form-column-view.md).
//
// A column's View makes a child that holds the view's properties and events; outside there is one column, as a field is
// one control (test_formField.cpp). So every check here goes through the doors any control has — the frame and the
// property object — and never names the child. These pin:
//
//   * a column is born an Input, its editor's properties its own;
//   * its view goes out with the form and comes back with it;
//   * a Flag is a Boolean's only, and a form field's other views are no column's;
//   * a source of another type makes a Flag an Input at once;
//   * a view this version does not know loads as an Input;
//   * a Label is read-only and has no editor.
//
// DB-FREE AND UI-FREE: a column made by its own ctor, outside any table.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/objCtor.h"                         // ibCtorAbstractType — a column made by its own ctor
#include "backend/propertyManager/property/propertyEnum.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/tableBox.h"      // g_controlTableBoxColumnCLSID

namespace {

ibValue MakeColumn()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(g_controlTableBoxColumnCLSID);
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

// A column asked only through the doors every control has. The view inside is never named.
struct ColumnUnderTest {
	ibValue m_value;
	ibValueModelTableBoxColumn* m_column = nullptr;

	ColumnUnderTest() : m_value(MakeColumn()), m_column(dynamic_cast<ibValueModelTableBoxColumn*>(m_value.GetRef())) {}

	bool IsOk() const { return m_column != nullptr; }

	ibPropertyEnumBase* ViewProperty() const {
		return dynamic_cast<ibPropertyEnumBase*>(m_column->GetProperty(ibString(ibT("View"))));
	}

	int View() const {
		const ibPropertyEnumBase* view = ViewProperty();
		return view != nullptr ? (int)view->GetValueAsInteger() : -1;
	}

	bool Has(const ibChar* name) const { return m_column->GetProperty(ibString(name)) != nullptr; }

	// Through the door the inspector, MCP and a script write through.
	bool SetView(ibColumnView view) {
		return ibPropertyGate::SetValue(nullptr, ViewProperty(), ibVariant(static_cast<int>(view)));
	}
};

} // namespace

TEST(FormTableColumn, AColumnIsBornAnInput)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest column;
	ASSERT_TRUE(column.IsOk());
	EXPECT_EQ(column.View(), (int)ibColumnView_Input);
	EXPECT_TRUE(column.Has(ibT("PasswordMode")));   // the editor's
	EXPECT_TRUE(column.Has(ibT("ChoiceForm")));
	EXPECT_NE(column.m_column->GetEvent(ibString(ibT("StartChoice"))), nullptr);
	EXPECT_FALSE(column.m_column->IsReadOnly());
}

TEST(FormTableColumn, SavedAndLoadedKeepsTheView)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ASSERT_TRUE(written.SetView(ibColumnView_Label));
	ibDataNode node;
	ASSERT_TRUE(written.m_column->WriteData(node));

	ColumnUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_column->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibColumnView_Label);
	EXPECT_FALSE(read.Has(ibT("PasswordMode")));
}

// A Flag is a Boolean's: refused on a column of a string, taken on one of a Boolean — and its own event comes with it.
TEST(FormTableColumn, AFlagIsABooleansOnly)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest column;
	ASSERT_TRUE(column.IsOk());
	EXPECT_FALSE(column.SetView(ibColumnView_Flag));
	EXPECT_EQ(column.View(), (int)ibColumnView_Input);

	column.m_column->SetDefaultMetaType(ibValueTypes::TYPE_BOOLEAN);
	ASSERT_TRUE(column.SetView(ibColumnView_Flag));
	EXPECT_EQ(column.View(), (int)ibColumnView_Flag);
	EXPECT_FALSE(column.Has(ibT("PasswordMode")));
	EXPECT_NE(column.m_column->GetEvent(ibString(ibT("OnChange"))), nullptr);
}

// A SOURCE OF ANOTHER TYPE makes a Flag an Input at once, told as the live road tells it — the type set, then the column
// told of its source (ibFormDesignView::SetProperty); never drawn: MCP edits a form nobody draws (2026-11-24).
TEST(FormTableColumn, ASourceOfAnotherTypeMakesAFlagAnInput)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest column;
	ASSERT_TRUE(column.IsOk());
	column.m_column->SetDefaultMetaType(ibValueTypes::TYPE_BOOLEAN);
	ASSERT_TRUE(column.SetView(ibColumnView_Flag));

	column.m_column->SetDefaultMetaType(ibValueTypes::TYPE_STRING);
	ibProperty* const source = column.m_column->GetProperty(ibString(ibT("Source")));
	ASSERT_NE(source, nullptr);
	column.m_column->OnPropertyChanged(source, source->GetValue(), source->GetValue());
	EXPECT_EQ(column.View(), (int)ibColumnView_Input);
	EXPECT_TRUE(column.Has(ibT("PasswordMode")));
}

// The inspector offers the column's own views — an enumeration of its own (Max, 2026-11-24), not the field's six: the
// field's Switch is no number of a column's view — and of them those its type allows (the View's filter, IsViewAllowed):
// a Flag once the column is a Boolean's. One not offered is still read (HasEnumValue).
TEST(FormTableColumn, OnlyItsOwnViewsAreOffered)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest column;
	ASSERT_TRUE(column.IsOk());
	ibPropertyChoiceList offered;
	column.ViewProperty()->GetValueList(offered);
	ASSERT_EQ(offered.GetCount(), 2u);   // Input, Label
	EXPECT_TRUE(column.ViewProperty()->HasEnumValue(ibColumnView_Flag));
	EXPECT_FALSE(column.ViewProperty()->HasEnumValue(ibFieldView_Switch));

	column.m_column->SetDefaultMetaType(ibValueTypes::TYPE_BOOLEAN);
	ibPropertyChoiceList boolean;
	column.ViewProperty()->GetValueList(boolean);
	EXPECT_EQ(boolean.GetCount(), 3u);
}

TEST(FormTableColumn, AnUnknownViewLoadsAsInput)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ASSERT_TRUE(written.SetView(ibColumnView_Label));
	ibDataNode node;
	ASSERT_TRUE(written.m_column->WriteData(node));
	node.SetField(ibString(ibT("View")), ibDataValue::Int(99));   // a later version's, or a damaged form's - in its place

	ColumnUnderTest read;
	ASSERT_TRUE(read.IsOk());
	EXPECT_TRUE(read.m_column->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibColumnView_Input);
	EXPECT_TRUE(read.Has(ibT("PasswordMode")));
}

// A Label shows the cell and edits nothing: read-only, with no editor's properties and no events of its own.
TEST(FormTableColumn, ALabelIsReadOnly)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	ColumnUnderTest column;
	ASSERT_TRUE(column.IsOk());
	ASSERT_TRUE(column.SetView(ibColumnView_Label));
	EXPECT_TRUE(column.m_column->IsReadOnly());
	EXPECT_FALSE(column.Has(ibT("Mask")));
	EXPECT_EQ(column.m_column->GetEvent(ibString(ibT("OnChange"))), nullptr);
}
