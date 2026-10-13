#include "core/composition/compositionTheme.h"

// ⭐ EACH THEME IS ONE AGGREGATE, and a new one is one more (Max, 2026-09-29: `static const … theme1 = {...}`).
// Built inside the function rather than at namespace scope: a colour is not a constant expression, and a
// palette asked for while another file is still being initialised must already be there.
//
// Every theme keeps one rule — a heading's tint fades with its rung, a record is fainter than any heading, the
// grid is darker than every fill — so changing the theme changes the colour and never how the structure reads.
const std::vector<const ibCompositionTheme*>& ibCompositionThemes()
{
	// THE ONE REPORTS WERE PAINTED IN until 2026-10-05 — a neutral green that blends rather than announces itself.
	static const ibCompositionTheme s_meadow = {
		wxT("Meadow"), wxTRANSLATE("Meadow"),
		ibColour(0xD4, 0xE4, 0xD4),
		{ ibColour(0xE2, 0xEE, 0xE2), ibColour(0xE9, 0xF2, 0xE9), ibColour(0xF0, 0xF7, 0xF0) },
		ibColour(0xFA, 0xFA, 0xF8),
		ibColour(0xA8, 0xB8, 0xA8),
		ibColour(0x2E, 0x5E, 0x3A),
	};
	static const ibCompositionTheme s_neutral = {
		wxT("Neutral"), wxTRANSLATE("Neutral"),
		ibColour(0xDA, 0xDC, 0xDE),
		{ ibColour(0xE4, 0xE6, 0xE8), ibColour(0xEC, 0xED, 0xEF), ibColour(0xF3, 0xF4, 0xF5) },
		ibColour(0xFB, 0xFB, 0xFB),
		ibColour(0xB0, 0xB4, 0xB8),
		ibColour(0x40, 0x44, 0x48),
	};
	// THE DEFAULT — the blue-grey of the application's own window, so a report reads as part of it.
	static const ibCompositionTheme s_sea = {
		wxT("Sea"), wxTRANSLATE("Sea"),
		ibColour(0xD2, 0xDF, 0xEC),
		{ ibColour(0xDF, 0xE8, 0xF2), ibColour(0xE7, 0xEE, 0xF6), ibColour(0xF0, 0xF4, 0xFA) },
		ibColour(0xFA, 0xFB, 0xFD),
		ibColour(0xA6, 0xB6, 0xC8),
		ibColour(0x24, 0x4A, 0x78),
	};
	static const ibCompositionTheme s_sand = {
		wxT("Sand"), wxTRANSLATE("Sand"),
		ibColour(0xE8, 0xDF, 0xCB),
		{ ibColour(0xEF, 0xE8, 0xD8), ibColour(0xF3, 0xEE, 0xE3), ibColour(0xF8, 0xF5, 0xEE) },
		ibColour(0xFC, 0xFB, 0xF8),
		ibColour(0xC2, 0xB6, 0x9E),
		ibColour(0x74, 0x55, 0x28),
	};
	// FOR PAPER — no tint at all, the structure carried by the weight of the type and by the grid alone. Here the
	// white is not the loudest thing on the page: it is the whole page.
	static const ibCompositionTheme s_blackAndWhite = {
		wxT("BlackAndWhite"), wxTRANSLATE("Black and white"),
		ibColour(0xFF, 0xFF, 0xFF),
		{ ibColour(0xFF, 0xFF, 0xFF), ibColour(0xFF, 0xFF, 0xFF), ibColour(0xFF, 0xFF, 0xFF) },
		ibColour(0xFF, 0xFF, 0xFF),
		ibColour(0x00, 0x00, 0x00),
		ibColour(0x00, 0x00, 0x00),
	};

	static const std::vector<const ibCompositionTheme*> s_themes = {
		&s_sea, &s_neutral, &s_meadow, &s_sand, &s_blackAndWhite,
	};
	return s_themes;
}

const ibCompositionTheme& ibCompositionThemeById(const ibString& id)
{
	const std::vector<const ibCompositionTheme*>& themes = ibCompositionThemes();
	for (const ibCompositionTheme* theme : themes)
		if (id.IsSameAs(theme->m_id, false))
			return *theme;
	return *themes.front();
}
