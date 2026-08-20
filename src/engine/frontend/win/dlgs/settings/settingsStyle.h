#ifndef __SETTINGS_STYLE_H__
#define __SETTINGS_STYLE_H__

// ---------------------------------------------------------------------------
// How a settings surface LOOKS — shared by both worlds under this folder.
//
// A verb wears one picture wherever it appears, and a settings grid reads as a
// grid. Spelled once here rather than per window: the two roads to the same
// command (a toolbar and a context menu) drifted the moment each spelled its own
// art, and the tables took the dialog's flat grey until a rule said otherwise.
// ---------------------------------------------------------------------------

#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/window.h>

#include "frontend/win/ctrls/dataview/dataview.h"

// ONE PICTURE PER VERB. Art ids, not files: they follow the platform's theme, as
// the rest of the shell does.
inline wxBitmapBundle ibSettingsArt(const wxString& artId, const wxWindow* owner)
{
	return wxArtProvider::GetBitmapBundle(artId, wxASCII_STR(wxART_MENU),
		owner != nullptr ? owner->FromDIP(wxSize(16, 16)) : wxSize(16, 16));
}

// Append a command that LOOKS the same wherever it appears.
inline wxMenuItem* ibAppendCmd(wxMenu& menu, int id, const wxString& label,
	const wxString& artId, const wxWindow* owner)
{
	wxMenuItem* item = menu.Append(id, label);
	if (item != nullptr)
		item->SetBitmap(ibSettingsArt(artId, owner));
	return item;
}

// THE TABLES READ AS TABLES. They used to take the dialog's own grey background,
// which made the grid and the panel one flat surface — the rows had no field of
// their own to sit on. A list background (the system's own, so it follows the
// theme) plus a faint alternating row makes the data area obvious without drawing
// a single border.
inline void ibStyleSettingsGrid(ibDataViewCtrl* view)
{
	if (view == nullptr)
		return;
	view->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
	view->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT));

	// A TINT OF THE BACKGROUND, not a fixed grey: on a dark theme the same rule
	// lightens instead of darkening, so the banding stays subtle either way.
	const wxColour base = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
	const bool dark = (base.Red() + base.Green() + base.Blue()) < 3 * 128;
	view->SetAlternateRowColour(dark ? base.ChangeLightness(115) : base.ChangeLightness(96));
}

// A virtual-list row id is 1-based — the cursor follows what was just added.
inline void ibSelectLastSettingsRow(ibDataViewCtrl* view, size_t count)
{
	if (view == nullptr || count == 0)
		return;
	view->Select(ibDataViewItem(reinterpret_cast<void*>(count)));
	view->EnsureVisible(ibDataViewItem(reinterpret_cast<void*>(count)));
}

#endif // __SETTINGS_STYLE_H__
