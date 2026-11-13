////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the workspace while no tab is open
////////////////////////////////////////////////////////////////////////////

#include "welcomeWindow.h"

#include <wx/datetime.h>

#include "deskgui/univ/theme.h"          // ibThemeEngine — the sheet's colours are the theme's
#include "deskgui/univ/colourScheme.h"   // wxTHEME_COLOUR

ibWelcomeWindow::ibWelcomeWindow(wxWindow* parent, const wxString& fullName)
	: ibPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxFULL_REPAINT_ON_RESIZE),
	m_fullName(fullName)
{
	Bind(wxEVT_PAINT, &ibWelcomeWindow::OnPaint, this);
}

void ibWelcomeWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	ibThemePaintDC dc(this);

	// The greeting by the hour it is now: the morning from 5, the afternoon from 12, the evening from 18 and through the
	// night.
	const int hour = wxDateTime::Now().GetHour();
	wxString greeting;
	if (hour >= 5 && hour < 12)
		greeting = m_fullName.empty() ? _("Good morning") : wxString::Format(_("Good morning, %s"), m_fullName);
	else if (hour >= 12 && hour < 18)
		greeting = m_fullName.empty() ? _("Good afternoon") : wxString::Format(_("Good afternoon, %s"), m_fullName);
	else
		greeting = m_fullName.empty() ? _("Good evening") : wxString::Format(_("Good evening, %s"), m_fullName);
	const wxString question = _("Where shall we start?");
	const std::vector<std::pair<wxString, wxString>> keys = GetKeys();

	// The theme's alternate text, a table's and the start page's; the question and what the keys do a shade lighter.
	// A key stands in a cap: the content's white in the border's line.
	const wxColour text = wxTHEME_COLOUR(CONTROL_TEXT_ALTERNATE);
	const wxColour quiet = text.ChangeLightness(115);

	const wxFont font = GetFont();
	wxFont fontGreeting = font;
	fontGreeting.SetPointSize(font.GetPointSize() + 7);
	wxFont fontQuestion = font;
	fontQuestion.SetPointSize(font.GetPointSize() + 1);

	dc.SetFont(fontGreeting);
	const wxSize sizeGreeting = dc.GetTextExtent(greeting);
	dc.SetFont(fontQuestion);
	const wxSize sizeQuestion = dc.GetTextExtent(question);

	const wxCoord gap = FromDIP(8), gapKeys = FromDIP(40), row = FromDIP(28), cap = FromDIP(19), pad = FromDIP(4);
	const wxCoord beside = FromDIP(8), radius = FromDIP(3);
	const wxCoord height = sizeGreeting.y + gap + sizeQuestion.y
		+ (keys.empty() ? 0 : gapKeys + static_cast<wxCoord>(keys.size()) * row);
	const wxSize client = GetClientSize();
	const wxCoord middle = client.x / 2;
	wxCoord y = wxMax(FromDIP(16), (client.y - height) / 3);

	dc.SetFont(fontGreeting);
	dc.SetTextForeground(text);
	dc.DrawText(greeting, middle - sizeGreeting.x / 2, y);
	y += sizeGreeting.y + gap;

	dc.SetFont(fontQuestion);
	dc.SetTextForeground(quiet);
	dc.DrawText(question, middle - sizeQuestion.x / 2, y);
	y += sizeQuestion.y;

	if (keys.empty())
		return;
	y += gapKeys;

	// What a command does to the left of the middle, its key in its cap to the right.
	dc.SetFont(font);
	dc.SetPen(wxPen(wxTHEME_COLOUR(SHADOW_IN)));
	dc.SetBrush(wxBrush(wxTHEME_COLOUR(WINDOW)));
	for (const auto& [label, key] : keys) {
		const wxSize sizeLabel = dc.GetTextExtent(label);
		const wxSize sizeKey = dc.GetTextExtent(key);
		dc.SetTextForeground(quiet);
		dc.DrawText(label, middle - beside - sizeLabel.x, y + (cap - sizeLabel.y) / 2);
		dc.DrawRoundedRectangle(wxRect(middle + beside, y, sizeKey.x + 2 * pad, cap), radius);
		dc.SetTextForeground(text);
		dc.DrawText(key, middle + beside + pad, y + (cap - sizeKey.y) / 2);
		y += row;
	}
}
