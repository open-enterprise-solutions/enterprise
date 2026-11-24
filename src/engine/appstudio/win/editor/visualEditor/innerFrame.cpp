////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the form's card in the visual editor
////////////////////////////////////////////////////////////////////////////

#include "innerFrame.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/sizer.h>

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/topLevel.h"   // wxTOPLEVEL_xxx - what the caption is drawn as

wxDEFINE_EVENT(wxEVT_INNER_FRAME_RESIZED, wxCommandEvent);

ibInnerFrame::ibInnerFrame(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
#ifdef __WXGTK__
	: ibPanel(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
#else
	: ibPanel(parent, id, pos, size, wxBORDER_RAISED | wxFULL_REPAINT_ON_RESIZE),
#endif
	// the caption painted whole as it is sized: its shade runs across it, its buttons stand where the theme puts them
	m_titleBar(new ibPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxNO_BORDER | wxFULL_REPAINT_ON_RESIZE)),
	m_content(new ibPanel(this, wxID_ANY)),
	m_titleStyle(style)
{
	// a dialog's caption height - the theme's
	m_titleBar->SetMinSize(wxSize(100,
		ibThemeEngine::Get()->GetRenderer()->GetFrameTotalSize(wxSize(), wxTOPLEVEL_TITLEBAR).y));
	m_titleBar->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_titleBar->Bind(wxEVT_PAINT, &ibInnerFrame::OnPaintTitleBar, this);
	m_titleBar->Bind(wxEVT_LEFT_DOWN, &ibInnerFrame::OnTitleBarLeftDown, this);

	// The resize cursors are the card's own; its children keep the arrow, or they would show the card's.
	m_titleBar->SetCursor(*wxSTANDARD_CURSOR);
	m_content->SetCursor(*wxSTANDARD_CURSOR);

	// The card's own strip two pixels wide right and below catches the mouse for resizing; the content
	// keeps one pixel free left and above, where the selection box around it is drawn.
	wxBoxSizer* const column = new wxBoxSizer(wxVERTICAL);
	column->Add(m_titleBar, wxSizerFlags().Expand());
	column->Add(m_content, wxSizerFlags(1).Expand().Border(wxLEFT | wxTOP, 1));

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(column, wxSizerFlags(1).Expand().Border(wxRIGHT | wxBOTTOM, 2));
	SetSizer(sizer);

	Bind(wxEVT_MOTION, &ibInnerFrame::OnMouseMotion, this);
	Bind(wxEVT_LEFT_DOWN, &ibInnerFrame::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &ibInnerFrame::OnLeftUp, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &ibInnerFrame::OnMouseCaptureLost, this);

	if (size == wxDefaultSize)
		SetSize(GetBestSize());
}

void ibInnerFrame::SetTitle(const wxString& title)
{
	m_title = title;
	m_titleBar->Refresh();
}

void ibInnerFrame::SetTitleStyle(long style)
{
	m_titleStyle = style;
	m_titleBar->Refresh();
}

void ibInnerFrame::ShowTitleBar(bool show)
{
	m_titleBar->Show(show);
	Layout();
}

wxSize ibInnerFrame::DoGetBestSize() const
{
	// The caption over the content, each at its best size, and the strips the sizer keeps around the content: a pixel
	// left and above, two right and below. The caption counts while still hidden: the card is sized before it is shown.
	const wxSize caption = m_titleBar->GetBestSize();
	const wxSize content = m_content->GetBestSize();
	return wxSize(std::max(caption.x, content.x + 1) + 2, caption.y + content.y + 1 + 2) + GetWindowBorderSize();
}

ibInnerFrame::Edge ibInnerFrame::GetEdgeAt(const wxPoint& point) const
{
	const wxSize size = GetClientSize();
	const int grip = FromDIP(10);
	const bool right = point.x >= size.x - grip;
	const bool bottom = point.y >= size.y - grip;
	if (right && bottom)
		return Edge::Corner;
	if (right)
		return Edge::Right;
	return bottom ? Edge::Bottom : Edge::None;
}

wxSize ibInnerFrame::LimitSize(const wxSize& size) const
{
	// Never smaller than the caption needs, or a sliver when there is none, nor than the minimum set
	// from outside; never larger than the maximum, when one is set.
	wxSize limited = size;
	limited.IncTo(m_titleBar->IsShown() ? m_titleBar->GetMinSize() + FromDIP(wxSize(8, 10)) : FromDIP(wxSize(10, 10)));
	limited.IncTo(GetMinSize());
	limited.DecToIfSpecified(GetMaxSize());
	return limited;
}

void ibInnerFrame::EndSizing()
{
	m_sizing = Edge::None;

	// The canvas scrolls over the card and the margin it stands at.
	GetParent()->SetVirtualSize(GetSize() + wxSize(20, 20));

	wxCommandEvent resized(wxEVT_INNER_FRAME_RESIZED, GetId());
	resized.SetEventObject(this);
	wxPostEvent(GetEventHandler(), resized);
}

void ibInnerFrame::OnPaintTitleBar(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(m_titleBar);

	// The theme's title bar, as a window of the form's style is painted (ibTopLevelWindow::GetDecorationsStyle): the
	// caption, the title and the buttons the style asks for, where the theme puts them — the card looks as the window it
	// shows. A frame's caption with no border and nothing under it.
	int decorations = wxTOPLEVEL_TITLEBAR | wxTOPLEVEL_ACTIVE;
	if (m_titleStyle & wxCLOSE_BOX)
		decorations |= wxTOPLEVEL_BUTTON_CLOSE;
	if (m_titleStyle & wxMINIMIZE_BOX)
		decorations |= wxTOPLEVEL_BUTTON_ICONIZE;
	if (m_titleStyle & wxMAXIMIZE_BOX)
		decorations |= wxTOPLEVEL_BUTTON_MAXIMIZE;
	ibThemeEngine::Get()->GetRenderer()->DrawFrameTitleBar(dc, m_titleBar->GetClientRect(), m_title, wxNullIcon,
		decorations);
}

void ibInnerFrame::OnTitleBarLeftDown(wxMouseEvent& event)
{
	// A click on the caption is a click on the card: it selects the form, and the caption's right end
	// is a grip like the card's edge.
	wxMouseEvent click(event);
	click.SetPosition(event.GetPosition() + m_titleBar->GetPosition());
	click.SetEventObject(this);
	GetEventHandler()->ProcessEvent(click);
}

void ibInnerFrame::OnMouseMotion(wxMouseEvent& event)
{
	if (m_sizing == Edge::None) {
		switch (GetEdgeAt(event.GetPosition())) {
		case Edge::Corner: SetCursor(wxCursor(wxCURSOR_SIZENWSE)); break;
		case Edge::Right: SetCursor(wxCursor(wxCURSOR_SIZEWE)); break;
		case Edge::Bottom: SetCursor(wxCursor(wxCURSOR_SIZENS)); break;
		case Edge::None: SetCursor(*wxSTANDARD_CURSOR); break;
		}
		event.Skip();
		return;
	}

	// The card follows the pointer: the held edge moves, the other one stays.
	wxSize size = GetSize();
	if (m_sizing != Edge::Bottom)
		size.x = event.GetX() + m_grabOffset.x;
	if (m_sizing != Edge::Right)
		size.y = event.GetY() + m_grabOffset.y;
	SetSize(LimitSize(size));
}

void ibInnerFrame::OnLeftDown(wxMouseEvent& event)
{
	const Edge edge = GetEdgeAt(event.GetPosition());
	if (m_sizing != Edge::None || edge == Edge::None) {
		event.Skip();
		return;
	}

	m_sizing = edge;
	m_grabOffset = GetSize() - wxSize(event.GetX(), event.GetY());
	CaptureMouse();
}

void ibInnerFrame::OnLeftUp(wxMouseEvent& event)
{
	if (m_sizing == Edge::None) {
		event.Skip();
		return;
	}

	ReleaseMouse();
	EndSizing();
}

void ibInnerFrame::OnMouseCaptureLost(wxMouseCaptureLostEvent&)
{
	if (m_sizing != Edge::None)
		EndSizing();
}
