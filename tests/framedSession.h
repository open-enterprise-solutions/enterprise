// A session with a frame, made in a test — what a form is born against. A form asks its session's frame for its key
// (ibBackendValueForm::CreateFormUniqueKey) and, made by name, for itself (CreateNewForm); a process with no frame
// refuses both, by design ("a server has no forms"), so a test that makes a form gives its session one.
//
// Minted UNLISTED, as a harness's session is: a listed one gives itself back to its base's registry as it goes, and a
// test's registry was never started — the request kept the session alive into the static teardown (ASan:
// heap-use-after-free; macOS: mutex lock failed).
//
//   auto session = std::make_shared<FramedSession>(wxT("name"), ibSessionKind::Designer);
//   KeyFrame frame(ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session));
//   session->Attach(&frame);
//   ibSessionScope scope(session.get());

#ifndef _IB_TEST_FRAMED_SESSION_H_
#define _IB_TEST_FRAMED_SESSION_H_

#include "backend/backend_mainFrame.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"   // MintUnlisted — a harness's session
#include "frmserver/visualView/ctrl/form.h"    // ibValueForm — what the frame makes

// The frame: names a key and makes the form and nothing more, as the client host's (ibClientFrame::CreateNewForm).
struct KeyFrame : ibBackendDocFrame {
	explicit KeyFrame(ibSessionHolder&& holder) : ibBackendDocFrame(std::move(holder)) {}
	void SetTitle(const ibString&) override {}
	void SetStatusText(const ibString&, int) override {}
	void RefreshFrame() override {}
	void RaiseFrame() override {}
	ibBackendValueForm* CreateNewForm(const ibFormRequest& request, const ibValueMetaObjectFormBase* creator,
		ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject) override
	{
		return new ibValueForm(request, creator, dynamic_cast<ibControlFrame*>(ownerControl), srcObject);
	}
	ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame*,
		const ibSourceDataObject*, const ibUniqueKey& formGuid) override
	{
		if (formGuid.isValid())
			return formGuid;
		ibUniqueKey made = ibNewUniqueGuid;
		return made;
	}
};

// The session the frame is attached to.
struct FramedSession : ibSession {
	using ibSession::ibSession;
	ibBackendDocFrame* GetFrame() const override { return m_frame; }
	void Attach(ibBackendDocFrame* frame) { m_frame = frame; }
	ibBackendDocFrame* m_frame = nullptr;
};

#endif
