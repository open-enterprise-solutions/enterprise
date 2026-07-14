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
	m_attributeTree->OnEditorLoaded();
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
	m_attributeTree->OnEditorRefresh();

	WireTableboxDrops(m_valueForm);   // (re)attach per-grid drop targets after the widgets rebuild
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyObjectCreated(ibValueFrame* obj)
{
	m_objectTree->OnObjectCreated(obj);
	// A just-dropped source control auto-provisions its form attribute (ibValueControl::AutoBindNewSource),
	// so the attribute tree must rebuild too — the object tree alone would miss the new attribute.
	m_attributeTree->OnEditorRefresh();
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
	// The attribute tree tracks property changes the SAME way the control tree does: a Type/source edit
	// re-evaluates the affected attribute's [+] composition picker in place (was only refreshed on reopen).
	m_attributeTree->OnPropertyModified(prop);
}

void ibVisualEditorNotebook::ibVisualEditor::NotifyEventModified(ibEvent* event)
{
}

//////////////////////////////////////////////////////////////////////////////////////

// Route EVERY lifecycle stage through the control's *WithLayers wrapper — same as
// the runtime host. A plain control forwards to its plain method (no change); a
// composite control (tableBox) builds/updates/tears down its chrome layer (the
// command-bar toolbar) around its inner widget, so the designer shows it too.
wxObject* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::Create(ibValueFrame* control, wxWindow* wxparent)
{
	return control->CreateWithLayers(wxparent, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnCreated(ibValueFrame* control, wxObject* obj, wxWindow* wndParent, bool firstCreated)
{
	control->OnCreatedWithLayers(obj, wndParent, this, firstCreated);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::Update(ibValueFrame* control, wxObject* obj)
{
	control->UpdateWithLayers(obj, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::OnUpdated(ibValueFrame* control, wxObject* obj, wxWindow* wndParent)
{
	control->OnUpdatedWithLayers(obj, wndParent, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost::Cleanup(ibValueFrame* control, wxObject* obj)
{
	control->CleanupWithLayers(obj, this);
}
