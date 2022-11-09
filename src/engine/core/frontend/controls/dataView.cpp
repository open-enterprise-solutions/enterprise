#include "dataView.h"
#include "common/tableInfo.h"

wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_START_DELETING, wxDataViewEvent);

wxBEGIN_EVENT_TABLE(CDataViewCtrl::CDataViewFreezeRowsWindow, wxWindow)
EVT_PAINT(CDataViewCtrl::CDataViewFreezeRowsWindow::OnPaint)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(CDataViewCtrl, wxDataViewCtrl);

wxBEGIN_EVENT_TABLE(CDataViewCtrl, wxDataViewCtrl)
EVT_SIZE(CDataViewCtrl::OnSize)
wxEND_EVENT_TABLE()

bool CDataViewCtrl::AssociateModel(IValueModel* model)
{
	if (model != NULL) {
		m_genNotitfier = new wxTableModelNotifier(this);
		model->AppendNotifier(m_genNotitfier);
	}
	else {
		// Our previous notifier has either been already deleted when the
		// previous model was DecRef()'d in the base class AssociateModel() or
		// is not associated with us any more because if the model is still
		// alive, it's not used by this control.
		m_genNotitfier = NULL;
	}

	return wxDataViewCtrl::AssociateModel(model);
}

wxSize CDataViewCtrl::GetSizeAvailableForScrollTarget(const wxSize& size) {

	wxSize newsize = size;

	if (!HasFlag(wxDV_NO_HEADER) && (GenericGetHeader()))
		newsize.y -= GenericGetHeader()->GetSize().y;
	if (m_freezeRows)
		newsize.y -= m_freezeRows->GetSize().y;
	return newsize;
}

void CDataViewCtrl::OnSize(wxSizeEvent& event) {

	if (m_freezeRows &&
		m_freezeRows->GetSize().y <= m_freezeRows->GetBestSize().y) {
		m_freezeRows->Refresh();
	}

	event.Skip();
}

#include <wx/dcbuffer.h>
#ifdef __WXMSW__
#include "wx/msw/private.h"
#endif

int CDataViewCtrl::CDataViewFreezeRowsWindow::GetDefaultRowHeight() const
{
	const int SMALL_ICON_HEIGHT = FromDIP(16);

#ifdef __WXMSW__
	// We would like to use the same line height that Explorer uses. This is
	// different from standard ListView control since Vista.
	if (wxGetWinVersion() >= wxWinVersion_Vista)
		return wxMax(SMALL_ICON_HEIGHT, GetCharHeight()) + FromDIP(6);
	else
#endif // __WXMSW__
		return wxMax(SMALL_ICON_HEIGHT, GetCharHeight()) + FromDIP(1);
}

void CDataViewCtrl::CDataViewFreezeRowsWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);
	const wxSize size = GetClientSize();
	dc.SetBrush(GetParent()->GetBackgroundColour());
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(size);

	//if (m_owner->IsEmpty()) {
	//	// No items to draw.
	//	return;
	//}

	// prepare the DC
	GetOwner()->PrepareDC(dc);
	dc.SetFont(GetFont());

	wxRect update = GetUpdateRegion().GetBox();
	GetOwner()->CalcUnscrolledPosition(update.x, update.y, &update.x, &update.y);

	// compute which items needs to be redrawn
	unsigned int item_start = GetLineAt(wxMax(0, update.y));
	unsigned int item_count =
		wxMin((int)(GetLineAt(wxMax(0, update.y + update.height)) - item_start + 1),
			(int)(GetRowCount() - item_start));
	
	unsigned int item_last = item_start + item_count;

	// compute which columns needs to be redrawn
	unsigned int cols = GetOwner()->GetColumnCount();
	if (!cols) {
		// we assume that we have at least one column below and painting an
		// empty control is unnecessary anyhow
		return;
	}
	unsigned int col_start = 0;
	unsigned int x_start;	
	for (x_start = 0; col_start < cols; col_start++) {
		wxDataViewColumn* col = GetOwner()->GetColumnAt(col_start);
		if (col->IsHidden())
			continue;      // skip it!
		unsigned int w = col->GetWidth();
		if (x_start + w >= (unsigned int)update.x)
			break;
		x_start += w;
	}

	unsigned int col_last = col_start;
	unsigned int x_last = x_start;
	for (; col_last < cols; col_last++) {
		wxDataViewColumn* col = GetOwner()->GetColumnAt(col_last);
		if (col->IsHidden())
			continue;      // skip it!
		if (x_last > (unsigned int)update.GetRight())
			break;
		x_last += col->GetWidth();
	}

	// Instead of calling GetLineStart() for each line from the first to the
	// last one, we will compute the starts of the lines as we iterate over
	// them starting from this one, as this is much more efficient when using
	// wxDV_VARIABLE_LINE_HEIGHT (and doesn't really change anything when not
	// using it, so there is no need to use two different approaches).
	const unsigned int first_line_start = GetLineStart(item_start);

	// Draw background of alternate rows specially if required
	if (GetOwner()->HasFlag(wxDV_ROW_LINES)) {
		wxColour altRowColour = GetOwner()->GetAlternateRowColour();
		if (!altRowColour.IsOk()) {
			// Determine the alternate rows colour automatically from the
			// background colour.
			const wxColour bgColour = GetOwner()->GetBackgroundColour();

			// Depending on the background, alternate row color
			// will be 3% more dark or 50% brighter.
			int alpha = bgColour.GetRGB() > 0x808080 ? 97 : 150;
			altRowColour = bgColour.ChangeLightness(alpha);
		}

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(altRowColour));

		// We only need to draw the visible part, so limit the rectangle to it.
		const int xRect = GetOwner()->CalcUnscrolledPosition(wxPoint(0, 0)).x;
		const int widthRect = size.x;
		unsigned int cur_line_start = first_line_start;
		for (unsigned int item = item_start; item < item_last; item++) {
			const int h = (item);
			if (item % 2) {
				dc.DrawRectangle(xRect, cur_line_start, widthRect, h);
			}
			cur_line_start += h;
		}
	}

	// Draw horizontal rules if required
	if (GetOwner()->HasFlag(wxDV_HORIZ_RULES))
	{
		dc.SetPen(m_penRule);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		unsigned int cur_line_start = first_line_start;
		for (unsigned int i = item_start; i <= item_last; i++) {
			const int h = GetLineHeight(i);
			dc.DrawLine(x_start, cur_line_start, x_last, cur_line_start);
			cur_line_start += h;
		}
	}

	// Draw vertical rules if required
	//if (GetOwner()->HasFlag(wxDV_VERT_RULES)) {
	//	
	//	dc.SetPen(m_penRule);
	//	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	//	// NB: Vertical rules are drawn in the last pixel of a column so that
	//	//     they align perfectly with native MSW wxHeaderCtrl as well as for
	//	//     consistency with MSW native list control. There's no vertical
	//	//     rule at the most-left side of the control.

	//	int x = x_start - 1;
	//	int line_last = GetLineStart(item_last);
	//	for (unsigned int i = col_start; i < col_last; i++)
	//	{
	//		wxDataViewColumn* col = GetOwner()->GetColumnAt(i);
	//		if (col->IsHidden())
	//			continue;       // skip it

	//		x += col->GetWidth();

	//		dc.DrawLine(x, first_line_start,
	//			x, line_last);
	//	}
	//}
}
