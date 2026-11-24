// =============================================================================
// OES Enterprise — the layout: one sizer, its kind a view (docs/private/form-layout.md).
//
// A layout's View makes a child that holds the view's properties; outside there is one control, as a field is one
// (test_formField.cpp). So every check here goes through the doors any control has — the frame and the property
// object — and never names the child. These pin:
//
//   * a layout is born a Box, laid out vertically;
//   * its view and the view's properties go out with the form and come back with it;
//   * what two views share by name crosses over a change of view — a Box turned into a Group keeps its Orient;
//   * a view's properties are the layout's members, as a script names them;
//   * a view this version does not know loads as a Box;
//   * it takes a control, not a page.
//
// DB-FREE AND UI-FREE: a layout made by its own ctor, outside any form.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/objCtor.h"                         // ibCtorAbstractType — a layout made by its own ctor
#include "backend/propertyManager/property/propertyEnum.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/field.h"         // g_controlFieldCLSID
#include "frmserver/visualView/ctrl/layout.h"        // g_controlLayoutCLSID
#include "frmserver/visualView/ctrl/notebook.h"      // g_controlNotebookPageCLSID

namespace {

ibValue MakeLayout()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(g_controlLayoutCLSID);
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

// A layout asked only through the doors every control has. The view inside is never named.
struct LayoutUnderTest {
	ibValue m_value;
	ibValueFrame* m_frame = nullptr;

	LayoutUnderTest() : m_value(MakeLayout()), m_frame(dynamic_cast<ibValueFrame*>(m_value.GetRef())) {}

	bool IsOk() const { return m_frame != nullptr; }

	ibPropertyEnumBase* EnumProperty(const ibChar* name) const {
		return dynamic_cast<ibPropertyEnumBase*>(m_frame->GetProperty(ibString(name)));
	}

	int View() const {
		const ibPropertyEnumBase* view = EnumProperty(ibT("View"));
		return view != nullptr ? (int)view->GetValueAsInteger() : -1;
	}

	int Orient() const {
		const ibPropertyEnumBase* orient = EnumProperty(ibT("Orient"));
		return orient != nullptr ? (int)orient->GetValueAsInteger() : -1;
	}

	// Through the door the inspector, MCP and a script write through.
	bool SetView(ibLayoutView view) {
		return ibPropertyGate::SetValue(nullptr, EnumProperty(ibT("View")), ibVariant(static_cast<int>(view)));
	}
};

} // namespace

TEST(FormLayout, ALayoutIsBornABox)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	LayoutUnderTest layout;
	ASSERT_TRUE(layout.IsOk());
	EXPECT_EQ(layout.View(), (int)ibLayoutView_Box);
	EXPECT_EQ(layout.Orient(), (int)ibOrientation_Vertical);
	EXPECT_EQ(layout.m_frame->GetProperty(ibString(ibT("Cols"))), nullptr);   // a Grid's
}

TEST(FormLayout, SavedAndLoadedKeepsTheViewAndItsProperties)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	LayoutUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ASSERT_TRUE(written.SetView(ibLayoutView_Grid));
	ibProperty* const cols = written.m_frame->GetProperty(ibString(ibT("Cols")));
	ASSERT_NE(cols, nullptr);
	ASSERT_TRUE(ibPropertyGate::SetValue(nullptr, cols, ibVariant(3)));

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	LayoutUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibLayoutView_Grid);
	ASSERT_NE(read.m_frame->GetProperty(ibString(ibT("Cols"))), nullptr);
	EXPECT_EQ(read.m_frame->GetProperty(ibString(ibT("Cols")))->GetValue().GetInteger(), 3);
	EXPECT_EQ(read.m_frame->GetProperty(ibString(ibT("Orient"))), nullptr);   // a Grid has none
}

// The Box is vertical by default and the Group horizontal: the Box's Orient is the one the Group keeps.
TEST(FormLayout, OrientCrossesFromBoxToGroup)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	LayoutUnderTest layout;
	ASSERT_TRUE(layout.IsOk());
	ASSERT_EQ(layout.Orient(), (int)ibOrientation_Vertical);

	ASSERT_TRUE(layout.SetView(ibLayoutView_Group));
	EXPECT_EQ(layout.View(), (int)ibLayoutView_Group);
	EXPECT_EQ(layout.Orient(), (int)ibOrientation_Vertical);
	EXPECT_NE(layout.m_frame->GetProperty(ibString(ibT("Title"))), nullptr);
}

// …the members a script names are the view's of the moment.
TEST(FormLayout, AViewsPropertiesAreTheLayoutsMembers)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	LayoutUnderTest layout;
	ASSERT_TRUE(layout.IsOk());
	EXPECT_EQ(layout.m_frame->FindProp(ibString(ibT("Cols"))), ibNOT_FOUND);

	ASSERT_TRUE(layout.SetView(ibLayoutView_Grid));
	const long cols = layout.m_frame->FindProp(ibString(ibT("Cols")));
	ASSERT_NE(cols, ibNOT_FOUND);
	ibValue value;
	ASSERT_TRUE(layout.m_frame->GetPropVal(cols, value));
	EXPECT_EQ(value.GetInteger(), 2);
}

TEST(FormLayout, AnUnknownViewLoadsAsBox)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	LayoutUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ASSERT_TRUE(written.SetView(ibLayoutView_Group));
	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));
	node.SetProperty(ibString(ibT("View")), ibDataValue::Int(99));   // a later version's, or a damaged form's

	LayoutUnderTest read;
	ASSERT_TRUE(read.IsOk());
	EXPECT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibLayoutView_Box);
	EXPECT_EQ(read.m_frame->GetProperty(ibString(ibT("Title"))), nullptr);   // a Box, not a Group
}

TEST(FormLayout, TakesAControlNotAPage)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	LayoutUnderTest layout;
	ASSERT_TRUE(layout.IsOk());
	EXPECT_TRUE(layout.m_frame->FilterChild(g_controlFieldCLSID));
	EXPECT_TRUE(layout.m_frame->FilterChild(g_controlLayoutCLSID));
	EXPECT_FALSE(layout.m_frame->FilterChild(g_controlNotebookPageCLSID));
}
