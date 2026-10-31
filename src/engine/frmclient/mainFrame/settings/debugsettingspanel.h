#ifndef DEBUG_SETTINGS_PANEL_H
#define DEBUG_SETTINGS_PANEL_H

#include <wx/wx.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// THE APPLICATION'S DEBUGGING PAGE OF THE OPTIONS: its box read from the server's schema
// (frmserver/schema/runtime/debugging.cpp) and switched by the schema's Start and Stop, at once, not on OK.
class FRMCLIENT_API ibPanelDebugSettings : public wxPanel
{
public:

	ibPanelDebugSettings(wxWindow* parent, const ibProtocolNode& shown);

private:

	void FillOption(const ibProtocolNode& shown);
	void OnEnableDebugger(wxCommandEvent& event);

	wxCheckBox* m_enableDebugger = nullptr;
};

#endif
