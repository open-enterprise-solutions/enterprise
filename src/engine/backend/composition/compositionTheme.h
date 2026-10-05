#ifndef __COMPOSITION_THEME_H__
#define __COMPOSITION_THEME_H__

// ---------------------------------------------------------------------------
// A REPORT'S THEME — the palette it is painted in (Max, 2026-09-29: "themes are a colour palette: defined by
// default, and one can make one's own").
//
// A report is read by its STRUCTURE while it scrolls: the header is tinted, a heading is tinted by its rung —
// the outermost strongest, three rungs down faded into the page — a record faintly, and the grid closes every
// cell. What the tints ARE is the theme; the rule that uses them stays with the driver that paints.
//
// The platform's themes are fixed (ibCompositionThemes) — the first one is the default, what a report is
// painted in until a setting names another. A setting names one by its ID (ibOutputParameter::Theme);
// an id nobody knows paints in the first, the way a field that has gone still prints under its name.
// ---------------------------------------------------------------------------

#include "backend/backend_core.h"   // BACKEND_API

#include <wx/colour.h>
#include <wx/intl.h>   // wxGetTranslation — a theme is named in the language in force

#include <vector>

struct ibCompositionTheme {
	const wxChar* m_id;        // what a setting stores — never shown, never translated
	const char*   m_caption;   // what a window shows — marked for extraction, translated where it is read

	wxColour m_headerFill;     // the column titles, and the foot of a table
	wxColour m_groupFill[3];   // a heading by its rung, outermost first — deeper keeps the last
	wxColour m_detailFill;     // a record: faint, never blank paper — pure white is the loudest thing on a tinted page
	wxColour m_gridLine;       // the lines of the TABLE — a node's own theme tints its rows, never the grid
	wxColour m_titleText;      // the report's heading, printed large: the theme's hue at its deepest, so it is read first

	const wxColour& GroupFill(int level) const {
		return m_groupFill[level <= 0 ? 0 : level >= 2 ? 2 : level];
	}
	wxString Caption() const { return wxGetTranslation(wxString::FromUTF8(m_caption)); }
};

// THE PLATFORM'S THEMES, in the order a window offers them. The first is the default.
BACKEND_API const std::vector<const ibCompositionTheme*>& ibCompositionThemes();

// THE THEME AN ID NAMES — the first one when it names none.
BACKEND_API const ibCompositionTheme& ibCompositionThemeById(const wxString& id);

#endif
