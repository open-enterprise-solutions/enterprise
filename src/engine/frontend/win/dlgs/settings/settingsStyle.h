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
#include <wx/bookctrl.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/window.h>

#include "frontend/artProvider/artProvider.h"   // the tabs' pictures (client wxART_FRONTEND)
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

// A TAB WEARS ITS PICTURE, the same tab the same one in both worlds. Every settings notebook is handed
// the whole set; a page names its picture by what the page IS, so a page taken off and put back (the
// grouping, the reader's parameters) finds it again without counting.
enum class ibSettingsTab { Query, Fields, Resources, Parameters, Output, Grouping, SelectedFields, Filter, Sort,
	OtherSettings };

inline void ibStyleSettingsTabs(wxBookCtrlBase* tabs)
{
	static const wxString s_art[] = { wxART_QUERY_CONSTRUCTOR, wxART_TABLE, wxART_TOTALS, wxART_PARAMETERS,
		wxART_OUTPUT, wxART_GROUPING, wxART_SELECTED_FIELDS, wxART_FILTER, wxART_SORT,
		wxART_ADVANCED };   // in ibSettingsTab's order
	wxWithImages::Images images;
	for (const wxString& id : s_art)   // a bundle's size is at normal DPI — no FromDIP
		images.push_back(wxArtProvider::GetBitmapBundle(id, wxART_FRONTEND, wxSize(16, 16)));
	tabs->SetImages(images);
}

inline int ibSettingsTabArt(ibSettingsTab tab)
{
	return static_cast<int>(tab);
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
