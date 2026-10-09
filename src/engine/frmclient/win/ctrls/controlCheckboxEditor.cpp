#include "controlCheckboxEditor.h"

#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/renderer.h>
#include <wx/settings.h>

wxBEGIN_EVENT_TABLE(ibControlCheckbox, wxWindow)
	EVT_SIZE(ibControlCheckbox::OnSize)
	EVT_DPI_CHANGED(ibControlCheckbox::OnDPIChanged)
	EVT_PAINT(ibControlCheckbox::OnPaint)
	EVT_ERASE_BACKGROUND(ibControlCheckbox::OnEraseBackground)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ibControlCheckbox::ibInnerCheckBox, wxCheckBox)
	EVT_KEY_DOWN(ibControlCheckbox::ibInnerCheckBox::OnKeyDown)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibControlCheckbox, wxWindow);

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxSize ibControlCheckbox::ComputeLabelBestSize() const
{
	if (m_labelText.empty())
		return wxSize(0, 0);

	if (m_cachedLabelSize.x < 0) {
		// Multi-line aware measurement: sum line heights, take max line
		// width. Matches ibControlTextEditor's label handling.
		const wxArrayString lines = wxSplit(m_labelText, wxT('\n'));
		int totalH = 0, maxW = 0;
		for (const wxString& line : lines) {
			wxCoord w = 0, h = 0;
			GetTextExtent(line, &w, &h);
			if (w > maxW) maxW = w;
			totalH += h;
		}
		m_cachedLabelSize = wxSize(maxW, totalH);
	}
	return m_cachedLabelSize;
}

wxSize ibControlCheckbox::DoGetBestClientSize() const
{
	wxSize size = m_checkBox != nullptr ? m_checkBox->GetBestSize() : wxSize(0, 0);
	wxSize labelSize = ComputeLabelBestSize();

	if (m_labelMinSize.x > 0)
		labelSize.x = m_labelMinSize.x;

	if (size.y < labelSize.y)
		size.y = labelSize.y;

	// Include the visual gap between the label and the native checkbox so
	// sizers reserve room for it.
	const int gap = labelSize.x > 0 ? FromDIP(4) : 0;
	size.x += labelSize.x + gap;
	return size;
}

wxWindowList ibControlCheckbox::GetCompositeWindowParts() const
{
	wxWindowList parts;
	if (!m_destroying && m_checkBox != nullptr)
		parts.push_back(m_checkBox);
	return parts;
}

void ibControlCheckbox::LayoutControls()
{
	if (m_checkBox == nullptr)
		return;

	const wxSize sizeTotal = GetClientSize();
	const int width = sizeTotal.x;
	const int height = sizeTotal.y;

	wxSize labelSize = ComputeLabelBestSize();
	if (m_labelMinSize.x > 0) labelSize.x = m_labelMinSize.x;
	const wxSize checkSize = m_checkBox->GetBestSize();

	const int gap = labelSize.x > 0 ? FromDIP(4) : 0;

	// Inset the native checkbox by 1 px on top/bottom/outside edge so the
	// dotted focus rectangle drawn by OnPaint is not overwritten by the
	// checkbox's own background paint.
	const int inset = FromDIP(1);

	if (m_align == wxAlignment::wxALIGN_LEFT) {
		m_labelRect = wxRect(0, 0, labelSize.x, height);
		const int x = labelSize.x + gap;
		m_checkBox->SetSize(x, inset,
			width - x - inset,
			height - 2 * inset);
	}
	else {
		m_checkBox->SetSize(inset, inset, checkSize.x, height - 2 * inset);
		m_labelRect = wxRect(checkSize.x + gap, 0, labelSize.x, height);
	}
}

// ----------------------------------------------------------------------------
// paint
// ----------------------------------------------------------------------------

void ibControlCheckbox::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(GetBackgroundColour());
	dc.Clear();

	if (!m_labelText.empty() && !m_labelRect.IsEmpty()) {

		dc.SetFont(GetFont());
		dc.SetTextForeground(m_enabledIntent
			? GetForegroundColour()
			: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

		// Per-line ellipsize for long labels; multi-line support via '\n'.
		wxString label;
		if (m_labelRect.width > 0 && m_labelText.Find(wxT('\n')) == wxNOT_FOUND) {
			label = wxControl::Ellipsize(m_labelText, dc, wxELLIPSIZE_END, m_labelRect.width);
		}
		else if (m_labelRect.width > 0) {
			const wxArrayString lines = wxSplit(m_labelText, wxT('\n'));
			for (size_t i = 0; i < lines.size(); ++i) {
				if (i > 0) label += wxT('\n');
				label += wxControl::Ellipsize(lines[i], dc, wxELLIPSIZE_END, m_labelRect.width);
			}
		}
		else {
			label = m_labelText;
		}

		wxCoord tw = 0, th = 0;
		dc.GetMultiLineTextExtent(label, &tw, &th);
		const int centerY = m_labelRect.y + m_labelRect.height / 2;
		dc.DrawText(label, m_labelRect.x, centerY - th / 2);
	}

	if (m_hasFocus) {
		// Dotted focus rectangle spanning the whole compound control (label
		// + native checkbox) so keyboard users can see which row currently
		// holds the focus without having to look for the native tick-ring.
		const wxRect client(wxPoint(0, 0), GetClientSize());
		wxRendererNative::Get().DrawFocusRect(this, dc, client);
	}
}

// ----------------------------------------------------------------------------
// label (ibControlDynamicBorder)
// ----------------------------------------------------------------------------

void ibControlCheckbox::CalculateLabelSize(wxCoord* w, wxCoord* h) const
{
	m_labelMinSize = wxDefaultSize;
	const wxSize s = ComputeLabelBestSize();
	if (w) *w = s.x;
	if (h) *h = s.y;
}

void ibControlCheckbox::ApplyLabelSize(const wxSize& s)
{
	// Same short-circuit as ibControlTextEditor — skip the layout churn
	// when nothing changed.
	if (m_labelMinSize == s)
		return;

	m_labelMinSize = s;
	InvalidateBestSize();
	LayoutControls();
	Refresh();
}
