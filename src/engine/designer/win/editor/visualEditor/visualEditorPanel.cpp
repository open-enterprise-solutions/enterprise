////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "titleFrame.h"
#include "frontend/visualView/ctrl/form.h"

wxIMPLEMENT_DYNAMIC_CLASS(CVisualEditorNotebook::CVisualEditor, wxPanel);

CVisualEditorNotebook::CVisualEditor::CVisualEditor() :
	wxPanel(), m_bReadOnly(false)
{
}

CVisualEditorNotebook::CVisualEditor::CVisualEditor(CMetaDocument* document, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_document(document), m_cmdProc(new CCommandProcessor()), m_valueForm(nullptr),
	m_bReadOnly(false)
{
	CreateWideGui();
}

void CVisualEditorNotebook::CVisualEditor::CreateWideGui()
{
	wxWindow::Freeze();

	m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_THIN_SASH);
	m_splitter->SetSashGravity(0.5);
	m_splitter->SetMinimumPaneSize(20); // Smalest size the

	wxBoxSizer* m_sizerMain = new wxBoxSizer(wxVERTICAL);
	m_sizerMain->Add(m_splitter, 1, wxEXPAND, 0);

	wxPanel* panelDesigner = new wxPanel(m_splitter, wxID_ANY);
	wxBoxSizer* designerSizer = new wxBoxSizer(wxVERTICAL);

	wxASSERT(m_visualEditor);

	m_visualEditor = new CVisualEditorHost(this, panelDesigner);

	designerSizer->Add(m_visualEditor, 1, wxEXPAND, 0);

	panelDesigner->SetSizer(designerSizer);

	wxPanel* panelTree = new wxPanel(m_splitter, wxID_ANY);
	wxBoxSizer* treesizer = new wxBoxSizer(wxVERTICAL);

	m_objectTree = new CVisualEditorObjectTree(this, panelTree);
	m_objectTree->Create();

	treesizer->Add(CPanelTitle::CreateTitle(m_objectTree, _("Tree elements")), 1, wxEXPAND, 0);
	panelTree->SetSizer(treesizer);

	m_splitter->SplitVertically(panelDesigner, panelTree, -300);
	SetSizer(m_sizerMain);

	wxWindow::Thaw();
}

#include "frontend/docView/docView.h" 

#include "backend/metaCollection/partial/object.h"
#include "backend/metadataConfiguration.h"

bool CVisualEditorNotebook::CVisualEditor::LoadForm()
{
	if (m_document == nullptr)
		return false;

	IMetaObjectForm* metaForm = m_document->GetMetaObject()->ConvertToType<IMetaObjectForm>();

	if (metaForm == nullptr)
		return false;

	IMetaData* metaData = metaForm->GetMetaData();
	wxASSERT(metaData);

	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->FindCompileModule(metaForm, m_valueForm)) {
		m_valueForm = new CValueForm(nullptr, metaForm);
		if (!metaForm->LoadFormData(m_valueForm)) {
			wxDELETE(m_valueForm);
			return false;
		}
	}

	m_valueForm->IncrRef();
	m_visualEditor->Freeze();
	
	NotifyProjectLoaded();
	SelectObject(m_valueForm, true);

	// first create control 
	m_visualEditor->CreateVisualEditor();
	
	// then update control 
	m_visualEditor->UpdateVisualEditor();
	
	//refresh project
	NotifyProjectRefresh();

	m_visualEditor->Thaw();
	return true;
}

bool CVisualEditorNotebook::CVisualEditor::SaveForm()
{
	IMetaObjectForm* formMetaObject = m_document->ConvertMetaObjectToType<IMetaObjectForm>();

	// Create a std::string and copy your document data in to the string    
	if (formMetaObject != nullptr) {
		formMetaObject->SaveFormData(m_valueForm);
	}

	NotifyProjectSaved();
	return true;
}

#include "frontend/visualView/visualHost.h"

void CVisualEditorNotebook::CVisualEditor::TestForm()
{
	m_valueForm->ShowForm(m_document, true);
}

CVisualEditorNotebook::CVisualEditor::~CVisualEditor()
{
	m_visualEditor->Destroy();
	m_objectTree->Destroy();
	m_splitter->Destroy();

	if (m_valueForm != nullptr) 
		m_valueForm->DecrRef();
	wxDELETE(m_cmdProc);
}