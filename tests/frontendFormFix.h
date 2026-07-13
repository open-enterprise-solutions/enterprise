// =============================================================================
// FrontendFormFix — the shared fixture for any test that touches a form or a
// control.
//
// A form CANNOT be constructed with `new ibValueForm()`: its ctor reaches for
// the active frame/session context and throws "Context functions are not
// available!" without one. The supported path is ibBackendValueForm::
// CreateNewForm(), which routes through ibSession::CurrentFrame(). So form/
// control tests need a live frame bound as the current session's frame — which
// is exactly what this fixture stands up (on top of the wxApp + env + pool from
// FrontendRuntimeFix). Build forms with NewForm().
// =============================================================================

#pragma once

#include "frontendFix.h"

#include "frontend/mainFrame/mainFrame.h"        // ibFrontendMainFrame
#include "frontend/session/guiSession.h"         // ibGUISession
#include "frontend/visualView/ctrl/form.h"       // ibValueForm
#include "backend/backend_form.h"                // ibBackendValueForm::CreateNewForm
#include "backend/session/session.h"             // ibSessionThreadBinding / AccessMode

#include <memory>

// Minimal concrete main frame — closes ibFrontendMainFrame's only pure virtual.
class ibTestMainFrame : public ibFrontendMainFrame {
public:
	ibTestMainFrame() : ibFrontendMainFrame(wxT("oes-test-frame")) {}
	void CreateGUI() override {}
};

// Runtime harness + a live frame bound as the current session's frame, so
// CurrentFrame() resolves and CreateNewForm has somewhere to route.
struct FrontendFormFix : FrontendRuntimeFix {
	std::shared_ptr<ibGUISession>           session;
	ibTestMainFrame*                        frame = nullptr;
	std::unique_ptr<ibSessionThreadBinding> binding;
	bool frameReady = false;

	void SetUp() override {
		FrontendRuntimeFix::SetUp();          // wxApp + appData env + SQLite pool
		if (!ready) return;

		ibSession::SetAccessMode(ibSession::AccessMode::Shared);
		session = std::make_shared<ibGUISession>(wxT("test-session"), ibSessionKind::Enterprise);
		frame   = new ibTestMainFrame();
		session->AttachFrame(frame);
		binding = std::make_unique<ibSessionThreadBinding>(session.get());

		frameReady = (ibSession::CurrentFrame() != nullptr);
	}

	void TearDown() override {
		binding.reset();
		if (frame != nullptr) { frame->Destroy(); frame = nullptr; }
		session.reset();
		FrontendRuntimeFix::TearDown();
	}

	// The supported way to build a form in a test. Returns nullptr if the frame
	// didn't come up (caller GTEST_SKIPs on !frameReady first).
	ibValueForm* NewForm() {
		return dynamic_cast<ibValueForm*>(ibBackendValueForm::CreateNewForm());
	}
};
