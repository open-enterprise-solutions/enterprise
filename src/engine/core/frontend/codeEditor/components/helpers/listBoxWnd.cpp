#include "listBoxWnd.h"

#define EXTENT_TEST wxT(" `~!@#$%^&*()-_=+\\|[]{};:\"\'<,>.?/1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")

COESListBox::COESListBox(wxWindow* parent, ÑListBoxVisualData* v, int ht)
	:wxSystemThemedControl<wxVListBox>(),
	m_visualData(v), m_maxStrWidth(0), m_currentRow(wxNOT_FOUND),
	m_aveCharWidth(8), m_textHeight(ht), m_itemHeight(ht),
	m_textTopGap(0), m_imageAreaWidth(0), m_imageAreaHeight(0)
{
	wxVListBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxBORDER_NONE, "AutoCompListBox");

	m_imagePadding = FromDIP(1);
	m_textBoxToTextGap = FromDIP(3);
	m_textExtraVerticalPadding = FromDIP(1);

	SetBackgroundColour(m_visualData->GetBgColour());

	Bind(wxEVT_SYS_COLOUR_CHANGED, &COESListBox::OnSysColourChanged, this);

	if (m_visualData->HasListCtrlAppearance())
	{
		EnableSystemTheme();
		Bind(wxEVT_MOTION, &COESListBox::OnMouseMotion, this);
		Bind(wxEVT_LEAVE_WINDOW, &COESListBox::OnMouseLeaveWindow, this);

#ifdef __WXMSW__
		// On MSW when using wxRendererNative to draw items in list control
		// style, the colours used seem to be based on the parent's
		// background colour. So set the popup's background.
		parent->SetOwnBackgroundColour(m_visualData->GetBgColour());
#endif
	}
}

bool COESListBox::AcceptsFocus() const
{
	return false;
}

// Do nothing in response to an attempt to set focus.
void COESListBox::SetFocus()
{
}

void COESListBox::SetContainerBorderSize(int s)
{
	m_borderSize = s;
}

void COESListBox::SetListBoxFont(wxFont &font)
{
	SetFont(font);
	int w;
	GetTextExtent(EXTENT_TEST, &w, &m_textHeight);
	RecalculateItemHeight();
}

void COESListBox::SetAverageCharWidth(int width)
{
	m_aveCharWidth = width;
}

wxRect COESListBox::GetDesiredRect() const
{
	int maxw = m_maxStrWidth * m_aveCharWidth;
	int maxh;

	// give it a default if there are no lines, and/or add a bit more
	if (maxw == 0) maxw = 100;
	maxw += TextBoxFromClientEdge() + m_textBoxToTextGap + m_aveCharWidth * 3;
	if (maxw > 350)
		maxw = 350;

	// estimate a desired height
	const int count = Length();
	const int desiredVisibleRows = m_visualData->GetDesiredVisibleRows();
	if (count)
	{
		if (count <= desiredVisibleRows)
			maxh = count * m_itemHeight;
		else
			maxh = desiredVisibleRows * m_itemHeight;
	}
	else
		maxh = 100;

	// Add space for a scrollbar if needed.
	if (count > desiredVisibleRows)
		maxw += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);

	// Add borders.
	maxw += 2 * m_borderSize;
	maxh += 2 * m_borderSize;

	return wxRect(0, 0, maxw, maxh);
}

int COESListBox::CaretFromEdge() const
{
	return m_borderSize + TextBoxFromClientEdge() + m_textBoxToTextGap;
}

void COESListBox::Clear()
{
	m_imageAreaWidth = 0;
	m_imageAreaHeight = 0;
	m_labels.clear();
	m_imageNos.clear();
}

void COESListBox::Append(wxString &s, int type)
{
	AppendHelper(s, type);
	AccountForBitmap(type, true);
}

int COESListBox::Length() const
{
	return GetItemCount();
}

void COESListBox::Select(int n)
{
	SetSelection(n);
}

wxString COESListBox::GetValue(int n) const
{
	return m_labels[n];
}

void COESListBox::AppendHelper(const wxString& text, int type)
{
	m_maxStrWidth = wxMax(m_maxStrWidth, text.length());
	m_labels.push_back(text);
	m_imageNos.push_back(type);
	SetItemCount(m_labels.size());
}

void COESListBox::AccountForBitmap(int type, bool recalculateItemHeight)
{
	const int oldHeight = m_imageAreaHeight;
	const wxBitmap* bmp = m_visualData->GetImage(type);

	if (bmp)
	{
		if (bmp->GetWidth() > m_imageAreaWidth)
			m_imageAreaWidth = bmp->GetWidth();

		if (bmp->GetHeight() > m_imageAreaHeight)
			m_imageAreaHeight = bmp->GetHeight();
	}

	if (recalculateItemHeight && m_imageAreaHeight != oldHeight)
		RecalculateItemHeight();
}

void COESListBox::RecalculateItemHeight()
{
	m_itemHeight = wxMax(m_textHeight + 2 * m_textExtraVerticalPadding,
		m_imageAreaHeight + 2 * m_imagePadding);
	m_textTopGap = (m_itemHeight - m_textHeight) / 2;
}

int COESListBox::TextBoxFromClientEdge() const
{
	return (m_imageAreaWidth == 0 ? 0 : m_imageAreaWidth + 2 * m_imagePadding);
}

void COESListBox::OnSysColourChanged(wxSysColourChangedEvent& WXUNUSED(event))
{
	m_visualData->ComputeColours();
	GetParent()->SetOwnBackgroundColour(m_visualData->GetBgColour());
	SetBackgroundColour(m_visualData->GetBgColour());
	GetParent()->Refresh();
}

void COESListBox::OnMouseLeaveWindow(wxMouseEvent& event)
{
	const int old = m_currentRow;
	m_currentRow = wxNOT_FOUND;

	if (old != wxNOT_FOUND)
		RefreshRow(old);

	event.Skip();
}

void COESListBox::OnMouseMotion(wxMouseEvent& event)
{
	const int old = m_currentRow;
	m_currentRow = VirtualHitTest(event.GetY());

	if (old != m_currentRow)
	{
		if (m_currentRow != wxNOT_FOUND)
			RefreshRow(m_currentRow);

		if (old != wxNOT_FOUND)
			RefreshRow(old);
	}

	event.Skip();
}

wxCoord COESListBox::OnMeasureItem(size_t WXUNUSED(n)) const
{
	return static_cast<wxCoord>(m_itemHeight);
}

// This control will be drawn so that a typical row of pixels looks like:
//
//    +++++++++++++++++++++++++   =====ITEM TEXT================
//  |         |                 |    |
//  |       m_imageAreaWidth    |    |
//  |                           |    |
// m_imagePadding               |   m_textBoxToTextGap
//                              |
//                   m_imagePadding
//
//
// m_imagePadding            : Used to give a little extra space between the
//                             client edge and an item's bitmap.
// m_imageAreaWidth          : Computed as the width of the largest registered
//                             bitmap.
// m_textBoxToTextGap        : Used so that item text does not begin immediately
//                             at the edge of the highlight box.
//
// Images are drawn centered in the image area.
// If a selection rectangle is drawn, its left edge is at x=0 if there are
// no bitmaps. Otherwise
//       x = m_imagePadding + m_imageAreaWidth + m_imagePadding.
// Text is drawn at x + m_textBoxToTextGap and centered vertically.

void COESListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const
{
	wxString label;
	int imageNo = -1;
	if (n < m_labels.size())
	{
		label = m_labels[n];
		imageNo = m_imageNos[n];
	}

	int topGap = m_textTopGap;
	int leftGap = TextBoxFromClientEdge() + m_textBoxToTextGap;

	wxDCTextColourChanger tcc(dc);

	if (IsSelected(n))
		tcc.Set(m_visualData->GetHighlightTextColour());
	else if (static_cast<int>(n) == m_currentRow)
		tcc.Set(m_visualData->GetCurrentTextColour());
	else
		tcc.Set(m_visualData->GetTextColour());

	label = wxControl::Ellipsize(label, dc, wxELLIPSIZE_END,
		rect.GetWidth() - leftGap);

	dc.DrawText(label, rect.GetLeft() + leftGap, rect.GetTop() + topGap);

	const wxBitmap* b = m_visualData->GetImage(imageNo);
	if (b)
	{
		topGap = (m_itemHeight - b->GetHeight()) / 2;
		leftGap = m_imagePadding + (m_imageAreaWidth - b->GetWidth()) / 2;
		dc.DrawBitmap(*b, rect.GetLeft() + leftGap, rect.GetTop() + topGap, true);
	}
}

void COESListBox::OnDrawBackground(wxDC &dc, const wxRect &rect, size_t n) const
{
	if (IsSelected(n))
	{
		wxRect selectionRect(rect);
		const wxColour& highlightBgColour = m_visualData->GetHighlightBgColour();

#ifdef __WXMSW__
		if (!m_visualData->HasListCtrlAppearance())
		{
			// On windows the selection rectangle in Scintilla's
			// autocompletion list only covers the text and not the icon.

			const int textBoxFromClientEdge = TextBoxFromClientEdge();
			selectionRect.SetLeft(rect.GetLeft() + textBoxFromClientEdge);
			selectionRect.SetWidth(rect.GetWidth() - textBoxFromClientEdge);
		}
#endif // __WXMSW__

		if (highlightBgColour.IsOk())
		{
			wxDCBrushChanger bc(dc, highlightBgColour);
			wxDCPenChanger   pc(dc, highlightBgColour);
			dc.DrawRectangle(selectionRect);
		}
		else
		{
			wxRendererNative::GetDefault().DrawItemSelectionRect(
				const_cast<COESListBox*>(this), dc, selectionRect,
				wxCONTROL_SELECTED | wxCONTROL_FOCUSED);
		}

		if (!m_visualData->HasListCtrlAppearance())
			wxRendererNative::GetDefault().DrawFocusRect(
				const_cast<COESListBox*>(this), dc, selectionRect);
	}
	else if (static_cast<int>(n) == m_currentRow)
	{
		const wxColour& currentBgColour = m_visualData->GetCurrentBgColour();

		if (currentBgColour.IsOk())
		{
			wxDCBrushChanger bc(dc, currentBgColour);
			wxDCPenChanger   pc(dc, currentBgColour);
			dc.DrawRectangle(rect);
		}
		else
		{
			wxRendererNative::GetDefault().DrawItemSelectionRect(
				const_cast<COESListBox*>(this), dc, rect,
				wxCONTROL_CURRENT | wxCONTROL_FOCUSED);
		}
	}
}