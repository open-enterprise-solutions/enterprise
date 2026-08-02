// =============================================================================
// Frontend GUI harness — STEP 1 (the seam itself).
//
// The whole GUI-test track hinges on ONE unknown: does frontend.dll link into a
// gtest target, and do a live GUI wxApp + the backend runtime env come up
// together inside the test process? Everything else — creating a form, walking
// its control tree, asserting a widget was built and bound — stacks on top of
// that. So this first target proves exactly that seam, nothing more.
//
// Runtime host, because the first client is a runtime control (warehouse / TSD
// list), not the designer editor. The designer-mode harness (eDESIGNER_MODE) is
// a sibling target added once this one is green.
//
// NOTE — forms need a concrete main frame. ibFrontendMainFrame is abstract
// (CreateGUI()=0) and the concrete frames (ibFrontendMainFrameEnterprise /
// ...Designer) live in the exe'shki and are NOT exported from frontend.dll, so
// ibBackendValueForm::CreateNewForm (which routes through
// ibSession::CurrentFrame()) has no frame to reach from a frontend-only link.
// Standing that frame up in-test is STEP 2 — deliberately deferred so the link
// + bring-up unknowns get resolved on a green base first.
// =============================================================================

#include "frontendFormFix.h"                     // FrontendFormFix + NewForm()

#include "frontend/visualView/ctrl/widgets.h"   // g_controlTextCtrlCLSID / g_controlCheckboxCLSID
#include "frontend/visualView/ctrl/tableBox.h"  // g_controlTableBoxCLSID (the TSD/list control)
#include "backend/compiler/value.h"             // ibValue / ibNumber / g_value*CLSID

// No local clsid constants here. `g_controlButtonCLSID` now lives in widgets.h as a
// GLOBAL (the drag-to-create arc made the three drop-target clsids shared constants), so
// re-declaring it in an anonymous namespace made every use ambiguous — a compile error
// that stayed hidden while this target went unbuilt. Use the shared ones; only the sizer
// id, which widgets.h does not carry, is spelled out.
namespace {
constexpr ibClassID g_testBoxSizerCLSID = control_to_clsid("CT_BSZR");
} // namespace

// Definition of the environment back-pointer declared in the fixture header.
ibWxGuiEnvironment* ibWxGuiEnvironment::s_instance = nullptr;

namespace {
// Register the GUI toolkit environment before RUN_ALL_TESTS. gtest_main owns
// main(), so this file-scope hook is how we inject a process-wide SetUp/TearDown
// without our own main().
const bool s_guiEnvRegistered = [] {
	auto* env = new ibWxGuiEnvironment();
	ibWxGuiEnvironment::s_instance = env;
	::testing::AddGlobalTestEnvironment(env);
	return true;
}();
} // namespace

// The seam: a GUI wxApp and the backend runtime env are both live, and the
// SQLite pool hands out a real connection holder — the ground every later
// form/control test stands on.
TEST_F(FrontendRuntimeFix, HarnessBringsUpGuiAndBackend)
{
	if (!ready) GTEST_SKIP();

	EXPECT_NE(wxTheApp, nullptr)
		<< "a live GUI wxApp is required — desktop controls are wxWindow subclasses";
	EXPECT_NE(ibApplicationData::Get(), nullptr)
		<< "runtime-mode appData env must be up";
	EXPECT_NE(ibApplicationData::GetConnectionPool(), nullptr)
		<< "the connection pool must be wired after CreateAppDataEnv";
	EXPECT_NE(ibConnectionPool::ThreadHolder(), nullptr)
		<< "the pool must hand out the master connection holder";
}

// -----------------------------------------------------------------------------
// STEP 2 — the runtime control MODEL (no wxWindow yet).
//
// A form is a plain refcounted value object: its ctor takes all-default args, so
// it constructs WITHOUT a main frame and without ibSession::CurrentFrame() (the
// path CreateNewForm needs). NewObject(clsid, form) builds a control model inside
// it — and the PARENT argument is not optional in practice: ibValueFrame::Init only
// calls AddChild when it gets one, so NewObject(clsid) alone yields a control that
// belongs to no tree. It is not in GetControlList, the visual-host walker never
// reaches it, and everything downstream that assumes a parented control is off the
// map. Always pass the form (or a container control) as the parent.
// This is the control layer the warehouse / TSD list sits on, exercised without
// a frame, a display, or ShowForm — the wxWindow tree only materialises later at
// CreateVisualHost time. Control identity is by clsid, never C++ RTTI.
// -----------------------------------------------------------------------------

// A bare form constructs as a value object and identifies as the Form control
// type, owning no child controls yet.
TEST_F(FrontendFormFix, FormConstructsAsValueObject)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ASSERT_NE(form, nullptr);
	EXPECT_EQ(form->GetClassType(), g_controlFormCLSID)
		<< "a form identifies as the Form control type (by clsid)";
}

// NewObject(clsid) yields a control whose type identity (GetClassType) is the
// requested clsid — text box, checkbox, and the table box (the list control the
// TSD screen is built from).
TEST_F(FrontendFormFix, FormNewObjectBuildsControlOfRequestedType)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ASSERT_NE(form, nullptr);

	const struct { const wxChar* label; ibClassID clsid; } kinds[] = {
		{ wxT("textctrl"), g_controlTextCtrlCLSID },
		{ wxT("checkbox"), g_controlCheckboxCLSID },
		{ wxT("tablebox"), g_controlTableBoxCLSID },
	};

	for (const auto& k : kinds) {
		ibValueFrame* ctrl = form->NewObject(k.clsid, form);
		ASSERT_NE(ctrl, nullptr)
			<< "NewObject must build a control for clsid " << k.label;
		EXPECT_EQ(ctrl->GetClassType(), k.clsid)
			<< k.label << ": the built control identifies by the requested clsid";
	}
}

// A control built into a container parent still identifies by its own clsid —
// nesting (a button inside a box sizer) does not change type identity. This is
// the shape a real form tree takes: containers holding widgets.
TEST_F(FrontendFormFix, FormNestsControlUnderContainer)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ASSERT_NE(form, nullptr);

	ibValueFrame* box = form->NewObject(g_testBoxSizerCLSID, form);
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(box->GetClassType(), g_testBoxSizerCLSID);

	ibValueFrame* button = form->NewObject(g_controlButtonCLSID, box);
	ASSERT_NE(button, nullptr) << "a button must build inside the box-sizer parent";
	EXPECT_EQ(button->GetClassType(), g_controlButtonCLSID);
}

// A built control is owned by the form and can be removed again — the model
// supports the create/remove churn the designer and runtime both drive.
TEST_F(FrontendFormFix, FormRemovesBuiltControl)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ASSERT_NE(form, nullptr);

	const size_t before = form->GetControlList().size();
	ibValueFrame* button = form->NewObject(g_controlButtonCLSID, form);
	ASSERT_NE(button, nullptr);
	EXPECT_EQ(form->GetControlList().size(), before + 1) << "the form owns the new control";

	form->RemoveControl(button);
	EXPECT_EQ(form->GetControlList().size(), before)
		<< "RemoveControl drops the control from the form";
}
