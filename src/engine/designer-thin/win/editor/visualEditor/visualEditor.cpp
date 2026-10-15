////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"   // ibFormDragItem / ibSourceDragItem / ibCommandDragItem — the polymorphic drop kinds

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/visualView/commandBar.h"   // ibViewCommandBar — a drop on a command bar joins it, a tool pressed selects its command
#include "frmclient/visualView/ctrl/sizer.h"   // ibValueSizerItem — the highlight is drawn out to its item's border

#include <wx/dataobj.h>   // wxDataObjectComposite — accept every registered drag kind on one target
#include <wx/utils.h>     // wxFindWindowAtPoint (point -> widget hit-test)

// Form-editor drop target: receives a node dragged from a form-editor tree and enacts it on the form. It registers
// the draggable KINDS (ibFormDragItem — a source path → a bound control, a command → a command button); OnData
// matches the received wire format to a kind, decodes it and hands it to the surface's applier. Shared by the form
// CANVAS and the OBJECT TREE — each supplies only its point→frame resolver and its editor-bound applier. Adding a
// draggable is a new ibFormDragItem subclass registered below; OnData never changes.
ibSourceDragDropTarget::ibSourceDragDropTarget(PointResolver resolver, DropApply apply, bool deferred)
	: m_resolver(std::move(resolver)), m_apply(std::move(apply)), m_deferred(deferred)
{
	// The registered kinds — the ONE place they are listed. A composite carries one sub-object per kind so OnData
	// can read the received kind's bytes back (the composite owns them; m_data keeps non-owning, index-parallel refs).
	m_items.push_back(std::make_unique<ibSourceDragItem>());    // oes_source_drag — the preferred (common) format
	m_items.push_back(std::make_unique<ibCommandDragItem>());   // oes_command_drag

	wxDataObjectComposite* composite = new wxDataObjectComposite();
	bool preferred = true;
	for (const std::unique_ptr<ibFormDragItem>& item : m_items) {
		wxCustomDataObject* data = new wxCustomDataObject(item->GetFormat());
		composite->Add(data, preferred);
		m_data.push_back(data);
		preferred = false;
	}
	SetDataObject(composite);
}

ibSourceDragDropTarget::~ibSourceDragDropTarget() = default;

wxDragResult ibSourceDragDropTarget::OnData(wxCoord x, wxCoord y, wxDragResult def)
{
	if (!GetData())
		return wxDragNone;
	wxDataObjectComposite* composite = static_cast<wxDataObjectComposite*>(GetDataObject());
	if (composite == nullptr)
		return wxDragNone;

	const wxDataFormat fmt = composite->GetReceivedFormat();
	for (size_t i = 0; i < m_items.size(); ++i) {
		ibFormDragItem* item = m_items[i].get();
		if (item->GetFormat() != fmt)
			continue;
		wxCustomDataObject* data = m_data[i];
		if (data != nullptr && data->GetSize() > 0) {
			ibReaderMemory reader(data->GetData(), (int)data->GetSize());
			if (item->LoadPayload(reader) && m_apply) {
				ibValueFrame* target = m_resolver ? m_resolver(x, y) : nullptr;
				if (m_deferred) {
					// DEFER out of the OS drop callback: a target on a tablebox goes with it when the apply draws the
					// form anew → use-after-free. Clone the DECODED item so it outlives THIS target, and run the apply
					// after the drag unwinds.
					DropApply apply = m_apply;
					std::shared_ptr<ibFormDragItem> clone(item->Clone());
					wxTheApp->CallAfter([apply, clone, target]() { apply(*clone, target); });
				}
				else {
					m_apply(*item, target);
				}
			}
		}
		return def;
	}
	return def;
}

///////////////////////////////////////////////////////////////////////////////

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ibVisualEditorHost(ibVisualEditor* handler, wxWindow* parent, long long tabId) :
	ibVisualHostClient(*ibFrontendMainFrame::GetFrame(), parent, tabId),
	m_formHandler(handler)
{
	// The canvas the card sits on.
	SetOwnBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));  // #D8E2EB palest powder — light background so form card pops

	m_back = new ibDesignerWindow(this, wxID_ANY, wxPoint(10, 10));
	m_back->GetEventHandler()->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnClickBackPanel), nullptr, this);
	// The card resized by its border — the form selected (the desktop's EVT_INNER_FRAME_RESIZED entry).
	m_back->GetEventHandler()->Connect(wxID_ANY, wxEVT_INNER_FRAME_RESIZED, wxCommandEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnResizeBackPanel), nullptr, this);

	// The form canvas accepts any registered drag KIND: a source path → a bound control, a command → a bound
	// command button. It supplies only the RESOLVER (map the OS drop point to the ibValueFrame under it, by
	// walking wx parents to the owning object — the same resolution a click uses) and the APPLIER (run the decoded
	// item's ApplyDrop with THIS editor). A null resolve is fine: a source falls back to the form root, a command
	// climbs to the nearest command bar (the server's).
	m_back->GetFrameContentPanel()->SetDropTarget(
		new ibSourceDragDropTarget(
			[this](wxCoord x, wxCoord y) -> ibValueFrame* {
				const wxPoint screenPt = m_back->GetFrameContentPanel()->ClientToScreen(wxPoint(x, y));
				wxWindow* const hit = wxFindWindowAtPoint(screenPt);
				ibValueFrame* target = nullptr;
				for (wxWindow* w = hit; w != nullptr && target == nullptr; w = w->GetParent())
					target = GetObjectBase(w);
				// A command dropped on a command bar (the form's or a tablebox's) joins THAT bar — a new item, not a free
				// button. Its control handed to CreateCommandButton; cleared when the drop is elsewhere.
				const ibValueFrame* const barOwner = GetCommandBarOwner(hit);
				m_formHandler->SetPendingDropBar(barOwner != nullptr ? barOwner->GetControlID() : 0);
				return target;
			},
			[this](const ibFormDragItem& item, ibValueFrame* target) { item.ApplyDrop(m_formHandler, target); }));
}

wxWindow* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::GetBackgroundWindow() const
{
	return m_back->GetFrameContentPanel();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event)
{
	if (event.GetEventType() == wxEVT_LEFT_DOWN) {
		OnLeftClickFromApp(currentWindow);
	}
	else if (event.GetEventType() == wxEVT_RIGHT_DOWN) {
		OnRightClickFromApp(currentWindow, event);
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnClickBackPanel(wxMouseEvent& event)
{
	m_formHandler->SelectObject(m_formHandler->GetValueForm());
	event.Skip();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnResizeBackPanel(wxCommandEvent& event)
{
	if (ibValueForm* const valueForm = m_formHandler->GetValueForm())
		m_formHandler->SelectObject(valueForm, true);
	event.Skip();
}

bool ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnLeftClickFromApp(wxWindow* currentWindow)
{
	// A TOOL OF A COMMAND BAR — the bar's command it runs is selected, as the desktop's bar did in the designer
	// (BuildCommandBarToolBar: SelectPropertyObject of FindItemByCommandId); a standard command is no item of the bar —
	// the bar's owner then, as a click on the bar elsewhere.
	if (ibValueFrame* const barOwner = GetCommandBarOwner(currentWindow)) {
		const ibViewCommandBar* const bar = barOwner->GetCommandBar();
		const ibProtocolNode entry = bar->FindEntryByPosition(bar->GetWindow()->ScreenToClient(wxGetMousePosition()));
		if (entry.Has(ibProtocolName::Position))
			m_formHandler->GetObjectTree()->SelectCommandItem(barOwner->GetControlID(),
				static_cast<unsigned int>(entry.GetInt(ibProtocolName::Position)));
		else
			m_formHandler->SelectObject(barOwner);
		return true;
	}

	// A control's window, or one inside it; none up to the canvas — the form (the desktop's form was the content
	// panel's own object).
	wxWindow* wnd = currentWindow;
	while (wnd != nullptr && wnd != this) {
		ibValueFrame* founded = GetObjectBase(wnd);
		if (founded != nullptr) {
			ibValueFrame* oldObj = m_formHandler->GetSelectedObject();
			if (oldObj == nullptr || founded != oldObj->GetParent())
				m_formHandler->SelectObject(founded);
			return true;
		}
		wnd = wnd->GetParent();
	}

	m_formHandler->SelectObject(m_formHandler->GetValueForm());
	return true;
}

bool ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnRightClickFromApp(wxWindow* currentWindow, wxMouseEvent& event)
{
	// ON A COMMAND BAR — the bar's own menu (Add command, Paste), not its owner's (a table's "Add column"): the one its
	// node in the tree pops, as the desktop's bar popped it (BuildCommandBarToolBar, wxEVT_RIGHT_DOWN).
	if (const ibValueFrame* const barOwner = GetCommandBarOwner(currentWindow)) {
		ibValueLayerObject* const layer = m_formHandler->GetObjectTree()->FindEntry(barOwner->GetControlID());
		if (layer == nullptr)
			return false;
		wxMenu menu;
		layer->PrepareDefaultMenu(&menu);
		const int sel = currentWindow->GetPopupMenuSelectionFromUser(menu, event.GetPosition());
		if (sel != wxID_NONE)
			layer->ExecuteMenu(sel);
		return true;
	}

	// The control the click is on, as a left click finds it — none: the form, which takes a paste.
	ibValueFrame* founded = nullptr;
	for (wxWindow* wnd = currentWindow; wnd != nullptr && wnd != this && founded == nullptr; wnd = wnd->GetParent())
		founded = GetObjectBase(wnd);
	if (founded == nullptr)
		founded = m_formHandler->GetValueForm();
	if (founded == nullptr)
		return false;

	if (founded != m_formHandler->GetSelectedObject())
		m_formHandler->SelectObject(founded);

	// On the stack: PopupMenu does NOT take ownership, and it blocks until the menu is dismissed, so the selection is
	// fully handled before the scope ends.
	ibVisualEditorItemPopupMenu menu(m_formHandler, currentWindow, founded);
	menu.UpdateUI(&menu);
	currentWindow->PopupMenu(&menu, event.GetPosition());
	return true;
}

ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::GetCommandBarOwner(wxWindow* window) const
{
	// The control the window is in (none: the form) asked for its bar — the window is on it when the bar's is among
	// those it stands in.
	ibValueFrame* owner = nullptr;
	for (wxWindow* w = window; w != nullptr && owner == nullptr; w = w->GetParent())
		owner = GetObjectBase(w);
	if (owner == nullptr)
		owner = m_formHandler->GetValueForm();
	const ibViewCommandBar* const bar = owner != nullptr ? owner->GetCommandBar() : nullptr;
	for (wxWindow* w = window; bar != nullptr && w != nullptr; w = w->GetParent()) {
		if (w == bar->GetWindow())
			return owner;
	}
	return nullptr;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ClearObjectSelect()
{
	m_back->ClearSelection();
	m_back->Refresh();
}

// THE DESKTOP'S SetObjectSelect over the client's controls: what a control is drawn as is asked of it (GetWindow,
// GetSizer) instead of the desktop's host (GetWxObject), and the panel the box is drawn on is the window it sits in —
// a page's or a column's box is its notebook's or its table's, the first of its parents that is drawn as a window.
void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetObjectSelect(ibValueFrame* obj)
{
	if (obj == nullptr || obj == m_formHandler->GetValueForm()) {
		ClearObjectSelect();
		return;
	}

	// The first of it and its parents that is drawn — a column has no window of its own, its table has.
	ibValueFrame* drawn = obj;
	while (drawn != nullptr && drawn->GetWindow() == nullptr && drawn->GetSizer() == nullptr)
		drawn = drawn->GetParent();
	if (drawn == nullptr || drawn == m_formHandler->GetValueForm()) {
		ClearObjectSelect();
		return;
	}

	wxSizer* sizer = nullptr;
	wxObject* item = nullptr;
	wxWindow* selPanel = nullptr;

	if (wxWindow* const window = drawn->GetWindow()) {
		// A notebook's page is boxed as its notebook (the desktop's: asked of the control's class).
		ibValueFrame* const notebook = drawn->GetClassName() == ibProtocolType::NotebookPage ? drawn->GetParent() : nullptr;
		wxWindow* const boxed = notebook != nullptr && notebook->GetWindow() != nullptr ? notebook->GetWindow() : window;
		item = boxed;
		selPanel = boxed->GetParent();
	}
	else {
		sizer = drawn->GetSizer();
		selPanel = sizer->GetContainingWindow();
	}

	m_back->SetSelectedSizer(sizer);
	m_back->SetSelectedItem(item);
	m_back->SetSelectedObject(drawn);
	m_back->SetSelectedPanel(selPanel);

	m_back->Refresh();
	if (selPanel != nullptr)
		selPanel->Refresh();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ScrollToObject(ibValueFrame* obj)
{
	wxWindow* const win = obj != nullptr ? obj->GetWindow() : nullptr;
	if (win == nullptr)
		return;

	// The card scrolls in the canvas.
	int stepx, stepy;
	GetScrollPixelsPerUnit(&stepx, &stepy);
	const wxRect viewRect(GetClientRect());
	const wxRect winRect(ScreenToClient(win->GetScreenPosition()), win->GetSize());
	if (viewRect.Contains(winRect) || stepx <= 0 || stepy <= 0)
		return;

	int startx, starty;
	GetViewStart(&startx, &starty);
	if (winRect.GetTop() < 0 || winRect.GetBottom() > viewRect.GetHeight())
		starty += (winRect.GetTop() < 0 ? winRect.GetTop() : winRect.GetBottom() - viewRect.GetHeight() + stepy) / stepy;
	if (winRect.GetLeft() < 0 || winRect.GetRight() > viewRect.GetWidth())
		startx += (winRect.GetLeft() < 0 ? winRect.GetLeft() : winRect.GetRight() - viewRect.GetWidth() + stepx) / stepx;

	Freeze();
	Scroll(startx, starty);
	Thaw();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetCaption(const wxString& strCaption)
{
	m_back->SetTitle(strCaption);
	m_back->SetTitleStyle(wxCAPTION);
	m_back->ShowTitleBar(true);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::UpdateVirtualSize()
{
	// The card at its best size, the form's; the canvas scrolls over the card and the margin it stands at.
	m_back->Layout();
	m_back->SetClientSize(m_back->GetBestSize());

	const wxPoint margin = m_back->GetPosition();
	SetVirtualSize(m_back->GetSize() + wxSize(margin.x * 2, margin.y * 2));
	Refresh();
}

/////////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_CLASS(ibDesignerWindow, ibInnerFrame);

wxBEGIN_EVENT_TABLE(ibDesignerWindow, ibInnerFrame)
EVT_PAINT(ibDesignerWindow::OnPaint)
wxEND_EVENT_TABLE()

ibDesignerWindow::ibDesignerWindow(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size, long style, const wxString& /*name*/)
	: ibInnerFrame(parent, id, pos, size, style)
{
	ShowTitleBar(false);
	SetGrid(10, FromDIP(10));

	m_selSizer = nullptr;
	m_selItem = nullptr;
	m_actPanel = nullptr;

	SetBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));  // #D8E2EB palest powder — light background so form card pops
}

ibDesignerWindow::~ibDesignerWindow()
{
}

void ibDesignerWindow::SetGrid(int x, int y)
{
	m_x = x;
	m_y = y;
}

void ibDesignerWindow::SetSelectedPanel(wxWindow* actPanel)
{
	// Bound once: the same handler unbound first, so a panel selected again is not painted twice.
	if (actPanel != nullptr && actPanel != this) {
		actPanel->Unbind(wxEVT_PAINT, &ibDesignerWindow::OnPanelPaint, this);
		actPanel->Bind(wxEVT_PAINT, &ibDesignerWindow::OnPanelPaint, this);
	}
	m_actPanel = actPanel;
}

void ibDesignerWindow::OnPaint(wxPaintEvent& event)
{
	// This paint event helps draw the selection boxes
	// when they extend beyond the edges of the content panel
	wxPaintDC dc(this);

	if (m_actPanel == GetFrameContentPanel()) {
		wxPoint origin = GetFrameContentPanel()->GetPosition();
		dc.SetDeviceOrigin(origin.x, origin.y);
		HighlightSelection(dc);
	}

	event.Skip();
}

void ibDesignerWindow::OnPanelPaint(wxPaintEvent& event)
{
	wxWindow* const panel = wxDynamicCast(event.GetEventObject(), wxWindow);
	if (panel != nullptr && panel == m_actPanel) {
		wxPaintDC dc(panel);
		HighlightSelection(dc);
	}

	event.Skip();
}

void ibDesignerWindow::DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, ibValueFrame* object)
{
	int min = (object->GetComponentType() == COMPONENT_TYPE_SIZER ? 0 : 1);

	int border = 0, flag = 0;

	// Out to the border its sizer item lays it out with — the item the client's sizer item set (ibValueSizerItem::Update).
	const wxSizerItem* const item = ibValueSizerItem::GetItem(object);
	if (item != nullptr) {
		border = item->GetBorder(); flag = item->GetFlag();
	}

	if (border == 0)
		border = min;

	int topBorder = (flag & wxTOP) == 0 ? min : border;
	int bottomBorder = (flag & wxBOTTOM) == 0 ? min : border;
	int rightBorder = (flag & wxRIGHT) == 0 ? min : border;
	int leftBorder = (flag & wxLEFT) == 0 ? min : border;

	dc.DrawRectangle(point.x - leftBorder,
		point.y - topBorder,
		size.x + leftBorder + rightBorder,
		size.y + topBorder + bottomBorder);
}

void ibDesignerWindow::HighlightSelection(wxDC& dc)
{
	wxSize size;
	ibValueFrame* object = m_selObj;

	if (m_selSizer != nullptr) {
		// A sizer is laid out where its window's scrolling puts it.
		wxPoint point = m_selSizer->GetPosition();
		if (const wxScrollHelper* scrolled = dynamic_cast<const wxScrollHelper*>(m_selSizer->GetContainingWindow()))
			point = scrolled->CalcScrolledPosition(point);
		size = m_selSizer->GetSize();
		wxPen bluePen(*wxBLUE, 2, wxPENSTYLE_SHORT_DASH);
		dc.SetPen(bluePen);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		DrawRectangle(dc, point, size, object);
	}
	else if (m_selItem != nullptr) {
		wxWindow* windowItem = wxDynamicCast(m_selItem, wxWindow);
		if (windowItem == nullptr || m_actPanel == nullptr)
			return;

		// In case the windowItem is inside a wxStaticBoxSizer its position is relative to
		// the wxStaticBox which is NOT m_actPanel in on which the highlight is painted,
		// so get the screen coordinates of the item and convert them into client coordinates
		// of the panel to get the correct relative coordinates.
		const wxPoint point = m_actPanel->ScreenToClient(windowItem->GetScreenPosition());
		size = windowItem->GetSize();

		if (windowItem->IsShown()) {
			wxPen redPen(*wxRED, 2, wxPENSTYLE_SHORT_DASH);
			dc.SetPen(redPen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			DrawRectangle(dc, point, size, object);
		}
	}
}
