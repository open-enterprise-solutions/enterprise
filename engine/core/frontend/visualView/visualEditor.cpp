////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "common/objectbase.h"
#include "utils/typeconv.h"
#include "frontend/objinspect/events.h"

#include <wx/collpane.h>
#include <wx/dcbuffer.h>

static const int ID_TIMER_SCAN = wxWindow::NewControlId();

wxBEGIN_EVENT_TABLE(CVisualEditorContextForm::CVisualEditorHost, wxScrolledWindow)
EVT_INNER_FRAME_RESIZED(wxID_ANY, CVisualEditorContextForm::CVisualEditorHost::OnResizeBackPanel)
wxEND_EVENT_TABLE()

CVisualEditorContextForm::CVisualEditorHost::CVisualEditorHost(CVisualEditorContextForm *handler, wxWindow *parent) :
	IVisualHost(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize),
	m_stopSelectedEvent(false),
	m_stopModifiedEvent(false),
	m_activeControl(NULL)
{
	m_formHandler = handler;
	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	m_formHandler->AddHandler(this->GetEventHandler());

#ifdef __WXMSW__
	SetOwnBackgroundColour(wxColour(150, 150, 150));
#else
	SetOwnBackgroundColour(wxColour(192, 192, 192));
#endif

	m_back = new CDesignerWindow(this, wxID_ANY, wxPoint(10, 10));
	m_back->GetEventHandler()->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CVisualEditorContextForm::CVisualEditorHost::OnClickBackPanel), NULL, this);
}

CValueForm *CVisualEditorContextForm::CVisualEditorHost::GetValueForm() const
{
	return m_formHandler->GetValueForm();
}

void CVisualEditorContextForm::CVisualEditorHost::SetValueForm(CValueForm *valueForm)
{
	m_formHandler->SetValueForm(valueForm);
}

CVisualEditorContextForm::CVisualEditorHost::~CVisualEditorHost()
{
	CValueForm *valueForm = m_formHandler->GetValueForm();

	if (valueForm) {
		for (unsigned int i = 0; i < valueForm->GetChildCount(); i++) {
			IValueFrame *objChild = valueForm->GetChild(i);
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

void CVisualEditorContextForm::CVisualEditorHost::OnResizeBackPanel(wxCommandEvent &)
{
	CValueForm *valueForm = m_formHandler->GetSelectedForm();

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

	// Clear selections, delete objects
	m_back->SetSelectedItem(NULL);
	m_back->SetSelectedSizer(NULL);
	m_back->SetSelectedObject(NULL);
	m_back->SetSelectedPanel(NULL);

	m_back->Enable(m_formHandler->m_valueForm->m_enabled);

	if (IsShown()) {
		// --- [1] Set the color of the form -------------------------------
		if (m_formHandler->m_valueForm->m_bg.IsOk()) {
			m_back->GetFrameContentPanel()->SetBackgroundColour(m_formHandler->m_valueForm->m_bg);
		}
		else {
			m_back->GetFrameContentPanel()->SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
		}

		// --- [2] Title bar Setup
		m_back->SetTitle(m_formHandler->m_valueForm->m_caption);

		m_back->SetTitleStyle(m_formHandler->m_valueForm->m_style);
		m_back->ShowTitleBar((m_formHandler->m_valueForm->m_style & wxCAPTION) != 0);

		// --- [3] Default sizer Setup 
		m_mainBoxSizer = new wxBoxSizer(m_formHandler->m_valueForm->m_orient);
		m_back->GetFrameContentPanel()->SetSizer(m_mainBoxSizer);

		// --- [4] Create the components of the form -------------------------
		// Used to save valueForm objects for later display
		IValueFrame *menubar = NULL;
		wxWindow* statusbar = NULL;
		wxWindow* toolbar = NULL;

		for (unsigned int i = 0; i < m_formHandler->m_valueForm->GetChildCount(); i++)
		{
			IValueFrame *child = m_formHandler->m_valueForm->GetChild(i);

			if (!menubar && (m_formHandler->m_valueForm->GetObjectTypeName() == wxT("menubar_form")))
			{
				// main form acts as a menubar
				menubar = m_formHandler->m_valueForm;
			}
			else if (child->GetObjectTypeName() == wxT("menubar"))
			{
				// Create the menubar later
				menubar = child;
			}
			else if (toolbar == NULL &&
				m_formHandler->m_valueForm->GetObjectTypeName() == wxT("toolbar_form")) {
				GenerateControl(m_formHandler->m_valueForm, m_back->GetFrameContentPanel(), m_back->GetFrameContentPanel());
				auto it = m_baseObjects.find(m_formHandler->GetValueForm());
				toolbar = wxDynamicCast(it->second, wxToolBar);
				break;
			}
			else
			{
				// Recursively generate the ObjectTree
				try
				{
					// we have to put the content valueForm panel as parentObject in order
					// to SetSizeHints be called.
					GenerateControl(child, m_back->GetFrameContentPanel(), GetFrameSizer());
				}
				catch (std::exception& ex)
				{
					wxLogError(ex.what());
				}
			}

			// Attach the toolbar (if any) to the valueForm
			if (child->GetClassName() == wxT("toolbar")) {
				auto it = m_baseObjects.find(child);
				toolbar = wxDynamicCast(it->second, wxAuiToolBar);
			}

			// Attach the status bar (if any) to the valueForm
			if (child->IsSubclassOf(wxT("statusbar"))) {
				auto it = m_baseObjects.find(child);
				statusbar = wxDynamicCast(it->second, wxStatusBar);
			}
		}

		if (menubar || statusbar || toolbar) {
			m_back->SetFrameWidgets(menubar, toolbar, statusbar);
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
	if (IsShown())
	{
		Freeze();   // Prevent flickering on wx 2.8,
					// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
	}
#endif

	if (IsShown())
	{
		wxASSERT(m_mainBoxSizer);

		// --- [1] Set the color of the form -------------------------------
		if (m_formHandler->m_valueForm->m_bg.IsOk()) {
			m_back->GetFrameContentPanel()->SetBackgroundColour(m_formHandler->m_valueForm->m_bg);
		}
		else {
			m_back->GetFrameContentPanel()->SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
		}

		// --- [2] Title bar Setup
		m_back->SetTitle(m_formHandler->m_valueForm->m_caption);

		m_back->SetTitleStyle(m_formHandler->m_valueForm->m_style);
		m_back->ShowTitleBar((m_formHandler->m_valueForm->m_style & wxCAPTION) != 0);

		// --- [3] Default sizer Setup 
		m_mainBoxSizer->SetOrientation(m_formHandler->m_valueForm->m_orient);

		// --- [4] Create the components of the form -------------------------
		// Used to save valueForm objects for later display
		IValueFrame *menubar = NULL;
		wxWindow* statusbar = NULL;
		wxWindow* toolbar = NULL;

		for (unsigned int i = 0; i < m_formHandler->m_valueForm->GetChildCount(); i++) {
			IValueFrame *child = m_formHandler->m_valueForm->GetChild(i);

			if (!menubar && (m_formHandler->m_valueForm->GetObjectTypeName() == wxT("menubar_form"))) {
				// main form acts as a menubar
				menubar = m_formHandler->m_valueForm;
			}
			else if (child->GetObjectTypeName() == wxT("menubar")) {
				// Create the menubar later
				menubar = child;
			}
			else if (toolbar == NULL &&
				m_formHandler->m_valueForm->GetObjectTypeName() == wxT("toolbar_form")) {
				RefreshControl(m_formHandler->m_valueForm, m_back->GetFrameContentPanel(), m_back->GetFrameContentPanel());

				auto it = m_baseObjects.find(m_formHandler->m_valueForm);
				toolbar = wxDynamicCast(it->second, wxToolBar);

				break;
			}
			else {
				// Recursively generate the ObjectTree
				try
				{
					// we have to put the content valueForm panel as parentObject in order
					// to SetSizeHints be called.
					RefreshControl(child, m_back->GetFrameContentPanel(), GetFrameSizer());
				}
				catch (std::exception& ex)
				{
					wxLogError(ex.what());
				}
			}

			// Attach the toolbar (if any) to the valueForm
			if (child->GetClassName() == wxT("toolbar")) {
				auto it = m_baseObjects.find(child);
				toolbar = wxDynamicCast(it->second, wxAuiToolBar);
			}

			// Attach the status bar (if any) to the valueForm
			if (child->IsSubclassOf(wxT("statusbar"))) {
				auto it = m_baseObjects.find(child);
				statusbar = wxDynamicCast(it->second, wxStatusBar);
			}
		}

		if (menubar || statusbar || toolbar) {
			m_back->SetFrameWidgets(menubar, toolbar, statusbar);
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

void CVisualEditorContextForm::CVisualEditorHost::ClearVisualEditor()
{
	CValueForm *m_valueForm = m_formHandler->GetValueForm();
	wxASSERT(m_valueForm);

	for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++)
	{
		IValueFrame *objChild = m_valueForm->GetChild(i);
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

void CVisualEditorContextForm::CVisualEditorHost::OnClickFromApp(wxWindow *currentWindow, wxMouseEvent &event)
{
	if (event.GetEventType() == wxEVT_LEFT_DOWN) {
		OnLeftClickFromApp(currentWindow);
	}
	else if (event.GetEventType() == wxEVT_RIGHT_DOWN) {
		OnRightClickFromApp(currentWindow, event);
	}
}

bool CVisualEditorContextForm::CVisualEditorHost::OnLeftClickFromApp(wxWindow *currentWindow)
{
	wxWindow *m_wnd = currentWindow;
	while (m_wnd)
	{
		std::map<wxObject*, IValueFrame *>::iterator founded = m_wxObjects.find(m_wnd);
		if (founded != m_wxObjects.end())
		{
			IValueFrame *m_oldObj = m_formHandler->GetSelectedObject();
			wxASSERT(m_oldObj);
			IValueFrame *m_selObj = founded->second;
			wxASSERT(m_selObj);

			wxObject *m_oldWxObj = m_oldObj->GetWxObject();
			wxObject *m_selWxObj = m_selObj->GetWxObject();

			if (founded->second != m_oldObj->GetParent())
				m_formHandler->SelectObject(founded->second);

			break;
		}
		m_wnd = m_wnd->GetParent();
	}

	return true;
}

bool CVisualEditorContextForm::CVisualEditorHost::OnRightClickFromApp(wxWindow *currentWindow, wxMouseEvent &event)
{
	wxWindow *m_window = currentWindow;
	while (m_window)
	{
		std::map<wxObject*, IValueFrame *>::iterator founded = m_wxObjects.find(m_window);
		if (founded != m_wxObjects.end())
		{
			IValueFrame* menu = NULL;

			if (founded->second != m_formHandler->GetSelectedObject()) { m_formHandler->SelectObject(founded->second); }

			for (unsigned int i = 0; i < founded->second->GetChildCount(); i++)
			{
				if (founded->second->GetChild(i)->GetObjectTypeName() == wxT("menu")) { menu = founded->second->GetChild(i); break; }
			}

			if (menu) {
				PopupMenu(CDesignerWindow::GetMenuFromObject(menu), event.GetPosition());
			}
			else {
				CVisualEditorItemPopupMenu *menu = new CVisualEditorItemPopupMenu(m_formHandler, currentWindow, founded->second);
				menu->UpdateUI(menu); currentWindow->PopupMenu(menu, event.GetPosition());
			}

			/*window->ClientToScreen(&event.m_x, &event.m_y);
			window->GetParent()->ScreenToClient(&event.m_x, &event.m_y);*/
			break;
		}
		m_window = m_window->GetParent();
	}
	return true;
}

void CVisualEditorContextForm::CVisualEditorHost::SetObjectSelect(IValueFrame *obj)
{
	// It is only necessary to Create() if the selected object is on a different form
	if (m_formHandler->m_valueForm != m_formHandler->GetSelectedForm()) CreateVisualEditor();

	// Get the IValueFrame from the event
	if (!obj)
	{
		// Strange...
		//LogDebug(wxT("The event object is NULL - why?"));
		return;
	}

	// highlight parent toolbar instead of its children
	IValueFrame* toolbar = obj->FindNearAncestor(wxT("toolbar"));
	if (!toolbar) toolbar = obj->FindNearAncestor(wxT("toolbar_form"));
	if (toolbar) obj = toolbar;

	// Make sure this is a visible object
	auto it = m_baseObjects.find(obj);
	if (it == m_baseObjects.end())
	{
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
	if (!m_stopSelectedEvent) OnSelected(obj, item);

	if (componentType != COMPONENT_TYPE_WINDOW &&
		componentType != COMPONENT_TYPE_WINDOW_TABLE) {
		item = NULL;
	}

	// Fire selection event in plugin for all parents
	if (!m_stopSelectedEvent)
	{
		IValueFrame* parent = obj->GetParent();
		while (parent)
		{
			auto parentIt = m_baseObjects.find(parent);

			if (parentIt != m_baseObjects.end())
			{
				if (obj->GetClassName() != wxT("page"))
				{
					OnSelected(parent, parentIt->second);
				}
			}

			parent = parent->GetParent();
		}
	}

	// Look for the active panel - this is where the boxes will be drawn during OnPaint
	// This is the closest parent of type COMPONENT_TYPE_WINDOW
	IValueFrame* nextParent = obj->GetParent();
	while (nextParent)
	{
		if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW ||
			nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE)
		{
			if (!item)
			{
				if (nextParent->GetClassName() == wxT("sizerItem")) { nextParent = nextParent->GetParent(); }
				item = GetWxObject(nextParent);
			}
			break;
		}
		else if (nextParent->GetClassName() == wxT("staticboxsizer"))
		{
			if (!item)
			{
				wxStaticBoxSizer *m_staticBoxSizer = wxDynamicCast(GetWxObject(nextParent), wxStaticBoxSizer);
				wxASSERT(m_staticBoxSizer);
				item = m_staticBoxSizer->GetStaticBox();
			}
			break;
		}
		else { nextParent = nextParent->GetParent(); }
	}

	// Get the panel to draw on
	wxWindow* selPanel = NULL;
	if (nextParent)
	{
		it = m_baseObjects.find(nextParent);
		if (it != m_baseObjects.end())
		{
			if (nextParent->GetClassName() == wxT("staticboxsizer"))
			{
				wxStaticBoxSizer *m_staticBoxSizer = wxDynamicCast(it->second, wxStaticBoxSizer);
				wxASSERT(m_staticBoxSizer);
				selPanel = m_staticBoxSizer->GetStaticBox();
			}
			else
			{
				selPanel = wxDynamicCast(it->second, wxWindow);
			}
		}
		else
		{
			selPanel = m_back->GetFrameContentPanel();
		}
	}
	else
	{
		selPanel = m_back->GetFrameContentPanel();
	}

	// Find the first COMPONENT_TYPE_WINDOW or COMPONENT_TYPE_SIZER
	// If it is a sizer, save it
	wxSizer* sizer = NULL;
	IValueFrame* nextObj = obj;
	while (nextObj)
	{
		if (nextObj->GetComponentType() == COMPONENT_TYPE_SIZER || nextObj->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		{
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

IMPLEMENT_CLASS(CDesignerWindow, CInnerFrame)

BEGIN_EVENT_TABLE(CDesignerWindow, CInnerFrame)
EVT_PAINT(CDesignerWindow::OnPaint)
END_EVENT_TABLE()

CDesignerWindow::CDesignerWindow(wxWindow *parent, int id, const wxPoint& pos, const wxSize &size, long style, const wxString & /*name*/)
	: CInnerFrame(parent, id, pos, size, style)
{
	ShowTitleBar(false);
	SetGrid(10, 10);
	m_selSizer = NULL;
	m_selItem = NULL;
	m_actPanel = NULL;

	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

	GetFrameContentPanel()->PushEventHandler(new HighlightPaintHandler(GetFrameContentPanel()));
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

void CDesignerWindow::OnPaint(wxPaintEvent &event)
{
	// This paint event helps draw the selection boxes
	// when they extend beyond the edges of the content panel
	wxPaintDC dc(this);

	if (m_actPanel == GetFrameContentPanel())
	{
		wxPoint origin = GetFrameContentPanel()->GetPosition();
		dc.SetDeviceOrigin(origin.x, origin.y);
		HighlightSelection(dc);
	}

	event.Skip();
}

void CDesignerWindow::DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, IValueFrame* object)
{
	bool isSizer = object->GetObjectTypeName() == wxT("sizer");
	int min = (isSizer ? 0 : 1);

	int border = 0, flag = 0;

	if (object->IsSubclassOf(wxT("sizerItem")))
	{
		CValueSizerItem *m_sizerItem = wxDynamicCast(object->GetParent(), CValueSizerItem);
		if (m_sizerItem)
		{
			border = m_sizerItem->m_border;
			flag = m_sizerItem->m_flag_border;
		}
	}

	if (!border) border = min;

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
	IValueFrame* object = m_selObj;

	if (m_selSizer)
	{
		wxScrolledWindow* scrolwin = wxDynamicCast(m_selSizer->GetContainingWindow(), wxScrolledWindow);
		if (scrolwin) { scrolwin->FitInside(); }
		wxPoint point = m_selSizer->GetPosition();

		size = m_selSizer->GetSize();

		wxPen bluePen(*wxBLUE, 2, wxPENSTYLE_SHORT_DASH);
		dc.SetPen(bluePen);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		// Look for the active panel - this is where the boxes will be drawn during OnPaint
		// This is the closest parent of type COMPONENT_TYPE_WINDOW
		while (object)
		{
			if (object->GetComponentType() == COMPONENT_TYPE_SIZER) break;
			object = object->GetParent();
		}

		if (object->GetClassName() == wxT("staticboxsizer") || object->GetChildCount() > 0) DrawRectangle(dc, point, size, object);
	}
	else if (m_selItem)
	{
		wxPoint point;
		bool shown = false;

		wxWindow* windowItem = wxDynamicCast(m_selItem, wxWindow);
		wxSizer* sizerItem = wxDynamicCast(m_selItem, wxSizer);
		if (NULL != windowItem)
		{
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
		else if (NULL != sizerItem)
		{
			point = sizerItem->GetPosition();
			size = sizerItem->GetSize();
			shown = true;
		}
		else return;

		// Look for the active panel - this is where the boxes will be drawn during OnPaint
		// This is the closest parent of type COMPONENT_TYPE_WINDOW
		while (object)
		{
			if (object->GetComponentType() == COMPONENT_TYPE_WINDOW || 
				object->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE) 
				break;

			object = object->GetParent();
		}

		if (shown)
		{
			wxPen redPen(*wxRED, 2, wxPENSTYLE_SHORT_DASH);
			dc.SetPen(redPen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			DrawRectangle(dc, point, size, object);
		}
	}
}

wxMenu* CDesignerWindow::GetMenuFromObject(IValueFrame* menu)
{
	int lastMenuId = wxID_HIGHEST + 1;
	wxMenu* menuWidget = new wxMenu();
	for (unsigned int j = 0; j < menu->GetChildCount(); j++)
	{
		IValueFrame* menuItem = menu->GetChild(j);
		if (menuItem->GetObjectTypeName() == wxT("submenu"))
		{
			wxMenuItem *item = new wxMenuItem(menuWidget,
				lastMenuId++,
				menuItem->GetPropertyAsString(wxT("label")),
				menuItem->GetPropertyAsString(wxT("help")),
				wxITEM_NORMAL,
				GetMenuFromObject(menuItem)
			);
			item->SetBitmap(menuItem->GetPropertyAsBitmap(wxT("bitmap")));
			menuWidget->Append(item);
		}
		else if (menuItem->GetClassName() == wxT("separator"))
		{
			menuWidget->AppendSeparator();
		}
		else
		{
			wxString label = menuItem->GetPropertyAsString(wxT("label"));
			wxString shortcut = menuItem->GetPropertyAsString(wxT("shortcut"));
			if (!shortcut.IsEmpty())
			{
				label = label + wxChar('\t') + shortcut;
			}

			wxMenuItem *item = new wxMenuItem(menuWidget,
				lastMenuId++,
				label,
				menuItem->GetPropertyAsString(wxT("help")),
				(wxItemKind)menuItem->GetPropertyAsInteger(wxT("kind"))
			);

			if (!menuItem->GetProperty(wxT("bitmap"))->IsNull())
			{
				wxBitmap unchecked = wxNullBitmap;
				if (!menuItem->GetProperty(wxT("unchecked_bitmap"))->IsNull())
				{
					unchecked = menuItem->GetPropertyAsBitmap(wxT("unchecked_bitmap"));
				}
#ifdef __WXMSW__
				item->SetBitmaps(menuItem->GetPropertyAsBitmap(wxT("bitmap")), unchecked);
#elif defined( __WXGTK__ )
				item->SetBitmap(menuItem->GetPropertyAsBitmap(wxT("bitmap")));
#endif
				}
			else
			{
				if (!menuItem->GetProperty(wxT("unchecked_bitmap"))->IsNull())
				{
#ifdef __WXMSW__
					item->SetBitmaps(wxNullBitmap, menuItem->GetPropertyAsBitmap(wxT("unchecked_bitmap")));
#endif
				}
			}

			menuWidget->Append(item);

			if (item->GetKind() == wxITEM_CHECK &&
				menuItem->GetPropertyAsInteger(wxT("checked")) != 0) {
				item->Check(true);
			}

			item->Enable((menuItem->GetPropertyAsInteger(wxT("enabled")) != 0));
			}
		}

	return menuWidget;
	}

void CDesignerWindow::SetFrameWidgets(IValueFrame* menubar, wxWindow *toolbar, wxWindow *statusbar)
{
	wxWindow *contentPanel = GetFrameContentPanel();
	wxSizer *mainSizer = contentPanel->GetSizer();
	contentPanel->SetSizer(NULL, false);

	wxSizer *dummySizer = new wxBoxSizer(wxVERTICAL);

	/*if (mbWidget)
	{
		dummySizer->Add(mbWidget, 0, wxEXPAND | wxTOP | wxBOTTOM, 0);
		dummySizer->Add(new wxStaticLine(contentPanel, wxID_ANY), 0, wxEXPAND | wxALL, 0);
	}*/

	wxSizer* contentSizer = dummySizer;
	if (toolbar)
	{
		if ((toolbar->GetWindowStyle() & wxTB_VERTICAL) != 0)
		{
			wxSizer* horiz = new wxBoxSizer(wxHORIZONTAL);
			horiz->Add(toolbar, 0, wxEXPAND | wxALL, 0);

			wxSizer* vert = new wxBoxSizer(wxVERTICAL);
			horiz->Add(vert, 1, wxEXPAND, 0);

			dummySizer->Add(horiz, 1, wxEXPAND, 0);

			contentSizer = vert;
		}
		else
		{
			dummySizer->Add(toolbar, 0, wxEXPAND | wxALL, 0);
		}
	}

	if (mainSizer)
	{
		contentSizer->Add(mainSizer, 1, wxEXPAND | wxALL, 0);
		if (mainSizer->GetChildren().IsEmpty())
		{
			// Sizers do not expand if they are empty
			mainSizer->AddStretchSpacer(1);
		}
	}
	else
	{
		contentSizer->AddStretchSpacer(1);
	}

	if (statusbar)
	{
		contentSizer->Add(statusbar, 0, wxEXPAND | wxALL, 0);
	}

	contentPanel->SetSizer(dummySizer, false);
	contentPanel->Layout();
}

BEGIN_EVENT_TABLE(CDesignerWindow::HighlightPaintHandler, wxEvtHandler)
EVT_PAINT(CDesignerWindow::HighlightPaintHandler::OnPaint)
END_EVENT_TABLE()

CDesignerWindow::HighlightPaintHandler::HighlightPaintHandler(wxWindow *win) : m_dsgnWin(win)
{
}

void CDesignerWindow::HighlightPaintHandler::OnPaint(wxPaintEvent &event)
{
	wxWindow *aux = m_dsgnWin;
	while (!aux->IsKindOf(CLASSINFO(CDesignerWindow))) aux = aux->GetParent();
	CDesignerWindow *dsgnWin = (CDesignerWindow*)aux;

	if (dsgnWin->GetActivePanel() == m_dsgnWin)
	{
		wxPaintDC dc(m_dsgnWin);
		dsgnWin->HighlightSelection(dc);
	}

	event.Skip();
}
