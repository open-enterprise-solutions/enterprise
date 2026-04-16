#ifndef _IB_WEB_MAIN_FRAME_H__
#define _IB_WEB_MAIN_FRAME_H__

#include "backend/backend_mainFrame.h"

///////////////////////////////////////////////////////////////////////////
// Minimal ibBackendDocMDIFrame for daemon/web server mode.
// Enables CreateNewForm() without GUI — forms live in memory,
// rendered as JSON for the web client.
///////////////////////////////////////////////////////////////////////////

class ibWebMainFrame : public ibBackendDocMDIFrame {
public:

	ibWebMainFrame();
	virtual ~ibWebMainFrame();

	// Pure virtual implementations — no GUI, just stubs
	virtual wxFrame* GetFrameHandler() const override { return nullptr; }
	virtual void SetTitle(const wxString& strTitle) override {}
	virtual void SetStatusText(const wxString& strTitle, int number = 0) override {}
	virtual void RefreshFrame() override {}
	virtual void RaiseFrame() override {}

	// Form factory — creates ibValueForm in memory (no GUI window)
	virtual ibBackendValueForm* CreateNewForm(
		const ibValueMetaObjectFormBase* creator,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr,
		const ibUniqueKey& formGuid = wxNullUniqueKey) override;

	static void Initialize();
	static void Destroy();

private:
	static ibWebMainFrame* ms_instance;
};

#endif // _IB_WEB_MAIN_FRAME_H__
