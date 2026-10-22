////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
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
	// The canvas the card sits on — the window the host scrolls in.
	GetContentWindow()->SetOwnBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));  // #D8E2EB palest powder — light background so form card pops

	m_back = new ibDesignerWindow(GetContentWindow(), wxID_ANY, wxPoint(10, 10));
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
		if (entry.Has(ibProtocol::ibForm::ibCommandBar::Position))
			m_formHandler->GetObjectTree()->SelectCommandItem(barOwner->GetControlID(),
				static_cast<unsigned int>(entry.GetInt(ibProtocol::ibForm::ibCommandBar::Position)));
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

	// THE CONTROL AND ITS PARENTS HEAR IT, as the desktop's SetObjectSelect said it to them (OnSelected): a page, or the
	// page a selected control sits on, shows itself in its notebook.
	for (ibValueFrame* selected = obj; selected != nullptr; selected = selected->GetParent())
		selected->OnSelected();

	// The first of it and its parents that is drawn — a column has no window of its own, its table has.
	ibValueFrame* drawn = obj;
	while (drawn != nullptr && drawn->GetWindow() == nullptr && drawn->GetSizer() == nullptr)
		drawn = drawn->GetParent();
	if (drawn == nullptr || drawn == m_formHandler->GetValueForm()) {
		ClearObjectSelect();
		return;
	}

	wxSizer* sizer = nullptr;
	wxWindow* item = nullptr;
	wxWindow* selPanel = nullptr;

	if (wxWindow* const window = drawn->GetWindow()) {
		// A notebook's page is boxed as its notebook (the desktop's: asked of the control's class).
		ibValueFrame* const notebook = drawn->GetClassName() == ibProtocol::ibForm::ibType::NotebookPage ? drawn->GetParent() : nullptr;
		wxWindow* const boxed = notebook != nullptr && notebook->GetWindow() != nullptr ? notebook->GetWindow() : window;
		item = boxed;
		selPanel = boxed->GetParent();
	}
	else {
		sizer = drawn->GetSizer();
		selPanel = sizer->GetContainingWindow();
	}

	m_back->SetSelection(drawn, item, sizer, selPanel);

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
	wxScrolledCanvas* const canvas = GetContentWindow();
	int stepx, stepy;
	canvas->GetScrollPixelsPerUnit(&stepx, &stepy);
	const wxRect viewRect(canvas->GetClientRect());
	const wxRect winRect(canvas->ScreenToClient(win->GetScreenPosition()), win->GetSize());
	if (viewRect.Contains(winRect) || stepx <= 0 || stepy <= 0)
		return;

	int startx, starty;
	canvas->GetViewStart(&startx, &starty);
	if (winRect.GetTop() < 0 || winRect.GetBottom() > viewRect.GetHeight())
		starty += (winRect.GetTop() < 0 ? winRect.GetTop() : winRect.GetBottom() - viewRect.GetHeight() + stepy) / stepy;
	if (winRect.GetLeft() < 0 || winRect.GetRight() > viewRect.GetWidth())
		startx += (winRect.GetLeft() < 0 ? winRect.GetLeft() : winRect.GetRight() - viewRect.GetWidth() + stepx) / stepx;

	canvas->Freeze();
	canvas->Scroll(startx, starty);
	canvas->Thaw();
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

	// Unscrolled: scrolled, the card stands where the scrolling moved it, and the canvas would shrink by twice that.
	wxScrolledCanvas* const canvas = GetContentWindow();
	const wxPoint margin = canvas->CalcUnscrolledPosition(m_back->GetPosition());
	canvas->SetVirtualSize(m_back->GetSize() + wxSize(margin.x * 2, margin.y * 2));
	canvas->Refresh();
}

/////////////////////////////////////////////////////////////////////////////////

ibDesignerWindow::ibDesignerWindow(wxWindow* parent, wxWindowID id, const wxPoint& pos)
	: ibInnerFrame(parent, id, pos, wxDefaultSize, 0)
{
	ShowTitleBar(false);
	SetBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));  // #D8E2EB palest powder — light background so form card pops

	Bind(wxEVT_PAINT, &ibDesignerWindow::OnPaint, this);
}

void ibDesignerWindow::SetSelection(ibValueFrame* object, wxWindow* window, wxSizer* sizer, wxWindow* panel)
{
	m_selectedObject = object;
	m_selectedWindow = window;
	m_selectedSizer = sizer;
	m_selectedPanel = panel;

	// Bound once: the same handler unbound first, so a panel selected again is not painted twice.
	if (panel != nullptr && panel != this) {
		panel->Unbind(wxEVT_PAINT, &ibDesignerWindow::OnPanelPaint, this);
		panel->Bind(wxEVT_PAINT, &ibDesignerWindow::OnPanelPaint, this);
	}
}

bool ibDesignerWindow::GetSelectionRect(wxRect& rect) const
{
	if (m_selectedSizer != nullptr) {
		// Where the scrolling of its window puts it.
		wxPoint position = m_selectedSizer->GetPosition();
		if (const wxScrollHelper* scrolled = dynamic_cast<const wxScrollHelper*>(m_selectedSizer->GetContainingWindow()))
			position = scrolled->CalcScrolledPosition(position);
		rect = wxRect(position, m_selectedSizer->GetSize());
	}
	else if (m_selectedWindow != nullptr && m_selectedPanel != nullptr && m_selectedWindow->IsShown()) {
		// Through the screen: a window in a static box stands in the box, not in the panel.
		rect = wxRect(m_selectedPanel->ScreenToClient(m_selectedWindow->GetScreenPosition()), m_selectedWindow->GetSize());
	}
	else {
		return false;
	}

	// Out to the border the sizer item gives it (ibValueSizerItem::Update) on the sides it borders; elsewhere a pixel
	// round a window and nothing round a sizer.
	const wxSizerItem* const item = m_selectedObject != nullptr ? ibValueSizerItem::GetItem(m_selectedObject) : nullptr;
	const int least = m_selectedObject != nullptr && m_selectedObject->GetComponentType() == COMPONENT_TYPE_SIZER ? 0 : 1;
	const int border = item != nullptr && item->GetBorder() != 0 ? item->GetBorder() : least;
	const auto side = [item, border, least](int direction) {
		return item != nullptr && (item->GetFlag() & direction) != 0 ? border : least;
	};

	rect.x -= side(wxLEFT);
	rect.y -= side(wxTOP);
	rect.width += side(wxLEFT) + side(wxRIGHT);
	rect.height += side(wxTOP) + side(wxBOTTOM);
	return true;
}

void ibDesignerWindow::DrawSelection(wxDC& dc) const
{
	wxRect rect;
	if (!GetSelectionRect(rect))
		return;

	dc.SetPen(wxPen(m_selectedSizer != nullptr ? *wxBLUE : *wxRED, 2, wxPENSTYLE_SHORT_DASH));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(rect);
}

void ibDesignerWindow::OnPaint(wxPaintEvent& event)
{
	wxPaintDC dc(this);
	if (m_selectedPanel != nullptr && m_selectedPanel == GetFrameContentPanel()) {
		const wxPoint origin = m_selectedPanel->GetPosition();
		dc.SetDeviceOrigin(origin.x, origin.y);
		DrawSelection(dc);
	}

	event.Skip();
}

void ibDesignerWindow::OnPanelPaint(wxPaintEvent& event)
{
	wxWindow* const panel = wxDynamicCast(event.GetEventObject(), wxWindow);
	if (panel != nullptr && panel == m_selectedPanel) {
		wxPaintDC dc(panel);
		DrawSelection(dc);
	}

	event.Skip();
}
