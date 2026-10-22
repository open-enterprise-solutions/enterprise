#ifndef _FRMCLIENT_VIEW_PANEL_H__
#define _FRMCLIENT_VIEW_PANEL_H__

#include "frmclient/visualView/ctrl/window.h"

class ibPanelPage;

// THE PANEL OF A VIEW — the desktop's (frontend/visualView/ctrl/panel.h): a plain window with its own sizer, the
// controls in it laid out there, shown while the State says it is Visible. Its properties are the server's.
class ibValuePanel : public ibValueWindow {
public:
	ibValuePanel(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;
	// The panel lays its controls out in its own sizer (ibPanelPage).
	virtual wxSizer*  GetSizer() const override;

private:

	ibPanelPage* m_panel = nullptr;
};

#endif
