// =============================================================================
// OES Enterprise — the static: a decoration, what it shows a view (docs/private/form-column-view.md).
//
// A static's View makes a child that holds the view's properties; outside there is one control, as a layout is one
// (test_formLayout.cpp). So every check here goes through the doors any control has — the frame and the property
// object — and never names the child. These pin:
//
//   * a static is born a Text;
//   * a Line chosen has its Orient, and its place in the sizer is left as it was (Stretch is the person's);
//   * its view and the view's properties go out with the form and come back with it — a line read keeps the stretch
//     it was saved with;
//   * a view this version does not know loads as a Text.
//
// DB-FREE AND UI-FREE: a static made by its own ctor, outside any form.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/objCtor.h"                         // ibCtorAbstractType — a static made by its own ctor
#include "backend/propertyManager/property/propertyEnum.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/static.h"        // g_controlStaticCLSID

namespace {

ibValue MakeStatic()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(g_controlStaticCLSID);
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

// A static asked only through the doors every control has. The view inside is never named.
struct StaticUnderTest {
	ibValue m_value;
	ibValueFrame* m_frame = nullptr;

	StaticUnderTest() : m_value(MakeStatic()), m_frame(dynamic_cast<ibValueFrame*>(m_value.GetRef())) {}

	bool IsOk() const { return m_frame != nullptr; }

	ibPropertyEnumBase* EnumProperty(const ibChar* name) const {
		return dynamic_cast<ibPropertyEnumBase*>(m_frame->GetProperty(ibString(name)));
	}

	int Enum(const ibChar* name) const {
		const ibPropertyEnumBase* property = EnumProperty(name);
		return property != nullptr ? (int)property->GetValueAsInteger() : -1;
	}

	// Through the door the inspector, MCP and a script write through.
	bool Set(const ibChar* name, int value) {
		return ibPropertyGate::SetValue(nullptr, EnumProperty(name), ibVariant(value));
	}
};

} // namespace

TEST(FormStatic, AStaticIsBornAText)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	StaticUnderTest decoration;
	ASSERT_TRUE(decoration.IsOk());
	EXPECT_EQ(decoration.Enum(ibT("View")), (int)ibStaticView_Text);
	EXPECT_NE(decoration.m_frame->GetProperty(ibString(ibT("Title"))), nullptr);
	EXPECT_EQ(decoration.m_frame->GetProperty(ibString(ibT("Orient"))), nullptr);   // a Line's
	EXPECT_EQ(decoration.Enum(ibT("Stretch")), (int)ibStretch_Shrink);
}

// The view does not touch the static's place in the sizer (Max, 2026-11-24): a Line chosen leaves Stretch as it was.
TEST(FormStatic, ALineChosenLeavesItsStretch)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	StaticUnderTest decoration;
	ASSERT_TRUE(decoration.IsOk());
	ASSERT_TRUE(decoration.Set(ibT("View"), ibStaticView_Line));
	EXPECT_EQ(decoration.Enum(ibT("View")), (int)ibStaticView_Line);
	EXPECT_EQ(decoration.Enum(ibT("Orient")), (int)ibOrientation_Horizontal);
	EXPECT_EQ(decoration.m_frame->GetProperty(ibString(ibT("Title"))), nullptr);   // a Text's
	EXPECT_EQ(decoration.Enum(ibT("Stretch")), (int)ibStretch_Shrink);
}

TEST(FormStatic, SavedAndLoadedKeepsTheLineItsOrientAndItsStretch)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	StaticUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ASSERT_TRUE(written.Set(ibT("View"), ibStaticView_Line));
	ASSERT_TRUE(written.Set(ibT("Orient"), ibOrientation_Vertical));
	ASSERT_TRUE(written.Set(ibT("Stretch"), ibStretch_Expand));   // the person's

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	StaticUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.Enum(ibT("View")), (int)ibStaticView_Line);
	EXPECT_EQ(read.Enum(ibT("Orient")), (int)ibOrientation_Vertical);
	EXPECT_EQ(read.Enum(ibT("Stretch")), (int)ibStretch_Expand);
}

TEST(FormStatic, AnUnknownViewLoadsAsText)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	StaticUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ASSERT_TRUE(written.Set(ibT("View"), ibStaticView_Line));
	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));
	node.SetProperty(ibString(ibT("View")), ibDataValue::Int(99));   // a later version's, or a damaged form's

	StaticUnderTest read;
	ASSERT_TRUE(read.IsOk());
	EXPECT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.Enum(ibT("View")), (int)ibStaticView_Text);
	EXPECT_EQ(read.m_frame->GetProperty(ibString(ibT("Orient"))), nullptr);   // a Text, not a Line
}
