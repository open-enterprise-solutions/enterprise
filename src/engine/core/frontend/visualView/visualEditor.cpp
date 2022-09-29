////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "common/propertyObject.h"
#include "utils/typeconv.h"
#include "frontend/objinspect/events.h"

#include <wx/collpane.h>
#include <wx/dcbuffer.h>

static const int ID_TIMER_SCAN = wxWindow::NewControlId();

wxBEGIN_EVENT_TABLE(CVisualEditorContextForm::CVisualEditorHost, wxScrolledWindow)
EVT_INNER_FRAME_RESIZED(wxID_ANY, CVisualEditorContextForm::CVisualEditorHost::OnResizeBackPanel)
wxEND_EVENT_TABLE()

CVisualEditorContextForm::CVisualEditorHost::CVisualEditorHost(CVisualEditorContextForm* handler, wxWindow* parent, wxWindowID id) :
	IVisualHost(parent, id, wxDefaultPosition, wxDefaultSize),
	m_formHandler(handler),
	m_stopSelectedEvent(false),
	m_stopModifiedEvent(false),
	m_activeControl(NULL)
{
	IVisualHost::SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	m_formHandler->AddHandler(this->GetEventHandler());

#ifdef __WXMSW__
	SetOwnBackgroundColour(wxColour(150, 150, 150));
#else
	SetOwnBackgroundColour(wxColour(192, 192, 192));
#endif

	m_back = new CDesignerWindow(this, wxID_ANY, wxPoint(10, 10));
	m_back->GetEventHandler()->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CVisualEditorContextForm::CVisualEditorHost::OnClickBackPanel), NULL, this);
}

CValueForm* CVisualEditorContextForm::CVisualEditorHost::GetValueForm() const
{
	return m_formHandler->GetValueForm();
}

void CVisualEditorContextForm::CVisualEditorHost::SetValueForm(CValueForm* valueForm)
{
	m_formHandler->SetValueForm(valueForm);
}

CVisualEditorContextForm::CVisualEditorHost::~CVisualEditorHost()
{
	CValueForm* valueForm = m_formHandler->GetValueForm();

	if (valueForm) {
		for (unsigned int i = 0; i < valueForm->GetChildCount(); i++) {
			IValueFrame* objChild = valueForm->GetChild(i);
			wxASSERT(objChild);
			DeleteRecursive(objChild, true);
		}
	}

	m_back->GetFrameContentPanel()->DestroyChildren();
	m_back->GetFrameContentPanel()->SetSizer(NULL); // *!*

	DestroyChildren();

	m_formHandler->RemoveHandler(GetEventHandler());
}

void CVisualEditorContextForm::CVisualEditorHost::OnClickBackPanel(wxMouseEvent& event)
{
	if (m_formHandler->GetValueForm()) {
		m_formHandler->SelectObject(m_formHandler->GetValueForm());
	}

	event.Skip();
}

void CVisualEditorContextForm::CVisualEditorHost::OnResizeBackPanel(wxCommandEvent&)
{
	CValueForm* valueForm = m_formHandler->GetValueForm();

	if (valueForm) {
		//Property *prop(valueForm->GetProperty(wxT("size")));
		//m_formHandler->ModifyProperty(prop, TypeConv::SizeToString(m_back->GetSize()));
		m_formHandler->SelectObject(valueForm, true);
	}

	/*event.Skip();*/
}

/**
* Crea la vista preliminar borrando la previa.
*/
void CVisualEditorContextForm::CVisualEditorHost::CreateVisualEditor()
{
#if !defined(__WXGTK__ )
	if (IsShown()) {
		Freeze();   // Prevent flickering on wx 2.8,
					// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
	}
#endif

	CValueForm* valueForm = m_formHandler->GetValueForm();

	// Clear selections, delete objects
	m_back->SetSelectedItem(NULL);
	m_back->SetSelectedSizer(NULL);
	m_back->SetSelectedObject(NULL);
	m_back->SetSelectedPanel(NULL);

	m_back->Enable(true);

	if (IsShown()) {
		// --- [1] Set the color of the form -------------------------------
		m_back->GetFrameContentPanel()->SetForegroundColour(valueForm->GetForegroundColour());
		m_back->GetFrameContentPanel()->SetBackgroundColour(valueForm->GetBackgroundColour());

		// --- [2] Title bar Setup
		m_back->SetTitle(valueForm->GetCaption());

		m_back->SetTitleStyle(wxCAPTION);
		m_back->ShowTitleBar(true);

		// --- [3] Default sizer Setup 
		m_mainBoxSizer = new wxBoxSizer(valueForm->GetOrient());
		m_back->GetFrameContentPanel()->SetSizer(m_mainBoxSizer);

		for (unsigned int i = 0; i < valueForm->GetChildCount(); i++)
		{
			IValueFrame* child = valueForm->GetChild(i);

			// Recursively generate the ObjectTree
			try {
				// we have to put the content valueForm panel as parentObject in order
				// to SetSizeHints be called.
				GenerateControl(child, m_back->GetFrameContentPanel(), GetFrameSizer());
			}
			catch (std::exception& ex)
			{
				wxLogError(ex.what());
			}

		}

		m_back->Layout();

		m_back->GetSizer()->Fit(m_back);
		m_back->SetClientSize(m_back->GetBestSize());

		m_back->Refresh();
		Refresh();

#if !defined(__WXGTK__)
		Thaw();
#endif
	}

	UpdateVirtualSize();
}

/**
* Crea la vista preliminar borrando la previa.
*/
void CVisualEditorContextForm::CVisualEditorHost::UpdateVisualEditor()
{
#if !defined(__WXGTK__ )
	if (IsShown()) {
		wxScrolledCanvas::Freeze();   // Prevent flickering on wx 2.8,
					// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
	}
#endif

	CValueForm* valueForm = m_formHandler->GetValueForm();

	if (wxScrolledCanvas::IsShown()) {

		wxASSERT(m_mainBoxSizer);

		// --- [1] Set the color of the form -------------------------------
		m_back->GetFrameContentPanel()->SetForegroundColour(valueForm->GetForegroundColour());
		m_back->GetFrameContentPanel()->SetBackgroundColour(valueForm->GetBackgroundColour());

		// --- [2] Title bar Setup
		m_back->SetTitle(valueForm->GetCaption());

		m_back->SetTitleStyle(wxCAPTION);
		m_back->ShowTitleBar(true);

		// --- [3] Default sizer Setup 
		m_mainBoxSizer->SetOrientation(valueForm->GetOrient());

		for (unsigned int i = 0; i < valueForm->GetChildCount(); i++) {
			IValueFrame* child = valueForm->GetChild(i);
			// Recursively generate the ObjectTree
			try {
				// we have to put the content valueForm panel as parentObject in order
				// to SetSizeHints be called.
				RefreshControl(child, m_back->GetFrameContentPanel(), GetFrameSizer());
			}
			catch (std::exception& ex)
			{
				wxLogError(ex.what());
			}
		}

		m_back->Layout();

		m_back->GetSizer()->Fit(m_back);
		m_back->SetClientSize(m_back->GetBestSize());

		m_back->Refresh();
		wxScrolledCanvas::Refresh();

#if !defined(__WXGTK__)
		wxScrolledCanvas::Thaw();
#endif
	}

	UpdateVirtualSize();
}

void CVisualEditorContextForm::CVisualEditorHost::ClearVisualEditor()
{
	CValueForm* valueForm = m_formHandler->GetValueForm();
	wxASSERT(valueForm);

	for (unsigned int i = 0; i < valueForm->GetChildCount(); i++) {
		IValueFrame* objChild = valueForm->GetChild(i);
		DeleteRecursive(objChild, true);
	}

	m_back->GetFrameContentPanel()->DestroyChildren();
	m_back->GetFrameContentPanel()->SetSizer(NULL); // *!*
}

void CVisualEditorContextForm::CVisualEditorHost::PreventOnSelected(bool prevent)
{
	m_stopSelectedEvent = prevent;
}

void CVisualEditorContextForm::CVisualEditorHost::PreventOnModified(bool prevent)
{
	m_stopModifiedEvent = prevent;
}

void CVisualEditorContextForm::CVisualEditorHost::OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event)
{
	if (event.GetEventType() == wxEVT_LEFT_DOWN) {
		OnLeftClickFromApp(currentWindow);
	}
	else if (event.GetEventType() == wxEVT_RIGHT_DOWN) {
		OnRightClickFromApp(currentWindow, event);
	}
}

bool CVisualEditorContextForm::CVisualEditorHost::OnLeftClickFromApp(wxWindow* currentWindow)
{
	wxWindow* wnd = currentWindow;
	while (wnd != NULL) {
		std::map<wxObject*, IValueFrame*>::iterator founded = m_wxObjects.find(wnd);
		if (founded != m_wxObjects.end()) {
			IValueFrame* m_oldObj = m_formHandler->GetSelectedObject();
			wxASSERT(m_oldObj);
			IValueFrame* m_selObj = founded->second;
			wxASSERT(m_selObj);
			wxObject* m_oldWxObj = m_oldObj->GetWxObject();
			wxObject* m_selWxObj = m_selObj->GetWxObject();
			if (founded->second != m_oldObj->GetParent())
				m_formHandler->SelectObject(founded->second);
			break;
		}
		wnd = wnd->GetParent();
	}

	return true;
}

bool CVisualEditorContextForm::CVisualEditorHost::OnRightClickFromApp(wxWindow* currentWindow, wxMouseEvent& event)
{
	wxWindow* wnd = currentWindow;
	while (wnd != NULL) {
		std::map<wxObject*, IValueFrame*>::iterator founded = m_wxObjects.find(wnd);
		if (founded != m_wxObjects.end()) {
			if (founded->second != m_formHandler->GetSelectedObject()) {
				m_formHandler->SelectObject(founded->second);
			}
			CVisualEditorItemPopupMenu* menu = new CVisualEditorItemPopupMenu(m_formHandler, currentWindow, founded->second);
			menu->UpdateUI(menu);
			currentWindow->PopupMenu(menu, event.GetPosition());
			break;
		}
		wnd = wnd->GetParent();
	}
	return true;
}

void CVisualEditorContextForm::CVisualEditorHost::SetObjectSelect(IValueFrame* obj)
{
	CValueForm* valueForm = m_formHandler->GetValueForm();
	wxASSERT(valueForm);

	// Get the IValueFrame from the event
	if (obj == NULL) {
		// Strange...
		wxLogDebug(wxT("The event object is NULL - why?"));
		return;
	}

	// highlight parent toolbar instead of its children
	IValueFrame* toolbar = obj->FindNearAncestor(wxT("toolbar"));

	if (toolbar != NULL)
		obj = toolbar;

	// Make sure this is a visible object
	auto it = m_baseObjects.find(obj);
	if (it == m_baseObjects.end()) {
		m_back->SetSelectedSizer(NULL);
		m_back->SetSelectedItem(NULL);
		m_back->SetSelectedObject(NULL);
		m_back->SetSelectedPanel(NULL);
		m_back->Refresh();
		return;
	}

	// Save wxobject
	wxObject* item = it->second;

	int componentType = obj->GetComponentType();

	// Fire selection event in plugin
	if (!m_stopSelectedEvent)
		OnSelected(obj, item);

	if (componentType != COMPONENT_TYPE_WINDOW &&
		componentType != COMPONENT_TYPE_WINDOW_TABLE) {
		item = NULL;
	}

	// Fire selection event in plugin for all parents
	if (!m_stopSelectedEvent) {
		IValueFrame* parent = obj->GetParent();
		while (parent != NULL) {
			auto parentIt = m_baseObjects.find(parent);
			if (parentIt != m_baseObjects.end()) {
				if (obj->GetClassName() != wxT("page")) {
					OnSelected(parent, parentIt->second);
				}
			}
			parent = parent->GetParent();
		}
	}

	// Look for the active panel - this is where the boxes will be drawn during OnPaint
	// This is the closest parent of type COMPONENT_TYPE_WINDOW
	IValueFrame* nextParent = obj->GetParent();
	while (nextParent != NULL) {
		if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW ||
			nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE) {
			if (!item) {
				if (nextParent->GetClassName() == wxT("sizerItem")) { 
					nextParent = nextParent->GetParent();
				}
				item = GetWxObject(nextParent);
			}
			break;
		}
		else if (nextParent->GetClassName() == wxT("staticboxsizer")) {
			if (!item) {
				wxStaticBoxSizer* m_staticBoxSizer = wxDynamicCast(GetWxObject(nextParent), wxStaticBoxSizer);
				wxASSERT(m_staticBoxSizer);
				item = m_staticBoxSizer->GetStaticBox();
			}
			break;
		}
		else {
			nextParent = nextParent->GetParent();
		}
	}

	// Get the panel to draw on
	wxWindow* selPanel = NULL;
	if (nextParent != NULL) {
		it = m_baseObjects.find(nextParent);
		if (it != m_baseObjects.end()) {
			if (nextParent->GetClassName() == wxT("staticboxsizer")) {
				wxStaticBoxSizer* m_staticBoxSizer = wxDynamicCast(it->second, wxStaticBoxSizer);
				wxASSERT(m_staticBoxSizer);
				selPanel = m_staticBoxSizer->GetStaticBox();
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
	wxSizer* sizer = NULL;
	IValueFrame* nextObj = obj;
	while (nextObj != NULL) {
		if (nextObj->GetComponentType() == COMPONENT_TYPE_SIZER ||
			nextObj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			it = m_baseObjects.find(nextObj);
			if (it != m_baseObjects.end()) { sizer = wxDynamicCast(it->second, wxSizer); }
			break;
		}
		else if (nextObj->GetComponentType() == COMPONENT_TYPE_WINDOW
			|| nextObj->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE) break;
		nextObj = nextObj->GetParent();
	}

	m_back->SetSelectedSizer(sizer);
	m_back->SetSelectedItem(item);
	m_back->SetSelectedObject(obj);
	m_back->SetSelectedPanel(selPanel);

	m_back->Refresh();
}

wxIMPLEMENT_CLASS(CDesignerWindow, CInnerFrame);

BEGIN_EVENT_TABLE(CDesignerWindow, CInnerFrame)
EVT_PAINT(CDesignerWindow::OnPaint)
END_EVENT_TABLE()

CDesignerWindow::CDesignerWindow(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size, long style, const wxString& /*name*/)
	: CInnerFrame(parent, id, pos, size, style)
{
	ShowTitleBar(false);
	SetGrid(10, 10);
	m_selSizer = NULL;
	m_selItem = NULL;
	m_actPanel = NULL;
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
	GetFrameContentPanel()->PushEventHandler(new CHighlightPaintHandler(GetFrameContentPanel()));
}

CDesignerWindow::~CDesignerWindow()
{
	GetFrameContentPanel()->PopEventHandler(true);
}

void CDesignerWindow::SetGrid(int x, int y)
{
	m_x = x;
	m_y = y;
}

void CDesignerWindow::OnPaint(wxPaintEvent& event)
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

void CDesignerWindow::DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, IPropertyObject* object)
{
	bool isSizer = object->GetObjectTypeName() == wxT("sizer");
	int min = (isSizer ? 0 : 1);

	int border = 0, flag = 0;

	if (object->IsSubclassOf(wxT("sizerItem"))) {
		CValueSizerItem* sizerItem = wxDynamicCast(object->GetParent(), CValueSizerItem);
		if (sizerItem != NULL) {
			border = sizerItem->GetBorder();
			flag = sizerItem->GetFlagBorder();
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

void CDesignerWindow::HighlightSelection(wxDC& dc)
{
	wxSize size;
	IPropertyObject* object = m_selObj;

	if (m_selSizer != NULL) {
		wxScrolledWindow* scrolwin = wxDynamicCast(m_selSizer->GetContainingWindow(), wxScrolledWindow);
		if (scrolwin != NULL) {
			scrolwin->FitInside();
		}
		wxPoint point = m_selSizer->GetPosition();
		size = m_selSizer->GetSize();
		wxPen bluePen(*wxBLUE, 2, wxPENSTYLE_SHORT_DASH);
		dc.SetPen(bluePen);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		// Look for the active panel - this is where the boxes will be drawn during OnPaint
		// This is the closest parent of type COMPONENT_TYPE_WINDOW
		while (object != NULL) {
			if (object->GetComponentType() == COMPONENT_TYPE_SIZER)
				break;
			object = object->GetParent();
		}

		if (object->GetClassName() == wxT("staticboxsizer") || object->GetChildCount() > 0)
			DrawRectangle(dc, point, size, object);
	}
	else if (m_selItem != NULL) {
		wxPoint point;
		bool shown = false;
		wxWindow* windowItem = wxDynamicCast(m_selItem, wxWindow);
		wxSizer* sizerItem = wxDynamicCast(m_selItem, wxSizer);
		if (NULL != windowItem) {
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
		else if (NULL != sizerItem) {
			point = sizerItem->GetPosition();
			size = sizerItem->GetSize();
			shown = true;
		}
		else {
			return;
		}

		// Look for the active panel - this is where the boxes will be drawn during OnPaint
		// This is the closest parent of type COMPONENT_TYPE_WINDOW
		while (object != NULL) {
			if (object->GetComponentType() == COMPONENT_TYPE_WINDOW ||
				object->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE)
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

BEGIN_EVENT_TABLE(CDesignerWindow::CHighlightPaintHandler, wxEvtHandler)
EVT_PAINT(CDesignerWindow::CHighlightPaintHandler::OnPaint)
END_EVENT_TABLE()

CDesignerWindow::CHighlightPaintHandler::CHighlightPaintHandler(wxWindow* win) : m_dsgnWin(win)
{
}

void CDesignerWindow::CHighlightPaintHandler::OnPaint(wxPaintEvent& event)
{
	wxWindow* aux = m_dsgnWin;
	while (!aux->IsKindOf(CLASSINFO(CDesignerWindow))) aux = aux->GetParent();
	CDesignerWindow* dsgnWin = (CDesignerWindow*)aux;

	if (dsgnWin->GetActivePanel() == m_dsgnWin) {
		wxPaintDC dc(m_dsgnWin);
		dsgnWin->HighlightSelection(dc);
	}

	event.Skip();
}
