////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "backend/propertyManager/propertyManager.h"
#include "backend/sourceDescription.h"   // ibSourceDescription / ibSourceDescriptionMemory (load the drag payload)
#include "backend/fileSystem/fs.h"       // ibReaderMemory (drag payload buffer)

#include <wx/dnd.h>       // wxDropTarget / wxCustomDataObject — form-canvas accepts a dragged attribute node
#include <wx/utils.h>     // wxFindWindowAtPoint (point -> widget hit-test)

static const int ID_TIMER_SCAN = wxScrolledWindow::NewControlId();

// Form-canvas drop target: receives a node dragged from the attribute tree (oes_source_drag format) and
// creates a control at the drop point, bound to the node's PATH. The payload is the source path, serialized
// by the engine's ibSourceDescriptionMemory (the tree's OnBeginDrag) — loaded back the same way here; the
// control class is resolved from the type AT the path on THIS (drop) side. ONE target on the content panel —
// child controls don't intercept the drop (the designer's mouse filter special-cases only mouse-down, not
// DnD), so the point is hit-tested to an object manually in DropBoundControl.
class ibFormEditorDropTarget : public wxDropTarget {
public:
	explicit ibFormEditorDropTarget(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* host)
		: wxDropTarget(new wxCustomDataObject(wxDataFormat(wxT("oes_source_drag")))), m_host(host) {}

	wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override {
		if (!GetData())
			return wxDragNone;
		wxCustomDataObject* obj = static_cast<wxCustomDataObject*>(GetDataObject());
		if (obj != nullptr && obj->GetSize() > 0) {
			ibReaderMemory reader(obj->GetData(), (int)obj->GetSize());
			ibSourceDescription srcDesc;
			ibSourceDescriptionMemory::LoadData(reader, srcDesc);   // raw id path — metadata-agnostic
			if (srcDesc.IsOk())
				m_host->DropBoundControl(x, y, srcDesc);   // pass the WRAPPER through (no decompose/rewrap)
		}
		return def;
	}
private:
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* m_host;
};

wxBEGIN_EVENT_TABLE(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost, wxScrolledWindow)
EVT_INNER_FRAME_RESIZED(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnResizeBackPanel)
wxEND_EVENT_TABLE()

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::ibVisualEditorHost(ibVisualEditor* handler, wxWindow* parent, wxWindowID id) :
	ibVisualHost(parent, id, wxDefaultPosition, wxDefaultSize),
	m_formHandler(handler),
	m_stopSelectedEvent(false),
	m_stopModifiedEvent(false)
{
	ibVisualHost::SetExtraStyle(wxWS_EX_BLOCK_EVENTS);

	SetOwnBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));  // #D8E2EB palest powder — light background so form card pops

	m_back = new ibDesignerWindow(this, wxID_ANY, wxPoint(10, 10));
	// Default MAIN sizer (m_mainSizer) on the designer card's content panel —
	// same model as runtime (GetBackgroundWindow() == the content panel here).
	InitMainSizer();
	m_back->GetEventHandler()->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnClickBackPanel), nullptr, this);

	// The form canvas accepts a node dragged from the attribute tree → create + bind a control there.
	m_back->GetFrameContentPanel()->SetDropTarget(new ibFormEditorDropTarget(this));
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::DropBoundControl(
	wxCoord x, wxCoord y, const ibSourceDescription& desc)
{
	// The OS gives only the drop coordinates, so map the point to the widget under it, then walk wx
	// parents to the owning ibValueFrame — the same resolution OnLeftClickFromApp uses for a click.
	// Fall back to the form root when the point maps to nothing (drop on empty canvas). CreateBoundControl
	// resolves the control class from the type at the path (the drop side owns the class choice).
	wxWindow* panel = m_back->GetFrameContentPanel();
	const wxPoint screenPt = panel->ClientToScreen(wxPoint(x, y));
	ibValueFrame* target = nullptr;
	for (wxWindow* w = wxFindWindowAtPoint(screenPt); w != nullptr && target == nullptr; w = w->GetParent())
		target = GetObjectBase(w);
	if (target == nullptr)
		target = GetValueForm();
	m_formHandler->CreateBoundControl(target, desc);
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
	ibValueForm* valueForm = m_formHandler->GetValueForm();
	if (valueForm != nullptr)
		ClearControl(valueForm, true);

	m_back->GetFrameContentPanel()->DestroyChildren();
	m_back->GetFrameContentPanel()->SetSizer(nullptr); // *!*

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
	wxWindow* wnd = currentWindow;
	while (wnd != nullptr) {
		ibValueFrame* founded = GetObjectBase(wnd);
		if (founded != nullptr) {
			if (founded != m_formHandler->GetSelectedObject()) {
				m_formHandler->SelectObject(founded);
			}
			ibVisualEditorItemPopupMenu* menu = new ibVisualEditorItemPopupMenu(m_formHandler, currentWindow, founded);
			menu->UpdateUI(menu);
			currentWindow->PopupMenu(menu, event.GetPosition());
			break;
		}
		wnd = wnd->GetParent();
	}
	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetObjectSelect(ibValueFrame* obj)
{
	// Get the ibValueFrame from the event
	if (obj == nullptr) {
		// Strange...
		wxLogDebug(wxT("The event object is nullptr - why?"));
		return;
	}

	// highlight parent toolbar instead of its children
	ibValueFrame* toolbar = obj->FindNearAncestor(wxT("toolbar"));
	if (toolbar != nullptr)
		obj = toolbar;

	// Make sure this is a visible object
	auto it = m_baseObjects.find(obj);
	if (it == m_baseObjects.end()) {
		m_back->SetSelectedSizer(nullptr);
		m_back->SetSelectedItem(nullptr);
		m_back->SetSelectedObject(nullptr);
		m_back->SetSelectedPanel(nullptr);
		m_back->Refresh();
		return;
	}

	// Save wxobject
	wxObject* item = it->second;

	int componentType = obj->GetComponentType();

	// Fire selection event in plugin
	if (!m_stopSelectedEvent)
		OnSelected(obj, item);

	if (componentType != COMPONENT_TYPE_WINDOW) {
		item = nullptr;
	}
	else if (obj->GetClassName() == wxT("NotebookPage")) {
		ibValueFrame* parent = obj->GetParent();
		item = m_baseObjects.at(parent);
	}
	else if (obj->GetClassName() == wxT("TableboxColumn")) {
		ibValueFrame* parent = obj->GetParent();
		item = m_baseObjects.at(parent);
	}

	// Fire selection event in plugin for all parents
	if (!m_stopSelectedEvent) {
		ibValueFrame* parent = obj->GetParent();
		while (parent != nullptr) {
			auto parentIt = m_baseObjects.find(parent);
			if (parentIt != m_baseObjects.end()) {
				if (obj->GetClassName() != wxT("NotebookPage")) {
					OnSelected(parent, parentIt->second);
				}
			}
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
	wxWindow* selPanel = nullptr;
	if (nextParent != nullptr) {
		it = m_baseObjects.find(nextParent);
		if (it != m_baseObjects.end()) {
			if (nextParent->GetClassName() == wxT("Staticboxsizer")) {
				wxStaticBoxSizer* staticBoxSizer = wxDynamicCast(it->second, wxStaticBoxSizer);
				wxASSERT(staticBoxSizer);
				selPanel = staticBoxSizer->GetStaticBox();
			}
			else if (nextParent->GetClassName() == wxT("Notebook") ||
				nextParent->GetClassName() == wxT("Tablebox")) {
				wxWindow* notebook = wxDynamicCast(it->second, wxWindow);
				wxASSERT(notebook);
				selPanel = notebook->GetParent();
			}
			else {
				selPanel = wxDynamicCast(it->second, wxWindow);
			}
		}
		else {
			selPanel = m_back->GetFrameContentPanel();
		}
	}
	else {
		selPanel = m_back->GetFrameContentPanel();
	}

	// Find the first COMPONENT_TYPE_WINDOW or COMPONENT_TYPE_SIZER
	// If it is a sizer, save it
	wxSizer* sizer = nullptr;
	ibValueFrame* nextObj = obj;
	while (nextObj != nullptr) {
		if (nextObj->GetComponentType() == COMPONENT_TYPE_SIZER ||
			nextObj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			it = m_baseObjects.find(nextObj);
			if (it != m_baseObjects.end()) {
				sizer = wxDynamicCast(it->second, wxSizer);
			} break;
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
	auto it = m_baseObjects.find(obj);
	if (it != m_baseObjects.end()) {

		// Save wxobject
		wxObject* item = it->second;

		if (obj->GetComponentType() == COMPONENT_TYPE_WINDOW) {

			const wxRect viewRect(m_targetWindow->GetClientRect());

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

			if (win->GetParent() != m_targetWindow)
			{
				wxWindow* parent = win->GetParent();
				wxSize parent_size = parent->GetSize();
				if (parent_size.GetWidth() <= viewRect.GetWidth() &&
					parent_size.GetHeight() <= viewRect.GetHeight())
					// make the immediate parent visible instead of the focused control
					win = parent;
			}

			// make win position relative to the m_targetWindow viewing area instead of
			// its parent
			const wxRect
				winRect(m_targetWindow->ScreenToClient(win->GetScreenPosition()),
					win->GetSize());

			// check if it's fully visible
			if (viewRect.Contains(winRect))
			{
				// it is, nothing to do
				return;
			}

			// do make the window fit inside the view area by scrolling to it
			int stepx, stepy;
			GetScrollPixelsPerUnit(&stepx, &stepy);

			int startx, starty;
			GetViewStart(&startx, &starty);

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

			wxScrolledCanvas::Freeze();
			wxScrolledCanvas::Scroll(startx, starty);
			wxScrolledCanvas::Thaw();
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

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::SetOrientation(int orient)
{
	ApplyContentOrientation(orient);
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