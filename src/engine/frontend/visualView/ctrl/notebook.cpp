#include "notebook.h"
#include "form.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueNotebook, ibValueWindow);

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

bool ibValueNotebook::LoadData(ibReaderMemory& reader)
{
	m_propertyOrient->SetValue(reader.r_s32());

	//events
	m_eventOnPageChanged->LoadData(reader);
	return ibValueWindow::LoadData(reader);
}

bool ibValueNotebook::SaveData(ibWriterMemory writer)
{
	writer.w_s32(m_propertyOrient->GetValueAsInteger());

	//events
	m_eventOnPageChanged->SaveData(writer);
	return ibValueWindow::SaveData(writer);
}

//**********************************************************************************

enum Func {
	enPages = 0,
	enActivePage
};

void ibValueNotebook::PrepareNames() const // this method is automatically called to initialize attribute and method names.
{
	ibValueFrame::PrepareNames();

	m_methodHelper->AppendFunc(wxT("Pages"), wxT("Pages()"));
	m_methodHelper->AppendFunc(wxT("ActivePage"), wxT("ActivePage()"));
}

#include "backend/system/value/valueMap.h"

bool ibValueNotebook::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)       //method call
{
	switch (lMethodNum)
	{
	case enPages:
	{
		ibValueStructure* structurePage = ibValue::CreateAndPrepareValueRef<ibValueStructure>(true);
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