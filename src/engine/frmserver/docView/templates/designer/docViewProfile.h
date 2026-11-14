#ifndef _DESIGNER_PROFILE_H__
#define _DESIGNER_PROFILE_H__

// WHAT A MEASUREMENT MEASURED, AS A TAB (ShowProfile) — opened as the person's Debug → Performance measurement ends
// (ibClientDebugger::OnProfile), or as the assistant asks (profile_stop / profile_read {show}); the registration journal's
// shape: no metaobject, an invisible template the designer's manager registers, the plain ibDocument/ibView pair. The
// document keeps what it was handed and is named by it; the client's view (ViewKind Profile) fetches it whole and draws
// it, a line's row opens its module there.

#include "frmserver/docView/docView.h"

#include "core/debugger/debugDefs.h"   // ibProfileData — what it keeps

class FRMSERVER_API ibProfileDocument : public ibDocument {
public:

	explicit ibProfileDocument(const ibProfileData& profile = ibProfileData());

	ibProfileDocument(const ibProfileDocument&) = delete;
	ibProfileDocument& operator=(const ibProfileDocument&) = delete;

	// What was measured is kept as it was — never prompts on close, never tracks dirty state.
	virtual bool IsModified() const override { return false; }
	virtual void Modify(bool) override {}

	const ibProfileData& GetProfile() const { return m_profile; }

	// What it measured, as a tab of the client whose session runs this — none: no client here.
	static ibProfileDocument* ShowProfile(const ibProfileData& profile);

protected:

	virtual bool DoSaveDocument(const ibString&) override { return true; }
	virtual bool DoOpenDocument(const ibString&) override { return true; }

private:

	const ibProfileData m_profile;
};

class FRMSERVER_API ibProfileView : public ibView {
public:

	ibProfileView() : ibView() {}

	// Nothing of it is drawn into the frame — the client's view fetches it.
	virtual void OnDraw(ibDataNode& /*frame*/) override {}

	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::Profile; }
	// The document's measurement, whole (ibProtocol::ibView::ibProfile).
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;
};

#endif
