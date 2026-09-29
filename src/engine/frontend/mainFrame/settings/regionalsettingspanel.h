#ifndef __REGIONAL_SETTINGS_PANEL_H__
#define __REGIONAL_SETTINGS_PANEL_H__

////////////////////////////////////////////////////////////////////////////
// THE BASE'S REGIONAL SETTINGS - a page of the designer's settings dialog.
//
// The zone the base's clock stands in and the locale its dates and numbers print in. Unlike the
// editor pages, and like the assistant's, this is not the frame's own state: it belongs to the
// BASE, to every client of it, and is read from and written back to the base (ibRegionalSettings).
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"
#include "backend/session/regionalSettings.h"

#include <wx/panel.h>
#include <wx/combobox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

class FRONTEND_API ibPanelRegionalSettings : public wxPanel
{
public:
	ibPanelRegionalSettings(wxWindow* parent, int id = wxID_ANY,
		wxPoint pos = wxDefaultPosition, wxSize size = wxSize(461, 438), int style = wxTAB_TRAVERSAL);

	void Initialize();

	void                      SetSettings(const ibRegionalSettings& settings);
	// What the page says now - read off the controls, so the caller sees the person's edits.
	const ibRegionalSettings& GetSettings();

	// The locale the page holds is a tag wx can read (uk-UA), or is empty. The zone is checked by
	// the base's server when the settings are saved, and its refusal is what the person sees.
	bool LocaleIsKnown() const;

private:
	ibRegionalSettings m_settings;

	wxComboBox*   m_zoneCtrl   = nullptr;
	wxTextCtrl*   m_localeCtrl = nullptr;
	wxStaticText* m_zoneNote   = nullptr;
};

#endif
