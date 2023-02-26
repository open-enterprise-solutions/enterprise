////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "title.h"
#include "controls/form.h"

wxIMPLEMENT_DYNAMIC_CLASS(CVisualEditorNotebook::CVisualEditorCtrl, wxPanel);

CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorCtrl() :
	wxPanel(), m_bReadOnly(false)
{
}

CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorCtrl(CDocument* document, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_document(document), m_cmdProc(new CCommandProcessor()), m_valueForm(NULL),
	m_bReadOnly(false)
{
	CreateWideGui();
}

void CVisualEditorNotebook::CVisualEditorCtrl::CreateWideGui()
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

	treesizer->Add(CTitle::CreateTitle(m_objectTree, _("Tree elements")), 1, wxEXPAND, 0);
	panelTree->SetSizer(treesizer);

	m_splitter->SplitVertically(panelDesigner, panelTree, -300);
	SetSizer(m_sizerMain);

	wxWindow::Thaw();
}

#include "frontend/docView/docView.h" 
#include "core/compiler/value.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metadata.h"
#include "core/metadata/metaObjectsDefines.h"

bool CVisualEditorNotebook::CVisualEditorCtrl::LoadForm()
{
	if (m_document == NULL)
		return false;

	IMetaObject* formMetaObject = m_document->GetMetaObject();

	if (formMetaObject == NULL)
		return false;

	IMetadata* metaData = formMetaObject->GetMetadata();
	wxASSERT(metaData);

	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (moduleManager->FindCompileModule(formMetaObject, m_valueForm)) {
		m_valueForm->IncrRef();
	}
	else {
		return false;
	}

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

bool CVisualEditorNotebook::CVisualEditorCtrl::SaveForm()
{
	IMetaFormObject* formMetaObject =
		dynamic_cast<IMetaFormObject*>(m_document->GetMetaObject());

	// Create a std::string and copy your document data in to the string    
	if (formMetaObject != NULL) {
		formMetaObject->SaveFormData(m_valueForm);
	}

	NotifyProjectSaved();
	return true;
}

#include "visualHost.h"

void CVisualEditorNotebook::CVisualEditorCtrl::TestForm()
{
	m_valueForm->ShowForm(m_document, true);
}

#include "frontend/mainFrame.h"
#include "frontend/objinspect/objinspect.h"

CVisualEditorNotebook::CVisualEditorCtrl::~CVisualEditorCtrl()
{
	m_visualEditor->Destroy();
	m_objectTree->Destroy();
	m_splitter->Destroy();

	if (m_valueForm != NULL)
		m_valueForm->DecrRef();

	delete m_cmdProc;
}