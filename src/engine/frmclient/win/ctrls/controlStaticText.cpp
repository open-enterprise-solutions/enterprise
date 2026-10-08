#include "controlStaticText.h"

#include <wx/dcbuffer.h>
#include <wx/settings.h>

wxBEGIN_EVENT_TABLE(ibControlStaticText, wxControl)
	EVT_PAINT(ibControlStaticText::OnPaint)
	EVT_ERASE_BACKGROUND(ibControlStaticText::OnEraseBackground)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibControlStaticText, wxControl);

bool ibControlStaticText::Create(wxWindow* parent,
	wxWindowID id,
	const wxString& label,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	if (!wxControl::Create(parent, id, pos, size, style | wxBORDER_NONE,
		wxDefaultValidator, name))
		return false;

	SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxControl::SetLabel(label);
	InvalidateBestSize();
	return true;
}

void ibControlStaticText::SetLabel(const wxString& label)
{
	if (GetLabel() == label) return;
	wxControl::SetLabel(label);
	InvalidateCache();
	InvalidateBestSize();
	Refresh();
}

wxSize ibControlStaticText::ComputeLabelExtent() const
{
	if (m_cachedLabelSize.x < 0) {
		const wxString& label = GetLabel();
		if (label.empty()) {
			m_cachedLabelSize = wxSize(0, 0);
		}
		else {
			const wxArrayString lines = wxSplit(label, wxT('\n'));
			int totalH = 0, maxW = 0;
			for (const wxString& line : lines) {
				wxCoord w = 0, h = 0;
				GetTextExtent(line, &w, &h);
				if (w > maxW) maxW = w;
				totalH += h;
			}
			m_cachedLabelSize = wxSize(maxW, totalH);
		}
	}
	return m_cachedLabelSize;
}

wxSize ibControlStaticText::DoGetBestClientSize() const
{
	wxSize size = ComputeLabelExtent();

	// Approximate the total row height of ibControlTextEditor: the inner
	// wxTextCtrl natural height (~charHeight + themed padding on MSW) plus
	// the +5 DIP breathing room we add there. A live wxTextCtrl probe
	// would be more accurate but creating/destroying an HWND during
	// layout caused construction-time crashes, so stick to the formula.
	const int rowH = GetCharHeight() + FromDIP(4);
	if (size.y < rowH) size.y = rowH;

	return size;
}

void ibControlStaticText::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(GetBackgroundColour());
	dc.Clear();

	const wxString& label = GetLabel();
	if (label.empty())
		return;

	dc.SetFont(GetFont());
	dc.SetTextForeground(IsThisEnabled()
		? GetForegroundColour()
		: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

	const wxSize client = GetClientSize();

	wxCoord tw = 0, th = 0;
	dc.GetMultiLineTextExtent(label, &tw, &th);

	// Align vertically with the *caret* of a neighbouring ibControlTextEditor
	// rather than its geometric centre. The editor shifts its native EDIT
	// slightly below centre to compensate for the control's intrinsic top
	// padding; match that offset here so both baselines line up on one row.
	const int centerY = client.y / 2 + FromDIP(2);
	dc.DrawText(label, 0, centerY - th / 2);
}
