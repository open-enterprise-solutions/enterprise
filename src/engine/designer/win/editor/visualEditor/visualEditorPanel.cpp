////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "titleFrame.h"
#include "frontend/visualView/ctrl/form.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibVisualEditorNotebook::ibVisualEditor, wxPanel);

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditor() :
	wxPanel(), m_visualEditor(nullptr)
{
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditor(ibMetaDocument* document, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_document(document), m_visualEditor(nullptr), m_cmdProc(new ibCommandProcessor()), m_valueForm(nullptr)
{
	CreateWideGui();
}

void ibVisualEditorNotebook::ibVisualEditor::CreateWideGui()
{
	wxWindow::Freeze();

	m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_THIN_SASH);
	m_splitter->SetSashGravity(0.5);
	m_splitter->SetMinimumPaneSize(20); // Smalest size the

	wxBoxSizer* sizerMain = new wxBoxSizer(wxVERTICAL);
	sizerMain->Add(m_splitter, 1, wxEXPAND, 0);

	wxASSERT(m_visualEditor == nullptr);

	m_visualEditor = new ibVisualEditorHost(this, m_splitter);
	m_objectTree = new ibVisualEditorObjectTree(this, m_splitter);

	m_splitter->SplitHorizontally(m_visualEditor, 
		ibPanelTitle::CreateTitle(m_objectTree, _("Tree elements")), 200);

	SetSizer(sizerMain);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	wxWindow::Thaw();
}

#include "frontend/docView/docView.h" 
#include "frontend/mainFrame/mainFrame.h"

void ibVisualEditorNotebook::ibVisualEditor::ActivateEditor()
{
	objectInspector->SelectObject(GetSelectedObject());
	SetFocus();
}

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataConfiguration.h"

bool ibVisualEditorNotebook::ibVisualEditor::LoadForm()
{
	if (m_document == nullptr)
		return false;

	const ibValueMetaObjectFormBase* creator = m_document->GetMetaObject()->ConvertToType<ibValueMetaObjectFormBase>();

	if (creator == nullptr)
		return false;

	const ibMetaData* metaData = creator->GetMetaData();
	wxASSERT(metaData);

	auto* cc = metaData->GetCompileCache();
	if (!cc || !cc->FindCompileModule(creator, m_valueForm)) {
		m_valueForm = new ibValueForm(creator, nullptr);
		if (!creator->LoadFormData(m_valueForm)) {
			wxDELETE(m_valueForm);
			return false;
		}
	}

	m_valueForm->IncrRef();
	m_visualEditor->Freeze();
	
	NotifyEditorLoaded();

	// first create control 
	m_visualEditor->CreateVisualHost();
		
	//refresh project
	NotifyEditorRefresh();

	m_visualEditor->Thaw();
	return true;
}

bool ibVisualEditorNotebook::ibVisualEditor::SaveForm()
{
	ibValueMetaObjectFormBase* creator = m_document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>();

	// Create a std::string and copy your document data in to the string    
	if (creator != nullptr) {
		creator->SaveFormData(m_valueForm);
	}

	NotifyEditorSaved();
	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::TestForm()
{
	m_valueForm->ShowForm(m_document, false);
}

ibVisualEditorNotebook::ibVisualEditor::~ibVisualEditor()
{
	m_visualEditor->Destroy();
	m_objectTree->Destroy();
	m_splitter->Destroy();

	if (m_valueForm != nullptr) 
		m_valueForm->DecrRef();
	
	wxDELETE(m_cmdProc);
}