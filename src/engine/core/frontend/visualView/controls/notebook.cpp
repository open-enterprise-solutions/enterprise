#include "notebook.h"
#include "frontend/visualView/visualEditor.h"
#include "compiler/methods.h"

static wxWindowID PAGE_ID = wxNewId();

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueNotebook, IValueWindow);

//***********************************************************************************
//*                                 Special Notebook func                           *
//***********************************************************************************

void CValueNotebook::AddNotebookPage()
{
	wxASSERT(m_formOwner);

	IValueControl *newNotebookPage =
		m_formOwner->NewObject("page", this);

	newNotebookPage->ReadProperty();

	m_visualHostContext->InsertObject(newNotebookPage, this);
	newNotebookPage->SaveProperty();

	m_visualHostContext->RefreshEditor();
}

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

CValueNotebook::CValueNotebook() : IValueWindow(), m_orientPage(wxAUI_NB_TOP),
m_bInitialized(false), 
m_activePage(NULL)
{
	PropertyContainer *categoryNotebook = IObjectBase::CreatePropertyContainer("Notebook");
	categoryNotebook->AddProperty("name", PropertyType::PT_WXNAME);
	categoryNotebook->AddProperty("orient_page", PropertyType::PT_OPTION, &CValueNotebook::GetOrientPage);	
	m_category->AddCategory(categoryNotebook);

	PropertyContainer *eventsNotebook = IObjectBase::CreatePropertyContainer("Events");
	eventsNotebook->AddEvent("onPageChanged", { "page" });
	m_category->AddCategory(eventsNotebook);

	//set default params
	//m_minimum_size = wxSize(300, 100);
}

enum
{
	enPages = 0,
	enActivePage
};

void CValueNotebook::PrepareNames() const //этот метод автоматически вызывается для инициализации имен атрибутов и методов
{
	IValueFrame::PrepareNames();

	std::vector<SEng> aMethods =
	{
		{"pages", "pages()"},
		{"activePage", "activePage()"},
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

#include "compiler/valueMap.h"

CValue CValueNotebook::Method(methodArg_t &aParams)       //вызов метода
{
	switch (aParams.GetIndex())
	{
	case enPages:
	{
		CValueStructure *m_pageMap = new CValueStructure(true);

		for (unsigned int i = 0; i < GetChildCount(); i++)
		{
			CValueNotebookPage *notebookPage = dynamic_cast<CValueNotebookPage *>(GetChild(i));
			if (notebookPage) {
				m_pageMap->Insert(notebookPage->GetControlName(), CValue(notebookPage));
			}
		}
#pragma message("nouverbe to nouverbe: необходимо доработать!")
		return m_pageMap;
	}
	case enActivePage: return m_activePage;
	}

	return IValueFrame::Method(aParams);
}

#include "frontend/theme/luna_auitabart.h"

wxObject* CValueNotebook::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxAuiNotebook *m_notebook = new wxAuiNotebook((wxWindow*)parent, wxID_ANY,
		m_pos,
		m_size, m_orientPage);

	m_notebook->SetArtProvider(new CLunaTabArt());

	m_notebook->Bind(wxEVT_MENU, &CValueNotebook::OnEnablePage, this);
	m_notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &CValueNotebook::OnChangedPage, this);

	return m_notebook;
}

void CValueNotebook::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated)
{
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstСreated) {
		CValueNotebook::AddNotebookPage();
	}
}

void CValueNotebook::OnSelected(wxObject* wxobject)
{
}

void CValueNotebook::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	m_bInitialized = false;
	wxAuiNotebook *notebook = dynamic_cast<wxAuiNotebook *>(wxobject);
	if (notebook)
	{
	}
	UpdateWindow(notebook);
}

void CValueNotebook::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	wxAuiNotebook *notebook = dynamic_cast<wxAuiNotebook *>(wxobject);
	
	if (notebook) {
		notebook->SetWindowStyle(m_orientPage);
	}

	//turn on pages  
	wxCommandEvent cmdEvent(wxEVT_MENU, PAGE_ID);
	cmdEvent.SetEventObject(notebook);
	wxPostEvent(notebook, cmdEvent);
}

void CValueNotebook::Cleanup(wxObject* wxobject, IVisualHost *visualHost)
{
	wxAuiNotebook *notebook = dynamic_cast<wxAuiNotebook *>(wxobject);
	notebook->Unbind(wxEVT_MENU, &CValueNotebook::OnEnablePage, this);
	notebook->Unbind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &CValueNotebook::OnChangedPage, this);
	m_bInitialized = false;
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool CValueNotebook::LoadData(CMemoryReader &reader)
{
	m_orientPage = (wxAuiNotebookOption)reader.r_s32();
	return IValueWindow::LoadData(reader);
}

bool CValueNotebook::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_orientPage);
	return IValueWindow::SaveData(writer);
}

//**********************************************************************************
//*                                   Property                                     *
//**********************************************************************************

void CValueNotebook::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("orient_page", m_orientPage, true);
}

void CValueNotebook::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("orient_page", m_orientPage, true);
}