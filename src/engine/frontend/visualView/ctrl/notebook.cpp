#include "notebook.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Special Notebook func                           *
//***********************************************************************************

void ibValueNotebook::AddNotebookPage()
{
	wxASSERT(m_formOwner);

	ibValueFrame* newNotebookPage = m_formOwner->NewObject(g_controlNotebookPageCLSID, this);
	g_visualHostContext->InsertControl(newNotebookPage, this);
	g_visualHostContext->RefreshEditor();
}

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueNotebook::ibValueNotebook() : ibValueWindow(), m_activePage(nullptr)
{
	m_members.Bind(this, &ibValueNotebook::FillControlMembers);
	//set default params
	//m_minimum_size = wxSize(300, 100);
}

#include "frontend/win/theme/luna_tabart.h"

wxObject* ibValueNotebook::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
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
}

void ibValueNotebook::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstСreated) {
		ibValueNotebook::AddNotebookPage();
	}

	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
		notebook->Bind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
		notebook->Bind(wxEVT_AUINOTEBOOK_BG_DCLICK, &ibValueNotebook::OnBGDClick, this);
	}
}

void ibValueNotebook::OnSelected(wxObject* wxobject)
{
}

void ibValueNotebook::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
	}

	UpdateWindow(notebook);
}

void ibValueNotebook::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);

	if (notebook != nullptr) {
		long style = m_propertyOrient->GetValueAsInteger() |
			wxAUI_NB_TAB_MOVE |
			wxAUI_NB_SCROLL_BUTTONS;
		if (!visualHost->IsDesignerHost())
			style |= wxAUI_NB_TAB_SPLIT;
		notebook->SetWindowStyle(style);
	}
}

void ibValueNotebook::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->Unbind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
		notebook->Unbind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
	}
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool ibValueNotebook::ReadData(const ibDataNode& node)
{
	m_propertyOrient->ReadNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	//events
	m_eventOnPageChanged->ReadNodeValue(node.GetProperty(m_eventOnPageChanged->GetName()));
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
#pragma message("nouverbe to nouverbe: необходимо доработать!")
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