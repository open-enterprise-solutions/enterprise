// =============================================================================
// Container controls — TableBox, TextBox, GridBox.
//
// wxWidgets' own tests/controls/{textctrltest,gridtest,dataviewctrltest}.cpp
// drive a control's behaviour (values, columns, content) against a live widget.
// The OES equivalents are the value-model controls (ibValueModelTableBox /
// ibValueTextBox / ibValueGridBox) — so these tests exercise the MODEL surface
// (type identity, column building, child containment) without a wx tree, the
// layer the warehouse / TSD list is built from. Widget-level behaviour comes
// once the visual-host build path (test_frontendVisualHost) is green.
// =============================================================================

#include "frontendFormFix.h"                      // FrontendFormFix + NewForm()

#include "frontend/visualView/ctrl/tableBox.h"   // ibValueModelTableBox + g_controlTableBox*CLSID
#include "frontend/visualView/ctrl/textBox.h"    // ibValueTextBox
#include "frontend/visualView/ctrl/gridBox.h"    // ibValueGridBox
#include "frontend/visualView/ctrl/notebook.h"   // ibValueNotebook + g_controlNotebook*CLSID
#include "backend/compiler/value.h"

#include <wx/buffer.h>                            // wxMemoryBuffer (form serialize round-trip)

namespace {

constexpr ibClassID g_ctrlTextBoxCLSID  = control_to_clsid("CT_TEXT");
constexpr ibClassID g_ctrlGridBoxCLSID  = control_to_clsid("CT_GRID");
constexpr ibClassID g_ctrlButtonCLSID   = control_to_clsid("CT_BUTN");

// Count a control's direct children carrying a given clsid.
unsigned CountChildrenOfType(ibValueFrame* parent, const ibClassID& clsid)
{
	unsigned n = 0;
	for (unsigned i = 0; i < parent->GetChildCount(); ++i)
		if (parent->GetChild(i) != nullptr && parent->GetChild(i)->GetClassType() == clsid)
			++n;
	return n;
}

} // namespace

// ----------------------------------- TableBox --------------------------------

// The table box builds as its own concrete model type and identifies by clsid.
TEST_F(FrontendFormFix, DISABLED_TableBoxBuildsAsModelType)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* tb = form->NewObject(g_controlTableBoxCLSID);
	ASSERT_NE(tb, nullptr);
	EXPECT_EQ(tb->GetClassType(), g_controlTableBoxCLSID);
	EXPECT_NE(dynamic_cast<ibValueModelTableBox*>(tb), nullptr)
		<< "the table box is an ibValueModelTableBox";
}

// AddColumn() adds a table-box-column child of the correct type — the mechanic
// the list/TSD screen leans on to build its columns.
TEST_F(FrontendFormFix, DISABLED_TableBoxAddColumnCreatesColumnChild)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* tb = form->NewObject(g_controlTableBoxCLSID);
	ASSERT_NE(tb, nullptr);
	ASSERT_NE(dynamic_cast<ibValueModelTableBox*>(tb), nullptr);

	// AddColumn() builds a column child of this clsid under the table box; do it
	// through NewObject (the same factory it uses) to stay off the non-exported
	// AddColumn symbol.
	const unsigned before = CountChildrenOfType(tb, g_controlTableBoxColumnCLSID);
	form->NewObject(g_controlTableBoxColumnCLSID, tb);
	const unsigned after = CountChildrenOfType(tb, g_controlTableBoxColumnCLSID);
	EXPECT_EQ(after, before + 1) << "a column child is added under the table box";
}

// Two AddColumn calls yield two distinct column children.
TEST_F(FrontendFormFix, DISABLED_TableBoxAddsMultipleColumns)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* tb = form->NewObject(g_controlTableBoxCLSID);
	ASSERT_NE(dynamic_cast<ibValueModelTableBox*>(tb), nullptr);

	form->NewObject(g_controlTableBoxColumnCLSID, tb);
	form->NewObject(g_controlTableBoxColumnCLSID, tb);
	EXPECT_EQ(CountChildrenOfType(tb, g_controlTableBoxColumnCLSID), 2u);
}

// ----------------------------------- TextBox ---------------------------------

// The text box builds as ibValueTextBox and identifies by clsid.
TEST_F(FrontendFormFix, DISABLED_TextBoxBuildsAsModelType)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* tx = form->NewObject(g_ctrlTextBoxCLSID);
	ASSERT_NE(tx, nullptr);
	EXPECT_EQ(tx->GetClassType(), g_ctrlTextBoxCLSID);
	EXPECT_NE(dynamic_cast<ibValueTextBox*>(tx), nullptr)
		<< "the text box is an ibValueTextBox";
}

// The text box is a container — a control built into it becomes its child.
TEST_F(FrontendFormFix, DISABLED_TextBoxHoldsChildControls)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* tx = form->NewObject(g_ctrlTextBoxCLSID);
	ASSERT_NE(tx, nullptr);

	ibValueFrame* child = form->NewObject(g_ctrlButtonCLSID, tx);
	ASSERT_NE(child, nullptr);
	EXPECT_GT(tx->GetChildCount(), 0u) << "a control built into the text box is contained by it";
}

// ----------------------------------- GridBox ---------------------------------

// The grid box builds as ibValueGridBox and identifies by clsid.
TEST_F(FrontendFormFix, DISABLED_GridBoxBuildsAsModelType)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* gb = form->NewObject(g_ctrlGridBoxCLSID);
	ASSERT_NE(gb, nullptr);
	EXPECT_EQ(gb->GetClassType(), g_ctrlGridBoxCLSID);
	EXPECT_NE(dynamic_cast<ibValueGridBox*>(gb), nullptr)
		<< "the grid box is an ibValueGridBox";
}

// The grid box is a container — a control built into it becomes its child.
TEST_F(FrontendFormFix, DISABLED_GridBoxHoldsChildControls)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* gb = form->NewObject(g_ctrlGridBoxCLSID);
	ASSERT_NE(gb, nullptr);

	ibValueFrame* child = form->NewObject(g_ctrlButtonCLSID, gb);
	ASSERT_NE(child, nullptr);
	EXPECT_GT(gb->GetChildCount(), 0u) << "a control built into the grid box is contained by it";
}

// ----------------------------------- Notebook --------------------------------

// AddNotebookPage() adds a notebook-page child — the tabbed-container mechanic.
TEST_F(FrontendFormFix, DISABLED_NotebookAddPageCreatesPageChild)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ibValueFrame* nb = form->NewObject(g_controlNotebookCLSID);
	ASSERT_NE(dynamic_cast<ibValueNotebook*>(nb), nullptr);
	EXPECT_EQ(nb->GetClassType(), g_controlNotebookCLSID);

	// AddNotebookPage() builds a page child; create it via NewObject (its own
	// factory) to stay off the non-exported AddNotebookPage symbol.
	const unsigned before = CountChildrenOfType(nb, g_controlNotebookPageCLSID);
	form->NewObject(g_controlNotebookPageCLSID, nb);
	EXPECT_EQ(CountChildrenOfType(nb, g_controlNotebookPageCLSID), before + 1)
		<< "a page child is added under the notebook";
}

// ------------------------------ Registry coverage ----------------------------

// Every registered control type builds through NewObject, identifies by the
// requested clsid, and classifies as a Control kind (high byte of the clsid).
TEST_F(FrontendFormFix, DISABLED_AllRegisteredControlsBuildAndClassify)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	const struct { const wxChar* label; ibClassID clsid; } specs[] = {
		{ wxT("button"),      control_to_clsid("CT_BUTN")  },
		{ wxT("checkbox"),    control_to_clsid("CT_CHKB")  },
		{ wxT("statictext"),  control_to_clsid("CT_STTX")  },
		{ wxT("staticline"),  control_to_clsid("CT_STLI")  },
		{ wxT("slider"),      control_to_clsid("CT_SLID")  },
		{ wxT("gauge"),       control_to_clsid("CT_GAUG")  },
		{ wxT("radiobutton"), control_to_clsid("CT_RDBT")  },
		{ wxT("boxsizer"),    control_to_clsid("CT_BSZR")  },
		{ wxT("gridsizer"),   control_to_clsid("CT_GSZR")  },
		{ wxT("htmlbox"),     control_to_clsid("CT_HTML")  },
		{ wxT("chartbox"),    control_to_clsid("CT_CHRB")  },
	};

	for (const auto& s : specs) {
		EXPECT_TRUE(IsControl(s.clsid)) << s.label << ": clsid classifies as a Control kind";
		ibValueFrame* ctrl = form->NewObject(s.clsid);
		ASSERT_NE(ctrl, nullptr) << "NewObject must build a " << s.label;
		EXPECT_EQ(ctrl->GetClassType(), s.clsid) << s.label << ": identity by clsid";
	}
}

// ------------------------------ Form serialize -------------------------------

// A form with controls round-trips through SaveForm -> LoadForm: the rebuilt
// form owns the same number of controls.
TEST_F(FrontendFormFix, DISABLED_FormSerializeRoundTripPreservesControls)
{
	if (!frameReady) GTEST_SKIP();

	ibValuePtr<ibValueForm> src(new ibValueForm());
	src->NewObject(g_ctrlButtonCLSID);
	src->NewObject(control_to_clsid("CT_CHKB"));
	const size_t srcCount = src->GetControlList().size();
	ASSERT_GE(srcCount, 2u) << "the source form has the controls we added";

	wxMemoryBuffer buffer;
	ASSERT_TRUE(src->SaveForm(buffer)) << "SaveForm serialises the control tree";

	ibValuePtr<ibValueForm> dst(new ibValueForm());
	ASSERT_TRUE(dst->LoadForm(buffer)) << "LoadForm rebuilds the control tree";
	EXPECT_EQ(dst->GetControlList().size(), srcCount)
		<< "the round-tripped form owns the same controls";
}

// ------------------------------ clsid kind-typing ----------------------------
// Pure classification helpers (clsid.h) — no fixture, no wxApp: they read the
// high byte of the id. Control identity is by kind, never by C++ RTTI.

TEST(FrontendClsidKind, ControlClsidsClassifyAsControl)
{
	EXPECT_TRUE(IsControl(control_to_clsid("CT_BUTN")));
	EXPECT_TRUE(IsControl(g_controlFormCLSID));
	EXPECT_EQ(clsid_kind(control_to_clsid("CT_BUTN")), ibClassKind_Control);
}

TEST(FrontendClsidKind, ControlKindExcludesOtherKinds)
{
	const ibClassID button = control_to_clsid("CT_BUTN");
	EXPECT_FALSE(IsMetadata(button));
	EXPECT_FALSE(IsValue(button));
	EXPECT_FALSE(IsReference(button));
	EXPECT_FALSE(IsPrimitive(button));
}
