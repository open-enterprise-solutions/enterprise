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
#include "backend/appData.h"                     // appData->CreateSession<T>()
#include "backend/session/session.h"             // ibSessionThreadBinding / AccessMode
#include "backend/session/sessionHolder.h"       // ibSessionHolder — the frame takes one
#include "backend/session/sessionRegistry.h"     // the CreateSession<T> template body

#include <memory>

// Minimal concrete main frame — closes ibFrontendMainFrame's only pure virtual.
//
// THE SESSION COMES IN WITH THE WINDOW. The frame's ctor takes an ibSessionHolder and,
// inside, takes ownership, wires the session's back-link and registers itself as the
// process main window — so there is no AttachFrame to call afterwards (there used to be;
// it is gone). A test frame therefore has to forward a holder, exactly as
// ibMainFrameEnterprise / ibMainFrameDesigner do.
class ibTestMainFrame : public ibFrontendMainFrame {
public:
	explicit ibTestMainFrame(ibSessionHolder&& holder)
		: ibFrontendMainFrame(std::move(holder), wxT("oes-test-frame")) {}
	void CreateGUI() override {}
};

// Runtime harness + a live frame owning the session, so CurrentFrame() resolves and
// CreateNewForm has somewhere to route.
struct FrontendFormFix : FrontendRuntimeFix {
	ibTestMainFrame*                        frame = nullptr;
	std::unique_ptr<ibSessionThreadBinding> binding;
	bool frameReady = false;

	void SetUp() override {
		FrontendRuntimeFix::SetUp();          // wxApp + appData env + SQLite pool
		if (!ready) return;

		ibSession::SetAccessMode(ibSession::AccessMode::Shared);

		// An UNLISTED session — imitated, not registered. The registered path
		// (appData->CreateSession<T>()) makes the registry own sys_session I/O, and this
		// harness runs against a bare SQLite :memory: database that has no system schema:
		// every session then failed its INSERT on a missing table and paid a connection
		// timeout for it — ~30 SECONDS per test, for a row no assertion reads. Nothing here
		// wants a registry row; it wants a session object a frame can own. So mint one the
		// way a background job's rented read does.
		//
		// Bind the THREAD before handing the holder over — after the move it is empty and
		// the frame owns the session.
		ibSessionHolder holder = ibSessionRegistry::MintUnlisted(
			std::make_shared<ibGUISession>(wxString(wxNewUniqueGuid), ibSessionKind::Enterprise));
		if (!holder) return;                  // frameReady stays false
		binding = std::make_unique<ibSessionThreadBinding>(holder.Get());
		frame   = new ibTestMainFrame(std::move(holder));

		frameReady = (ibSession::CurrentFrame() != nullptr);
	}

	void TearDown() override {
		binding.reset();
		// Destroying the frame releases the holder it took, which tears the session down.
		if (frame != nullptr) { frame->Destroy(); frame = nullptr; }
		FrontendRuntimeFix::TearDown();
	}

	// The supported way to build a form in a test. Returns nullptr if the frame
	// didn't come up (caller GTEST_SKIPs on !frameReady first).
	ibValueForm* NewForm() {
		return dynamic_cast<ibValueForm*>(ibBackendValueForm::CreateNewForm());
	}
};
