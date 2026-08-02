// =============================================================================
// Visual host — ibVisualHostClient (the container that owns one open form's
// control tree and renders it into wxWidgets).
//
// On desktop ibVisualHost derives from wxScrolledCanvas, so the host IS a
// wxWindow and needs a real parent window. It keeps an ibValueFrame -> wxObject
// map (AppendInnerControl / GetWxObject / GetObjectBase) and builds the whole
// wx tree in CreateVisualHost. These tests drive both: the map round-trip and a
// full CreateAndUpdateVisualHost that materialises a control into a widget.
//
// Forms come from NewForm() (CreateNewForm through the bound frame) — a form
// cannot be `new`ed directly. Lifetime note: the test process has no wx event
// loop, so wxWindow::Destroy() (deferred to idle) never runs; the host +
// document are DELIBERATELY leaked to avoid a dtor-ordering UAF. Leaking in a
// short-lived test process is the safe choice here, not a bug.
// =============================================================================

#include "frontendFormFix.h"

#include "frontend/visualView/visualHostClient.h"  // ibVisualHostClient + ibFormVisualDocument
#include "backend/compiler/value.h"                // control_to_clsid

#include <wx/frame.h>

namespace {

constexpr ibClassID g_hostButtonCLSID = control_to_clsid("CT_BUTN");

// Form harness + a real parent wxWindow to host the visual canvas under.
struct VisualHostFix : FrontendFormFix {
	wxFrame* parent = nullptr;

	void SetUp() override {
		FrontendFormFix::SetUp();             // wxApp + env + pool + bound frame
		if (!frameReady) return;
		parent = new wxFrame(nullptr, wxID_ANY, wxT("visual-host-parent"));
	}
	void TearDown() override {
		if (parent != nullptr) {
			parent->Destroy();                // takes its (leaked) host children with it
			parent = nullptr;
		}
		FrontendFormFix::TearDown();
	}

	// Build a host over a fresh form (+ optional one control). Host + document are
	// intentionally leaked (see file header); the form is owned by the frame.
	ibVisualHostClient* MakeHost(ibValueForm*& form, ibValueFrame** outCtrl = nullptr) {
		form = NewForm();
		if (form == nullptr) return nullptr;
		// The control is PARENTED TO THE FORM. Passing no parent leaves it an orphan:
		// ibValueFrame::Init only calls AddChild when a parent is given, so the control
		// never enters the form's tree — GetControlList / the host walker never see it.
		ibValueFrame* ctrl = form->NewObject(g_hostButtonCLSID, form);
		if (outCtrl != nullptr) *outCtrl = ctrl;
		auto* doc = new ibFormVisualDocument(form);         // leaked
		return new ibVisualHostClient(doc, form, parent);   // leaked (parent child)
	}
};

} // namespace

// The host exposes the form it was built over.
TEST_F(VisualHostFix, HostExposesItsForm)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = nullptr;
	ibVisualHostClient* host = MakeHost(form);
	ASSERT_NE(host, nullptr);
	EXPECT_EQ(host->GetValueForm(), form)
		<< "the host reports the form whose tree it owns";
}

// The ibValueFrame <-> wxObject map round-trips: append a mapping, resolve it
// both ways, then remove it.
TEST_F(VisualHostFix, HostControlMapRoundTrips)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = nullptr;
	ibValueFrame* button = nullptr;
	ibVisualHostClient* host = MakeHost(form, &button);
	ASSERT_NE(host, nullptr);
	ASSERT_NE(button, nullptr);

	// parent is any wxObject — used here purely as the mapped value.
	host->AppendInnerControl(button, parent);
	EXPECT_EQ(host->GetWxObject(button), parent) << "forward lookup: control -> wxObject";
	EXPECT_EQ(host->GetObjectBase(parent), button) << "reverse lookup: wxObject -> control";

	host->RemoveInnerControl(button);
	EXPECT_EQ(host->GetWxObject(button), nullptr) << "removed mapping no longer resolves";
}

// The full walker builds the wx tree: after CreateAndUpdateVisualHost the form's
// control has a materialised wx widget registered in the host's map.
TEST_F(VisualHostFix, CreateVisualHostMaterialisesControl)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = nullptr;
	ibValueFrame* button = nullptr;
	ibVisualHostClient* host = MakeHost(form, &button);
	ASSERT_NE(host, nullptr);
	ASSERT_NE(button, nullptr);

	EXPECT_TRUE(host->CreateAndUpdateVisualHost())
		<< "the host walker builds the form's wx tree";
	EXPECT_NE(host->GetWxObject(button), nullptr)
		<< "the button control materialised into a wx widget in the host map";
}
