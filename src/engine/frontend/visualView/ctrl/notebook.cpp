#include "notebook.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Special Notebook func                           *
//***********************************************************************************

#ifdef OES_USE_WEB
// Adding a page is the DESIGNER's act, and there is no designer on this road:
// the three below exist so the vtable has them, the way the tablebox's menu
// pair does.
void ibValueNotebook::AddNotebookPage() {}
void ibValueNotebook::PrepareDefaultMenu(wxMenu* /*menu*/) {}
void ibValueNotebook::ExecuteMenu(ibVisualHost* /*visualHost*/, int /*id*/) {}
#else
void ibValueNotebook::AddNotebookPage()
{
	wxASSERT(m_formOwner);

	ibValueFrame* newNotebookPage = m_formOwner->NewObject(g_controlNotebookPageCLSID, this);
	g_visualHostContext->InsertControl(newNotebookPage, this);
	g_visualHostContext->RefreshEditor();
}
#endif

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueNotebook::ibValueNotebook() : ibValueWindow(), m_activePage(nullptr)
{
	m_members.Bind(this, &ibValueNotebook::FillControlMembers);
	//set default params
	//m_minimum_size = wxSize(300, 100);
}

#ifndef OES_USE_WEB
#include "frontend/win/theme/luna_tabart.h"
#endif

wxObject* ibValueNotebook::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	auto* notebook = new ibWebNotebook(GetControlID());
	notebook->Bind(wxEVT_WEB_NOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnWebPageChanged, this);
	return notebook;
#else
	long style = m_propertyOrient->GetValueAsInteger() |
		wxAUI_NB_TAB_MOVE | 
		wxAUI_NB_SCROLL_BUTTONS;
	if (!visualHost->IsDesignerHost())
		style |= wxAUI_NB_TAB_SPLIT;
	wxAuiNotebook* notebook = new wxAuiNotebook(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize, style);
	notebook->SetArtProvider(new wxAuiLunaTabArt());
	return notebook;
#endif
}

void ibValueNotebook::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifdef OES_USE_WEB
	(void)wxobject;
	(void)wxparent;
	(void)visualHost;
	(void)firstCreated;
#else
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstCreated) {
		ibValueNotebook::AddNotebookPage();
	}

	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
		notebook->Bind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
		notebook->Bind(wxEVT_AUINOTEBOOK_BG_DCLICK, &ibValueNotebook::OnBGDClick, this);
	}
#endif
}

void ibValueNotebook::OnSelected(wxObject* wxobject)
{
}

#ifdef OES_USE_WEB
// A notebook with no page in front shows nothing at all, so the answer always
// names one while there is a visible page to name. It also repairs a stale
// choice: a page can be hidden, or removed, after it was picked.
ibValueNotebookPage* ibValueNotebook::ResolveActivePage()
{
	ibValueNotebookPage* firstVisible = nullptr;
	bool activeStillHere = false;

	for (unsigned int i = 0; i < GetChildCount(); i++) {
		auto* page = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
		if (page == nullptr || !page->m_propertyVisible->GetValueAsBoolean())
			continue;
		if (firstVisible == nullptr)
			firstVisible = page;
		if (page == m_activePage)
			activeStillHere = true;
	}

	if (!activeStillHere)
		m_activePage = firstVisible;
	return m_activePage;
}
#endif

void ibValueNotebook::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	auto* notebook = static_cast<ibWebNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->SetOrientation(m_propertyOrient->GetValueAsInteger());
		const ibValueNotebookPage* const active = ResolveActivePage();
		notebook->SetActivePage(active != nullptr ? active->GetControlID() : 0);
	}
#else
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
	}
#endif

	UpdateWindow(notebook);
}

void ibValueNotebook::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	// The pages have Updated by now, so a page turned invisible by a script is
	// visible as such here -- and if it was the one in front, the notebook has
	// to name another before the tree is written out.
	(void)wxparent;
	(void)visualHost;
	auto* notebook = static_cast<ibWebNotebook*>(wxobject);
	if (notebook != nullptr) {
		const ibValueNotebookPage* const active = ResolveActivePage();
		notebook->SetActivePage(active != nullptr ? active->GetControlID() : 0);
	}
#else
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);

	if (notebook != nullptr) {
		long style = m_propertyOrient->GetValueAsInteger() |
			wxAUI_NB_TAB_MOVE |
			wxAUI_NB_SCROLL_BUTTONS;
		if (!visualHost->IsDesignerHost())
			style |= wxAUI_NB_TAB_SPLIT;
		notebook->SetWindowStyle(style);
	}
#endif
}

void ibValueNotebook::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	if (auto* notebook = static_cast<ibWebNotebook*>(wxobject))
		notebook->Unbind(wxEVT_WEB_NOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnWebPageChanged, this);
	// The pages go with the tree; m_activePage points into it and must not
	// outlive it.
	m_activePage = nullptr;
#else
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->Unbind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
		notebook->Unbind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
	}
#endif
}

#ifdef OES_USE_WEB
//**********************************************************************************
//*                                  Events                                        *
//**********************************************************************************

void ibValueNotebook::OnWebPageChanged(wxCommandEvent& event)
{
	ibValueNotebookPage* picked = nullptr;
	for (unsigned int i = 0; i < GetChildCount(); i++) {
		auto* page = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
		if (page != nullptr && page->GetControlID() == event.GetInt())
			picked = page;
	}

	if (picked == nullptr || m_activePage == picked)
		return;

	m_activePage = picked;

	// Same shape the desktop handler has: the script hears about the page it
	// was given, and the form is refreshed either way -- what the tab reveals
	// is drawn from the shims, and they are re-read on the refresh.
	event.Skip(
		CallAsEvent(m_eventOnPageChanged, ibValue(m_activePage))
	);

	if (m_formOwner != nullptr)
		m_formOwner->UpdateForm();
}
#endif

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool ibValueNotebook::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	//events
	m_eventOnPageChanged->SetNodeValue(node.GetProperty(m_eventOnPageChanged->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueNotebook::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	//events
	node.SetProperty(m_eventOnPageChanged->GetName(), m_eventOnPageChanged->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//**********************************************************************************

enum Func {
	enPages = 0,
	enActivePage
};

void ibValueNotebook::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Pages"), wxT("Pages()"));
	helper.AppendFunc(wxT("ActivePage"), wxT("ActivePage()"));
}

#include "backend/system/value/valueMap.h"

bool ibValueNotebook::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)       //method call
{
	switch (lMethodNum)
	{
	case enPages:
	{
		ibValueStructure* structurePage = new ibValueStructure(true);
		for (unsigned int i = 0; i < GetChildCount(); i++) {
			ibValueNotebookPage* notebookPage = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
			if (notebookPage) {
				structurePage->Insert(notebookPage->GetControlName(), ibValue(notebookPage));
			}
		}
#pragma message("nouverbe to nouverbe: needs more work!")
		pvarRetValue = structurePage;
		return true; 
	}
	case enActivePage:
		pvarRetValue = m_activePage;
		return true;
	}

	return ibValueWindow::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueNotebook, "Notebook", "Notebook", g_controlNotebookCLSID);