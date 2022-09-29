////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "title.h"
#include "controls/form.h"

wxIMPLEMENT_DYNAMIC_CLASS(CVisualEditorContextForm, wxPanel);

wxBEGIN_EVENT_TABLE(CVisualEditorContextForm, wxPanel)
EVT_PROJECT_LOADED(CVisualEditorContextForm::OnProjectLoaded)
EVT_PROJECT_SAVED(CVisualEditorContextForm::OnProjectSaved)
EVT_OBJECT_SELECTED(CVisualEditorContextForm::OnObjectSelected)
EVT_OBJECT_CREATED(CVisualEditorContextForm::OnObjectCreated)
EVT_OBJECT_REMOVED(CVisualEditorContextForm::OnObjectRemoved)
EVT_PROPERTY_MODIFIED(CVisualEditorContextForm::OnPropertyModified)
EVT_PROJECT_REFRESH(CVisualEditorContextForm::OnProjectRefresh)
EVT_CODE_GENERATION(CVisualEditorContextForm::OnCodeGeneration)
wxEND_EVENT_TABLE()

CVisualEditorContextForm::CVisualEditorContextForm() :
	wxPanel(), m_bReadOnly(false)
{
}

CVisualEditorContextForm::CVisualEditorContextForm(CDocument* document, CView* view, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_document(document), m_view(view), m_cmdProc(document->GetCommandProcessor()), m_valueForm(NULL),
	m_bReadOnly(false)
{
	CreateWideGui();
}

void CVisualEditorContextForm::CreateWideGui()
{
	wxWindow::Freeze();

	m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
	m_splitter->SetSashGravity(0.5);
	m_splitter->SetMinimumPaneSize(20); // Smalest size the

	wxBoxSizer* m_sizerMain = new wxBoxSizer(wxVERTICAL);
	m_sizerMain->Add(m_splitter, 1, wxEXPAND, 0);

	wxPanel* m_panelDesigner = new wxPanel(m_splitter, wxID_ANY);
	wxBoxSizer* designersizer = new wxBoxSizer(wxVERTICAL);

	wxASSERT(m_visualEditor);

	m_visualEditor = new CVisualEditorHost(this, m_panelDesigner);

	designersizer->Add(m_visualEditor, 1, wxEXPAND, 0);

	m_panelDesigner->SetSizer(designersizer);

	wxPanel* m_panelTree = new wxPanel(m_splitter, wxID_ANY);
	wxBoxSizer* treesizer = new wxBoxSizer(wxVERTICAL);

	m_objectTree = new CVisualEditorObjectTree(this, m_panelTree);
	m_objectTree->Create();

	treesizer->Add(m_objectTree, 1, wxEXPAND, 0);
	m_panelTree->SetSizer(treesizer);

	m_splitter->SplitVertically(m_panelDesigner, m_panelTree, -300);
	SetSizer(m_sizerMain);

	wxWindow::Thaw();
}

void CVisualEditorContextForm::ActivateObject()
{
	g_visualHostContext = this;
}

void CVisualEditorContextForm::DeactivateObject()
{
	g_visualHostContext = NULL;
}

#include "common/docInfo.h" 
#include "compiler/value.h"
#include "metadata/metaObjects/objects/object.h"
#include "metadata/metadata.h"
#include "metadata/metaObjectsDefines.h"

bool CVisualEditorContextForm::LoadForm()
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

	NotifyProjectLoaded();
	SelectObject(m_valueForm, true);

	// first create control 
	m_visualEditor->CreateVisualEditor();
	// then update control 
	m_visualEditor->UpdateVisualEditor();

	//refresh project
	NotifyProjectRefresh();
	return true;
}

bool CVisualEditorContextForm::SaveForm()
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

void CVisualEditorContextForm::TestForm()
{
	m_valueForm->ShowForm(m_document, true);
}

#include "frontend/mainFrame.h"
#include "frontend/objinspect/objinspect.h"

CVisualEditorContextForm::~CVisualEditorContextForm()
{
	m_visualEditor->Destroy();
	m_objectTree->Destroy();
	m_splitter->Destroy();

	if (m_valueForm != NULL) {
		m_valueForm->DecrRef();
	}
}

//********************************************************************
//*                           Events                                 *
//********************************************************************

void CVisualEditorContextForm::OnProjectLoaded(wxFrameEvent& event)
{
	// first create control 
	m_visualEditor->CreateVisualEditor();
	// then update control 
	m_visualEditor->UpdateVisualEditor();
}

void CVisualEditorContextForm::OnProjectSaved(wxFrameEvent& event)
{
}

void CVisualEditorContextForm::OnObjectSelected(wxFrameObjectEvent& event)
{
	/*// Get the IValueFrame from the event
	IValueFrame* obj = event.GetFrameObject();
	if (!obj) return;
	m_visualEditor->SetObjectSelect(obj);*/
}

void CVisualEditorContextForm::OnObjectCreated(wxFrameObjectEvent& event)
{
}

void CVisualEditorContextForm::OnObjectRemoved(wxFrameObjectEvent& event)
{
}

void CVisualEditorContextForm::OnPropertyModified(wxFramePropertyEvent& event)
{
	Property* prop = event.GetFrameProperty();
	wxASSERT(prop);
	if (prop->GetType() == PropertyType::PT_WXNAME
		&& !CVisualEditorContextForm::IsCorrectName(event.GetValue())) {
		event.Skip();
		return;
	}

	ModifyProperty(prop, event.GetValue());
}

void CVisualEditorContextForm::OnProjectRefresh(wxFrameEvent& event)
{
}

void CVisualEditorContextForm::OnCodeGeneration(wxFrameEventHandlerEvent& event)
{
	wxView* view = m_document->GetFirstView();
	if (!view->ProcessEvent(event))
		return;

	m_document->Modify(true);

	IMetaFormObject* formObject =
		dynamic_cast<IMetaFormObject*>(m_document->GetMetaObject());
	wxASSERT(formObject);
	formObject->SaveFormData(m_valueForm);
}