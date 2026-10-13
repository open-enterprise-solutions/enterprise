#ifndef __REGIONAL_SETTINGS_PANEL_H__
#define __REGIONAL_SETTINGS_PANEL_H__

////////////////////////////////////////////////////////////////////////////
// THE BASE'S TIME ZONE - a page of the designer's settings dialog.
//
// Unlike the editor pages, and like the assistant's, this is not the frame's own state: it belongs to the
// BASE, to every client of it, and is read from and written back to the base (ibRegionalSettings). The
// page only shows and edits; the frame hands it what the base holds and saves what it returns.
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"

#include <wx/panel.h>
#include <wx/combobox.h>
#include <wx/stattext.h>

#include <vector>

class FRONTEND_API ibPanelRegionalSettings : public wxPanel
{
public:
	ibPanelRegionalSettings(wxWindow* parent, int id = wxID_ANY,
		wxPoint pos = wxDefaultPosition, wxSize size = wxSize(461, 438), int style = wxTAB_TRAVERSAL);

	void Initialize();

	// The zone in force and the names the base's server knows (empty where it cannot say: the name is typed).
	void SetTimeZone(const wxString& zone, const std::vector<wxString>& known);
	// What the page says now - read off the control, so the caller sees the person's edit.
	wxString GetTimeZone() const;

private:
	wxString              m_zone;
	std::vector<wxString> m_known;

	wxComboBox*   m_zoneCtrl = nullptr;
	wxStaticText* m_zoneNote = nullptr;
};

#endif
