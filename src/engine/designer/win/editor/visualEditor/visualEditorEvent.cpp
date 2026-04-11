////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "backend/metaCollection/metaFormObject.h"

//////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::NotifyEditorLoaded()
{
	m_objectTree->OnEditorLoaded();
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyEditorSaved()
{
	ibValueMetaObjectFormBase* creator = m_document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>();
	wxASSERT(creator);
	// Create a std::string and copy your document data in to the string
	if (creator != nullptr) creator->SaveFormData(m_valueForm);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyEditorRefresh()
{
	m_objectTree->OnEditorRefresh();
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyObjectCreated(ibValueFrame* obj)
{
	m_objectTree->OnObjectCreated(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyObjectSelected(ibValueFrame* obj, bool force)
{
	m_objectTree->OnObjectSelected(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyObjectExpanded(ibValueFrame* obj)
{
	m_objectTree->OnObjectExpanded(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyObjectRemoved(ibValueFrame* obj)
{
	m_objectTree->OnObjectRemoved(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyPropertyModified(ibProperty* prop)
{
	m_objectTree->OnPropertyModified(prop);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyEventModified(ibEvent* event)
{
}

//////////////////////////////////////////////////////////////////////////////////////

wxObject* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::Create(ibValueFrame* control, wxWindow* wxparent)
{
	return control->Create(wxparent, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnCreated(ibValueFrame* control, wxObject* obj, wxWindow* wndParent, bool firstCreated)
{
	control->OnCreated(obj, wndParent, this, firstCreated);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::Update(ibValueFrame* control, wxObject* obj)
{
	control->Update(obj, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnUpdated(ibValueFrame* control, wxObject* obj, wxWindow* wndParent)
{
	control->OnUpdated(obj, wndParent, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::Cleanup(ibValueFrame* control, wxObject* obj)
{
	control->Cleanup(obj, this);
}
