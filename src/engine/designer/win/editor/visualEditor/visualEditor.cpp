////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "backend/propertyManager/propertyManager.h"
#include "visualEditorDragItem.h"        // ibFormDragItem / ibSourceDragItem / ibCommandDragItem — the polymorphic drop kinds
#include "frontend/win/ctrls/toolBar.h"  // ibAuiToolBar — a command-bar toolbar right-click is served by the bar's own menu
#include "frontend/visualView/layers/commandBar.h"  // ibCommandBarFromToolBar — route a command dropped on a toolbar to its bar

#include <wx/dnd.h>       // wxDropTarget / wxCustomDataObject — form-canvas accepts a dragged attribute node
#include <wx/dataobj.h>   // wxDataObjectComposite — accept every registered drag kind on one target
#include <wx/utils.h>     // wxFindWindowAtPoint (point -> widget hit-test)
#include <wx/app.h>       // wxTheApp->CallAfter (deferred apply for tablebox-grid drops)

static const int ID_TIMER_SCAN = wxScrolledWindow::NewControlId();

// Form-editor drop target: receives a node dragged from a form-editor tree and enacts it on the form. It registers
// the draggable KINDS (ibFormDragItem — a source path → a bound control, a command → a command button); OnData
// matches the received wire format to a kind, decodes it and hands it to the surface's applier. Shared by the form
// CANVAS, the OBJECT TREE and each tablebox grid — each supplies only its point→frame resolver and its editor-bound
// applier. Adding a draggable is a new ibFormDragItem subclass registered below; OnData never changes.
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
					// DEFER out of the OS drop callback: a target on a tablebox grid re-wires (frees) itself when
					// the apply rebuilds the editor → use-after-free. Clone the DECODED item so it outlives THIS
					// target, and run the apply after the drag unwinds.
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

wxBEGIN_EVENT_TABLE(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost, wxPanel)
EVT_INNER_FRAME_RESIZED(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnResizeBackPanel)
wxEND_EVENT_TABLE()

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ibVisualEditorHost(ibVisualEditor* handler, wxWindow* parent, wxWindowID id) :
	ibVisualHost(parent, id, wxDefaultPosition, wxDefaultSize),
	m_formHandler(handler),
	m_stopSelectedEvent(false),
	m_stopModifiedEvent(false)
{
	ibVisualHost::SetExtraStyle(wxWS_EX_BLOCK_EVENTS);

	// The canvas the card sits on IS the host's inner scrolling window now — the host itself
	// is the facade panel around it.
	GetContentWindow()->SetOwnBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));  // #D8E2EB palest powder — light background so form card pops

	m_back = new ibDesignerWindow(GetContentWindow(), wxID_ANY, wxPoint(10, 10));
	m_back->GetEventHandler()->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnClickBackPanel), nullptr, this);

	// The form canvas accepts any registered drag KIND: a source path → a bound control, a command → a bound
	// command button. It supplies only the RESOLVER (map the OS drop point to the ibValueFrame under it, by
	// walking wx parents to the owning object — the same resolution a click uses) and the APPLIER (run the decoded
	// item's ApplyDrop with THIS editor). A null resolve is fine: a source falls back to the form root, a command
	// climbs to the nearest command bar. Same shared target the object tree and tablebox grids use.
	m_back->GetFrameContentPanel()->SetDropTarget(
		new ibSourceDragDropTarget(
			[this](wxCoord x, wxCoord y) -> ibValueFrame* {
				const wxPoint screenPt = m_back->GetFrameContentPanel()->ClientToScreen(wxPoint(x, y));
				// A command dropped on a command-bar TOOLBAR (the form's or a tablebox's) joins THAT bar — a new item,
				// not a free button. Detect the tagged toolbar under the point and hand its bar to CreateCommandButton;
				// cleared when the drop is elsewhere. Set BEFORE the object walk (which returns the frame, not the bar).
				ibValueCommandBar* dropBar = nullptr;
				for (wxWindow* w = wxFindWindowAtPoint(screenPt); w != nullptr && dropBar == nullptr; w = w->GetParent())
					dropBar = ibCommandBarFromToolBar(w);
				m_formHandler->SetPendingDropBar(dropBar);
				ibValueFrame* target = nullptr;
				for (wxWindow* w = wxFindWindowAtPoint(screenPt); w != nullptr && target == nullptr; w = w->GetParent())
					target = GetObjectBase(w);
				return target;
			},
			[this](const ibFormDragItem& item, ibValueFrame* target) { item.ApplyDrop(m_formHandler, target); }));
}

ibValueForm* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::GetValueForm() const
{
	return m_formHandler->GetValueForm();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetValueForm(ibValueForm* valueForm)
{
	m_formHandler->SetValueForm(valueForm);
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::~ibVisualEditorHost()
{
	// The same teardown a rebuild does — chrome first, then the controls — so nothing is left
	// for wx to free a second time when the windows below go.
	ClearVisualHost();

	DestroyChildren();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnClickBackPanel(wxMouseEvent& event)
{
	if (m_formHandler->GetValueForm()) {
		m_formHandler->SelectObject(m_formHandler->GetValueForm());
	}

	event.Skip();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnResizeBackPanel(wxCommandEvent& event)
{
	ibValueForm* valueForm = m_formHandler->GetValueForm();

	if (valueForm) {
		//ibProperty*prop(valueForm->GetProperty(wxT("size")));
		//m_formHandler->ModifyProperty(prop, typeConv::SizeToString(m_back->GetSize()));
		m_formHandler->SelectObject(valueForm, true);
	}

	event.Skip();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::PreventOnSelected(bool prevent)
{
	m_stopSelectedEvent = prevent;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::PreventOnModified(bool prevent)
{
	m_stopModifiedEvent = prevent;
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

bool ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnLeftClickFromApp(wxWindow* currentWindow)
{
	wxWindow* wnd = currentWindow;
	while (wnd != nullptr) {
		ibValueFrame* founded = GetObjectBase(wnd);
		if (founded != nullptr) {
			ibValueFrame* oldObj = m_formHandler->GetSelectedObject();
			wxASSERT(oldObj);
			if (founded != oldObj->GetParent())
				m_formHandler->SelectObject(founded);
			break;
		}
		wnd = wnd->GetParent();
	}

	return true;
}

bool ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnRightClickFromApp(wxWindow* currentWindow, wxMouseEvent& event)
{
	// A right-click on a control's command-bar TOOLBAR is served by the toolbar's OWN handler
	// (BuildCommandBarToolBar binds wxEVT_RIGHT_DOWN -> the bar's "Add command" menu). Don't pop the
	// owning control's menu (e.g. a table's "Add column") over it — return so the app filter's Skip lets
	// that handler run. Body of the table -> the control's own menu (Add column); toolbar -> Add command.
	for (wxWindow* w = currentWindow; w != nullptr && !w->IsKindOf(CLASSINFO(ibVisualHost)); w = w->GetParent())
		if (dynamic_cast<ibAuiToolBar*>(w) != nullptr)
			return true;

	wxWindow* wnd = currentWindow;
	while (wnd != nullptr) {
		ibValueFrame* founded = GetObjectBase(wnd);
		if (founded != nullptr) {
			if (founded != m_formHandler->GetSelectedObject()) {
				m_formHandler->SelectObject(founded);
			}
			// On the stack: PopupMenu does NOT take ownership, and it blocks until the menu is
			// dismissed, so the selection is fully handled before the scope ends. Same shape as
			// every other popup here (userList, activeUser, codeEditor) — no delete to forget.
			ibVisualEditorItemPopupMenu menu(m_formHandler, currentWindow, founded);
			menu.UpdateUI(&menu);
			currentWindow->PopupMenu(&menu, event.GetPosition());
			break;
		}
		wnd = wnd->GetParent();
	}
	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ClearObjectSelect()
{
	// Drop the canvas control highlight — a non-control element (an attribute / command) is now the selection, so a
	// lingering control box would read as a SECOND selection. Mirrors the "not a visible object" clear below.
	m_back->SetSelectedSizer(nullptr);
	m_back->SetSelectedItem(nullptr);
	m_back->SetSelectedObject(nullptr);
	m_back->SetSelectedPanel(nullptr);
	m_back->Refresh();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetObjectSelect(ibValueFrame* obj)
{
	// Get the ibValueFrame from the event
	if (obj == nullptr) {
		// Strange...
		ibJournalInfo(wxT("designer"), wxT("The event object is nullptr - why?"));
		return;
	}

	// highlight parent toolbar instead of its children
	ibValueFrame* toolbar = obj->FindNearAncestor(wxT("toolbar"));
	if (toolbar != nullptr)
		obj = toolbar;

	// Make sure this is a visible object
	wxObject* item = GetWxObject(obj);
	if (item == nullptr) {
		m_back->SetSelectedSizer(nullptr);
		m_back->SetSelectedItem(nullptr);
		m_back->SetSelectedObject(nullptr);
		m_back->SetSelectedPanel(nullptr);
		m_back->Refresh();
		return;
	}

	int componentType = obj->GetComponentType();

	// Fire selection event in plugin
	if (!m_stopSelectedEvent)
		OnSelected(obj, item);

	if (componentType != COMPONENT_TYPE_WINDOW) {
		item = nullptr;
	}
	else if (obj->GetClassName() == wxT("NotebookPage")) {
		item = GetWxObject(obj->GetParent());
	}
	else if (obj->GetClassName() == wxT("TableboxColumn")) {
		item = GetWxObject(obj->GetParent());
	}

	// Fire selection event in plugin for all parents
	if (!m_stopSelectedEvent) {
		ibValueFrame* parent = obj->GetParent();
		while (parent != nullptr) {
			wxObject* parentObj = GetWxObject(parent);
			if (parentObj != nullptr && obj->GetClassName() != wxT("NotebookPage"))
				OnSelected(parent, parentObj);
			parent = parent->GetParent();
		}
	}

	// Look for the active panel - this is where the boxes will be drawn during OnPaint
	// This is the closest parent of type COMPONENT_TYPE_WINDOW
	ibValueFrame* nextParent = obj->GetParent();
	while (nextParent != nullptr) {
		if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
			if (item == nullptr) {
				if (nextParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
					nextParent = nextParent->GetParent();
				item = GetWxObject(nextParent);
			} break;
		}
		else if (nextParent->GetClassName() == wxT("Staticboxsizer")) {
			if (item == nullptr) {
				wxStaticBoxSizer* staticBoxSizer = wxDynamicCast(GetWxObject(nextParent), wxStaticBoxSizer);
				wxASSERT(staticBoxSizer);
				item = staticBoxSizer->GetStaticBox();
			} break;
		}
		else {
			nextParent = nextParent->GetParent();
		}
	}

	// Get the panel to draw on
	wxWindow* selPanel = m_back->GetFrameContentPanel();
	if (wxObject* parentObj = nextParent != nullptr ? GetWxObject(nextParent) : nullptr) {
		if (nextParent->GetClassName() == wxT("Staticboxsizer")) {
			wxStaticBoxSizer* staticBoxSizer = wxDynamicCast(parentObj, wxStaticBoxSizer);
			wxASSERT(staticBoxSizer);
			selPanel = staticBoxSizer->GetStaticBox();
		}
		else if (nextParent->GetClassName() == wxT("Notebook") ||
			nextParent->GetClassName() == wxT("Tablebox")) {
			wxWindow* notebook = wxDynamicCast(parentObj, wxWindow);
			wxASSERT(notebook);
			selPanel = notebook->GetParent();
		}
		else {
			selPanel = wxDynamicCast(parentObj, wxWindow);
		}
	}

	// Find the first COMPONENT_TYPE_WINDOW or COMPONENT_TYPE_SIZER
	// If it is a sizer, save it
	wxSizer* sizer = nullptr;
	ibValueFrame* nextObj = obj;
	while (nextObj != nullptr) {
		if (nextObj->GetComponentType() == COMPONENT_TYPE_SIZER ||
			nextObj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			sizer = wxDynamicCast(GetWxObject(nextObj), wxSizer);
			break;
		}
		else if (nextObj->GetComponentType() == COMPONENT_TYPE_WINDOW)
			break;
		nextObj = nextObj->GetParent();
	}

	m_back->SetSelectedSizer(sizer);
	m_back->SetSelectedItem(item);
	m_back->SetSelectedObject(obj);
	m_back->SetSelectedPanel(selPanel);

	m_back->Refresh();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ScrollToObject(ibValueFrame* obj)
{
	// Make sure this is a visible object
	wxObject* const item = GetWxObject(obj);
	if (item != nullptr) {

		// Scrolling belongs to the host's inner window now — the host itself is the facade.
		ibContentWindow* const scrollWindow = GetContentWindow();
		wxWindow* const targetWindow = scrollWindow->GetTargetWindow();

		if (obj->GetComponentType() == COMPONENT_TYPE_WINDOW) {

			const wxRect viewRect(targetWindow->GetClientRect());

			// For composite controls such as wxComboCtrl we should try to fit the
			// entire control inside the visible area of the target window, not just
			// the focused child of the control. Otherwise we'd make only the textctrl
			// part of a wxComboCtrl visible and the button would still be outside the
			// scrolled area.  But do so only if the parent fits *entirely* inside the
			// scrolled window. In other situations, such as nested wxPanel or
			// wxScrolledWindows, the parent might be way too big to fit inside the
			// scrolled window. If that is the case, then make only the focused window
			// visible

			const wxWindow* win = dynamic_cast<wxWindow*>(item);
			wxASSERT(win);

			if (win->GetParent() != targetWindow)
			{
				wxWindow* parent = win->GetParent();
				wxSize parent_size = parent->GetSize();
				if (parent_size.GetWidth() <= viewRect.GetWidth() &&
					parent_size.GetHeight() <= viewRect.GetHeight())
					// make the immediate parent visible instead of the focused control
					win = parent;
			}

			// make win position relative to the target window viewing area instead of
			// its parent
			const wxRect
				winRect(targetWindow->ScreenToClient(win->GetScreenPosition()),
					win->GetSize());

			// check if it's fully visible
			if (viewRect.Contains(winRect))
			{
				// it is, nothing to do
				return;
			}

			// do make the window fit inside the view area by scrolling to it
			int stepx, stepy;
			scrollWindow->GetScrollPixelsPerUnit(&stepx, &stepy);

			int startx, starty;
			scrollWindow->GetViewStart(&startx, &starty);

			// first in vertical direction:
			if (stepy > 0)
			{
				int diff = 0;

				if (winRect.GetTop() < 0)
				{
					diff = winRect.GetTop();
				}
				else if (winRect.GetBottom() > viewRect.GetHeight())
				{
					diff = winRect.GetBottom() - viewRect.GetHeight() + 1;
					// round up to next scroll step if we can't get exact position,
					// so that the window is fully visible:
					diff += stepy - 1;
				}

				starty = (starty * stepy + diff) / stepy;
			}

			// then horizontal:
			if (stepx > 0)
			{
				int diff = 0;

				if (winRect.GetLeft() < 0)
				{
					diff = winRect.GetLeft();
				}
				else if (winRect.GetRight() > viewRect.GetWidth())
				{
					diff = winRect.GetRight() - viewRect.GetWidth() + 1;
					// round up to next scroll step if we can't get exact position,
					// so that the window is fully visible:
					diff += stepx - 1;
				}

				startx = (startx * stepx + diff) / stepx;
			}

			scrollWindow->Freeze();
			scrollWindow->Scroll(startx, starty);
			scrollWindow->Thaw();
		}
		else if (obj != nullptr) {
			ScrollToObject(obj->GetParent()); 
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::UpdateHostSize()
{
	// --- Set sizer properties
	if (m_back != nullptr) {
		m_back->Layout();
		m_back->SetClientSize(m_back->GetBestSize());
	}

	// The card sized itself; the canvas around it still needs the base's pass.
	ibVisualHost::UpdateHostSize();
}

#include "backend/metaCollection/partial/commonObject.h"

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetCaption(const wxString& strCaption)
{
	const ibValueForm* handler = m_formHandler->GetValueForm();
	if (strCaption.IsEmpty()) {
		const ibSourceDataObject* srcObject = handler->GetSourceObject();
		if (srcObject != nullptr) {
			const ibValueMetaObjectFormBase* metaFormObject = handler->GetFormMetaObject();
			const ibValueMetaObjectGenericData* genericObject = srcObject->GetSourceMetaObject();
			if (genericObject != nullptr) {
				m_back->SetTitle(genericObject->GetSynonym() + wxT(": ") + metaFormObject->GetSynonym());
			}
			else if (metaFormObject != nullptr) {
				m_back->SetTitle(metaFormObject->GetSynonym());
			}
		}
		else {
			const ibValueMetaObjectFormBase* metaFormObject = handler->GetFormMetaObject();
			if (metaFormObject != nullptr) m_back->SetTitle(metaFormObject->GetSynonym());
		}
	}
	else {
		m_back->SetTitle(strCaption);
	}

	m_back->SetTitleStyle(wxCAPTION);
	m_back->ShowTitleBar(true);
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
	GetFrameContentPanel()->PushEventHandler(
		new ibHighlightPaintHandler(GetFrameContentPanel())
	);
}

ibDesignerWindow::~ibDesignerWindow()
{
	GetFrameContentPanel()->PopEventHandler(true);
}

void ibDesignerWindow::SetGrid(int x, int y)
{
	m_x = x;
	m_y = y;
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

void ibDesignerWindow::DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, ibValueFrame* object)
{
	int min = (object->GetObjectTypeName() == wxT("sizer") ? 0 : 1);

	int border = 0, flag = 0;

	if (object->IsSubclassOf(wxT("sizerItem"))) {
		ibValueSizerItem* sizerItem = dynamic_cast<ibValueSizerItem*>(object->GetParent());
		if (sizerItem != nullptr) {
			border = sizerItem->GetBorder(); flag = sizerItem->GetFlagBorder();
		}
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
		wxScrolledWindow* scrolwin = wxDynamicCast(m_selSizer->GetContainingWindow(), wxScrolledWindow);
		if (scrolwin != nullptr) {
			scrolwin->FitInside();
		}
		wxPoint point = m_selSizer->GetPosition();
		size = m_selSizer->GetSize();
		wxPen bluePen(*wxBLUE, 2, wxPENSTYLE_SHORT_DASH);
		dc.SetPen(bluePen);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		// Look for the active panel - this is where the boxes will be drawn during OnPaint
		// This is the closest parent of type COMPONENT_TYPE_WINDOW
		while (object != nullptr) {
			if (object->GetComponentType() == COMPONENT_TYPE_SIZER)
				break;
			object = object->GetParent();
		}

		if (object->GetClassName() == wxT("Staticboxsizer") || object->GetChildCount() > 0)
			DrawRectangle(dc, point, size, object);
	}
	else if (m_selItem != nullptr) {
		wxPoint point;
		bool shown = false;
		wxWindow* windowItem = wxDynamicCast(m_selItem, wxWindow);
		wxSizer* sizerItem = wxDynamicCast(m_selItem, wxSizer);
		if (nullptr != windowItem) {
			// In case the windowItem is inside a wxStaticBoxSizer its position is relative to
			// the wxStaticBox which is NOT m_actPanel in on which the highlight is painted,
			// so get the screen coordinates of the item and convert them into client coordinates
			// of the panel to get the correct relative coordinates. This doesn't do any harm if
			// the item is not inside a wxStaticBoxSizer, if this conversion results in a big
			// performance penalty maybe check if the parent is a wxStaticBox and only then do
			// this conversion.
			point = m_actPanel->ScreenToClient(windowItem->GetScreenPosition());
			size = windowItem->GetSize();
			shown = windowItem->IsShown();
		}
		else if (nullptr != sizerItem) {
			point = sizerItem->GetPosition();
			size = sizerItem->GetSize();
			shown = true;
		}
		else {
			return;
		}

		// Look for the active panel - this is where the boxes will be drawn during OnPaint
		// This is the closest parent of type COMPONENT_TYPE_WINDOW
		while (object != nullptr) {
			if ((object->GetComponentType() == COMPONENT_TYPE_WINDOW)
				&& object->GetClassName() != wxT("notebookPage")
				&& object->GetClassName() != wxT("tableboxColumn"))
				break;
			object = object->GetParent();
		}

		if (shown) {
			wxPen redPen(*wxRED, 2, wxPENSTYLE_SHORT_DASH);
			dc.SetPen(redPen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			DrawRectangle(dc, point, size, object);
		}
	}
}

BEGIN_EVENT_TABLE(ibDesignerWindow::ibHighlightPaintHandler, wxEvtHandler)
EVT_PAINT(ibDesignerWindow::ibHighlightPaintHandler::OnPaint)
END_EVENT_TABLE()

ibDesignerWindow::ibHighlightPaintHandler::ibHighlightPaintHandler(wxWindow* win) : m_dsgnWin(win)
{
}

void ibDesignerWindow::ibHighlightPaintHandler::OnPaint(wxPaintEvent& event)
{
	wxWindow* aux = m_dsgnWin;
	while (!aux->IsKindOf(CLASSINFO(ibDesignerWindow))) aux = aux->GetParent();
	ibDesignerWindow* dsgnWin = (ibDesignerWindow*)aux;

	if (dsgnWin->GetActivePanel() == m_dsgnWin) {
		wxPaintDC dc(m_dsgnWin);
		dsgnWin->HighlightSelection(dc);
	}

	event.Skip();
}