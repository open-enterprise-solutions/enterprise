////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"   // ibFormDragItem — the polymorphic drop kinds (ApplyDrop enacts the drop)

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "backend/backend_exception.h"   // ibBackendException — caught explicitly; it is NOT a std::exception
#include "backend/propertyManager/propertyManager.h"
#include "backend/system/value/valueJob.h"   // g_valueScheduleCLSID — a dropped schedule builds as static text
#include "backend/system/value/valueDataComposition.h"  // g_valueDataCompositionCLSID — drops as a gridbox
#include "backend/system/value/valueSpreadsheet.h"       // g_valueSpreadsheetCLSID — so does a document

#include "frontend/visualView/ctrl/formAttribute.h"   // ibFormAttributeValue (form attribute add command)
#include "frontend/visualView/ctrl/formCommand.h"      // ibFormCommandValue (ibEditorSelection command ctor upcast)
#include "frontend/visualView/ctrl/tableBox.h"         // g_controlTableBoxCLSID — a drop onto a tablebox appends a column
#include "backend/query/queryColumn.h"                 // ibBackendSourceColumn::GetName (name a dropped control after its field)
#include "backend/srcDataObject.h"                     // ibSourceExplorer — resolve a dropped path's leaf type -> control class
#include "frontend/visualView/layers/commandBar.h"     // ibValueCommandBar / ibValueCommandBarItem — a command dropped on a tablebox becomes a bar item
#include "frontend/visualView/ctrl/widgets.h"           // ibValueButton — a command dropped on the form becomes a bound Button control
#include "backend/metaData.h"                           // ibMetaData::FindAnyObjectByFilter — name the button after the command
#include <wx/app.h>                                     // wxTheApp->CallAfter — defer a grid drop out of the OS drop callback

void ibVisualEditorNotebook::ibVisualEditor::Execute(ibVisualEditorCmd* cmd)
{
	if (m_cmdProc != nullptr)
		m_cmdProc->Execute(cmd);

	NotifyEditorSaved();
}

///////////////////////////////////////////////////////////////////////////////
// Commands
///////////////////////////////////////////////////////////////////////////////

/** Command for expanding an object in the object tree */

class ibVisualEditorExpandObjectCmd :
	public ibVisualEditorCmd {
public:
	ibVisualEditorExpandObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, bool expand);
protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibValueFrame* m_object = nullptr;
	bool m_expand;
};

/**
* Command for inserting an object into the tree.
*/

class ibVisualEditorInsertObjectCmd :
	public ibVisualEditorCmd {
public:
	ibVisualEditorInsertObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, ibValueFrame* parent, int pos = -1, bool firstCreated = true);
protected:

	void GenerateId();
	void ResetId();

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:

	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibValueFrame* m_parent = nullptr;
	ibValuePtr<ibValueFrame> m_object; // owns the in-flight control (RAII +1/-1)
	int m_pos;
	ibValueFrame* m_oldSelected;
	bool m_firstCreated;
};

/**
* Command for deleting an object.
*/

class ibVisualEditorRemoveObjectCmd :
	public ibVisualEditorCmd, public wxEvtHandler {
public:

	ibVisualEditorRemoveObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object);
	~ibVisualEditorRemoveObjectCmd();

protected:

	void GenerateId();
	void ResetId();

	void RemoveObject();

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:

	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibValueFrame* m_parent = nullptr;
	ibValuePtr<ibValueFrame> m_object; // owns the in-flight control (RAII +1/-1)
	int m_oldPos;
	ibValueFrame* m_oldSelected = nullptr;
};

/**
* Command for modifying a property.
*/

class ibVisualEditorModifyPropertyCmd :
	public ibVisualEditorCmd {
public:

	ibVisualEditorModifyPropertyCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibProperty* m_property;
	wxVariant m_oldValue, m_newValue;
};

/**
* ibVisualEditorCmd for modifying an event
*/

class ibVisualEditorModifyEventCmd :
	public ibVisualEditorCmd {
public:
	ibVisualEditorModifyEventCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibEvent* m_event = nullptr;
	wxVariant m_oldValue, m_newValue;
};

/**
* Command for moving an object to another position.
*/

class ibVisualEditorShiftChildCmd :
	public ibVisualEditorCmd
{
public:

	ibVisualEditorShiftChildCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, int pos);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibValueFrame* m_object = nullptr;
	int m_oldPos, m_newPos;
};

/**
* ibVisualEditorCutObjectCmd, besides removing the object from the tree, makes sure
* to clear the "clipboard" reference when the change is undone.
*/

class ibVisualEditorCutObjectCmd :
	public ibVisualEditorCmd, public wxEvtHandler {
public:

	ibVisualEditorCutObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, bool force);
	~ibVisualEditorCutObjectCmd();

protected:

	void GenerateId();
	void ResetId();

	void RemoveObject();

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:

	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	// needed to query/modify the "clipboard" object
	ibValueFrame* m_parent = nullptr;
	ibValuePtr<ibValueFrame> m_object; // owns the in-flight control (RAII +1/-1)
	int m_oldPos;
	ibValueFrame* m_oldSelected = nullptr;

	bool m_needEvent;
};

/**
* Command for inserting an attribute into the form. The attribute is created DETACHED
* (ibValueForm::MakeAttribute); the command attaches it to the form on execute / redo
* and detaches it (reclaiming the property) on undo, so the exact entry the user
* configured survives in the undo stack.
*/

class ibVisualEditorInsertAttributeCmd :
	public ibVisualEditorCmd {
public:

	ibVisualEditorInsertAttributeCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueForm* form, ibValuePtr<ibFormAttributeValue> holder);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibValueForm* m_form;
	ibFormAttributeValue* m_entry = nullptr;            // valid while attached to the form
	ibValuePtr<ibFormAttributeValue> m_detached;        // owns it while detached (undone)
};

/**
* Command to remove a form attribute. Mirror of the insert command: on execute it detaches
* the attribute (keeping it for redo); on undo it re-attaches it.
*/

class ibVisualEditorRemoveAttributeCmd :
	public ibVisualEditorCmd {
public:

	ibVisualEditorRemoveAttributeCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueForm* form, ibFormAttributeValue* entry);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibValueForm* m_form;
	ibFormAttributeValue* m_entry = nullptr;            // valid while attached to the form
	ibValuePtr<ibFormAttributeValue> m_detached;        // owns it while detached (removed)
};

///////////////////////////////////////////////////////////////////////////////
// Command implementations
///////////////////////////////////////////////////////////////////////////////

ibVisualEditorExpandObjectCmd::ibVisualEditorExpandObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, bool expand) : m_visualEditor(visualEditor),
m_object(object), m_expand(expand)
{
}

void ibVisualEditorExpandObjectCmd::DoExecute()
{
	m_object->SetExpanded(m_expand);
}

void ibVisualEditorExpandObjectCmd::DoRestore()
{
	m_object->SetExpanded(!m_expand);
}

ibVisualEditorInsertObjectCmd::ibVisualEditorInsertObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, ibValueFrame* parent, int pos, bool firstCreated) : m_visualEditor(visualEditor),
m_parent(parent), m_object(object), m_pos(pos), m_firstCreated(firstCreated)
{
	// m_object is an owning ibValuePtr: its member-init from `object` takes the
	// reference that keeps the control alive while it lives detached in the undo
	// stack (the RemoveChild below drops the parent's owning handle). The handle
	// is released automatically when the command is destroyed.
	m_oldSelected = visualEditor->GetSelectedObject();

	if (m_parent) {
		m_parent->RemoveChild(m_object);
		m_object->SetParent(nullptr);
	}

	ResetId();
}

void ibVisualEditorInsertObjectCmd::GenerateId()
{
	std::function<void(ibValueFrame*)> reset = [&reset](ibValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() == 0) {
			object->GenerateNewID();
		}
		};
	reset(m_object);
}

void ibVisualEditorInsertObjectCmd::ResetId()
{
	std::function<void(ibValueFrame*)> reset = [&reset](ibValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() != 0) {
			object->SetControlID(0);
		}
		};
	reset(m_object);
}

void ibVisualEditorInsertObjectCmd::DoExecute()
{
	m_visualEditor->Modify(true);

	if (m_parent != nullptr) {
		m_parent->AddChild(m_object);
		m_object->SetParent(m_parent);
	}

	GenerateId();

	if (m_pos >= 0) {
		m_parent->ChangeChildPosition(m_object, m_pos);
	}

	ibValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	//create control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	wxASSERT(visualEditor);
	visualEditor->CreateControl(m_object, nullptr, m_firstCreated);

	ibValueForm* valueForm = visualEditor->GetValueForm();
	if (valueForm != nullptr) valueForm->InvalidateNames();

	//select object
	m_visualEditor->SelectObject(obj, false, false);
}

void ibVisualEditorInsertObjectCmd::DoRestore()
{
	m_visualEditor->Modify(true);

	//remove control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	visualEditor->RemoveControl(m_object);

	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);

	ResetId();

	ibValueForm* valueForm = visualEditor->GetValueForm();
	if (valueForm != nullptr) valueForm->InvalidateNames();

	m_visualEditor->SelectObject(m_oldSelected);
}

//-----------------------------------------------------------------------------

ibVisualEditorRemoveObjectCmd::ibVisualEditorRemoveObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object) : m_visualEditor(visualEditor)
{
	m_object = object;
	m_parent = object->GetParent();
	m_oldPos = m_parent->GetChildPosition(object);
	m_oldSelected = visualEditor->GetSelectedObject();
	// m_object is an owning ibValuePtr: assigning `object` takes the reference that
	// keeps the control alive while it lives detached in the undo stack; released
	// automatically when the command is destroyed.
}

ibVisualEditorRemoveObjectCmd::~ibVisualEditorRemoveObjectCmd()
{
}

void ibVisualEditorRemoveObjectCmd::GenerateId()
{
	std::function<void(ibValueFrame*)> reset = [&reset](ibValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() == 0) {
			object->GenerateNewID();
		}
		};
	reset(m_object);
}

void ibVisualEditorRemoveObjectCmd::ResetId()
{
	std::function<void(ibValueFrame*)> reset = [&reset](ibValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() != 0) {
			object->SetControlID(0);
		}
		};
	reset(m_object);
}

void ibVisualEditorRemoveObjectCmd::RemoveObject()
{
	//remove control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	visualEditor->RemoveControl(m_object);

	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);

	ResetId();

	ibValueForm* valueForm = visualEditor->GetValueForm();
	if (valueForm != nullptr) valueForm->InvalidateNames();
}

void ibVisualEditorRemoveObjectCmd::DoExecute()
{
	m_visualEditor->Modify(true);

	ibValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	if (m_object->GetParent() != nullptr) {
		m_parent->RemoveChild(m_object);
		m_object->SetParent(nullptr);
	}

	m_visualEditor->DetermineObjectToSelect(m_parent, m_oldPos);

	wxEvtHandler::CallAfter(&ibVisualEditorRemoveObjectCmd::RemoveObject);
}

void ibVisualEditorRemoveObjectCmd::DoRestore()
{
	m_visualEditor->Modify(true);

	if (m_object->GetParent() == nullptr) {
		m_parent->AddChild(m_object);
		m_object->SetParent(m_parent);
	}

	ibValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	GenerateId();

	// restore the position
	m_parent->ChangeChildPosition(m_object, m_oldPos);
	m_visualEditor->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	visualEditor->CreateControl(m_object);

	ibValueForm* valueForm = visualEditor->GetValueForm();
	if (valueForm != nullptr) valueForm->InvalidateNames();
}

//-----------------------------------------------------------------------------

ibVisualEditorModifyPropertyCmd::ibVisualEditorModifyPropertyCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) : m_visualEditor(visualEditor),
m_property(prop), m_oldValue(oldValue), m_newValue(newValue)
{
}

void ibVisualEditorModifyPropertyCmd::DoExecute()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control =
		dynamic_cast<ibValueFrame*>(m_property->GetPropertyObject());

	m_property->SetValue(m_newValue);
	m_visualEditor->Modify(true);

	if (control != nullptr) {
		if (g_controlFormCLSID == control->GetClassType()) {
			visualEditor->UpdateVisualHost();
		}
		else {
			visualEditor->UpdateControl(control);
		}
	}
	else {
		// Non-control owner (a form ATTRIBUTE / its held value / a command-bar LAYER): rebuild the WHOLE
		// editor — its RefreshEditor fans out NotifyEditorRefresh, which re-reads the attribute tree too.
		// (RefreshEditor coalesces re-entrant calls itself, so the parallel objinspect child→parent bubble
		// firing the same refresh is a no-op — no double rebuild.)
		m_visualEditor->RefreshEditor();
		// RefreshEditor drops the tree selection. Re-reveal the CURRENTLY selected element (what the inspector shows —
		// the attribute FACADE, not m_property's real owner which is the hidden inner attribute) and FORCE its property
		// list to recompute: a Type change (e.g. -> DynamicList) regenerates the facade's sub-properties, so the
		// inspector must rebuild even though the selection is unchanged — no more "reselect the attribute by hand".
		// REFRESH, not show: a no-op when the panel is hidden (nothing shown -> nothing to recompute).
		m_visualEditor->RefreshCurrentElement();
	}
}

void ibVisualEditorModifyPropertyCmd::DoRestore()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control =
		dynamic_cast<ibValueFrame*>(m_property->GetPropertyObject());

	m_property->SetValue(m_oldValue);

	if (control != nullptr) {
		if (g_controlFormCLSID == control->GetClassType()) {
			visualEditor->UpdateVisualHost();
		}
		else {
			visualEditor->UpdateControl(control);
		}
	}
	else {
		m_visualEditor->RefreshEditor();   // attribute owner — rebuild the whole editor (attr tree included)
	}

	m_visualEditor->Modify(true);
	// Undo restored the value — REFRESH the current selection's properties in place (a restored Type must rebuild the
	// facade's sub-properties), same as DoExecute. No-op when the panel is hidden; keeps the inspector where it was.
	m_visualEditor->RefreshCurrentElement();
}

//-----------------------------------------------------------------------------

ibVisualEditorModifyEventCmd::ibVisualEditorModifyEventCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue) : m_visualEditor(visualEditor),
m_event(event), m_oldValue(oldValue), m_newValue(newValue)
{
}

void ibVisualEditorModifyEventCmd::DoExecute()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control =
		dynamic_cast<ibValueFrame*>(m_event->GetPropertyObject());

	m_event->SetValue(m_newValue);
	m_visualEditor->Modify(true);

	if (control != nullptr) {
		if (g_controlFormCLSID == control->GetClassType()) {
			visualEditor->UpdateVisualHost();
		}
		else {
			visualEditor->UpdateControl(control);
		}
	}
	else {
		visualEditor->UpdateVisualHost();
	}
}

void ibVisualEditorModifyEventCmd::DoRestore()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control =
		dynamic_cast<ibValueFrame*>(m_event->GetPropertyObject());

	m_event->SetValue(m_oldValue);
	m_visualEditor->Modify(true);

	if (control != nullptr) {
		if (g_controlFormCLSID == control->GetClassType()) {
			visualEditor->UpdateVisualHost();
		}
		else {
			visualEditor->UpdateControl(control);
		}
	}
	else {
		visualEditor->UpdateVisualHost();
	}
}

//-----------------------------------------------------------------------------

ibVisualEditorShiftChildCmd::ibVisualEditorShiftChildCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, int pos) : m_visualEditor(visualEditor)
{
	m_object = object;
	ibValueFrame* parent = object->GetParent();

	assert(parent);

	m_oldPos = parent->GetChildPosition(object);
	m_newPos = pos;
}

void ibVisualEditorShiftChildCmd::DoExecute()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	if (m_oldPos != m_newPos) {
		ibValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object, m_newPos);

		visualEditor->UpdateControl(m_object);
	}

	m_visualEditor->Modify(true);
}

void ibVisualEditorShiftChildCmd::DoRestore()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	if (m_oldPos != m_newPos) {
		ibValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object, m_oldPos);

		visualEditor->UpdateControl(m_object);
	}

	m_visualEditor->Modify(true);
}

//-----------------------------------------------------------------------------

ibVisualEditorCutObjectCmd::ibVisualEditorCutObjectCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueFrame* object, bool force) : m_visualEditor(visualEditor), m_needEvent(!force)
{
	m_object = object;
	m_parent = object->GetParent();
	m_oldPos = m_parent->GetChildPosition(object);
	m_oldSelected = visualEditor->GetSelectedObject();
	// m_object is an owning ibValuePtr: assigning `object` takes the reference that
	// keeps the control alive while it lives detached (clipboard / undo stack);
	// released automatically when the command is destroyed.
}

ibVisualEditorCutObjectCmd::~ibVisualEditorCutObjectCmd()
{
}

void ibVisualEditorCutObjectCmd::GenerateId()
{
	std::function<void(ibValueFrame*)> reset = [&reset](ibValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() == 0) {
			object->GenerateNewID();
		}
		};
	reset(m_object);
}

void ibVisualEditorCutObjectCmd::ResetId()
{
	std::function<void(ibValueFrame*)> reset = [&reset](ibValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() != 0) {
			object->SetControlID(0);
		}
		};
	reset(m_object);
}

void ibVisualEditorCutObjectCmd::RemoveObject()
{
	//remove control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	visualEditor->RemoveControl(m_object);

	//remove control in visual editor
	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);
}

void ibVisualEditorCutObjectCmd::DoExecute()
{
	m_visualEditor->Modify(true);

	if (!m_needEvent) {
		//remove control in visual editor
		ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
			m_visualEditor->GetVisualEditor();
		visualEditor->RemoveControl(m_object);
	}

	ibValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	//remove control in visual editor
	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);

	//determine object to select
	m_visualEditor->DetermineObjectToSelect(m_parent, m_oldPos);

	//reset id, guid
	ResetId();

	if (m_needEvent) {
		wxEvtHandler::CallAfter(&ibVisualEditorCutObjectCmd::RemoveObject);
	}
}

void ibVisualEditorCutObjectCmd::DoRestore()
{
	m_visualEditor->Modify(true);

	// move the object back to where it was
	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	ibValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	//generate new id
	GenerateId();

	//change child position
	m_parent->ChangeChildPosition(m_object, m_oldPos);

	// restore the clipboard
	//m_visualEditor->SetClipboardObject(nullptr);
	m_visualEditor->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	visualEditor->CreateControl(m_object, m_parent);
}

//-----------------------------------------------------------------------------

ibVisualEditorInsertAttributeCmd::ibVisualEditorInsertAttributeCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueForm* form, ibValuePtr<ibFormAttributeValue> holder) : m_visualEditor(visualEditor),
m_form(form), m_detached(std::move(holder))
{
}

void ibVisualEditorInsertAttributeCmd::DoExecute()
{
	m_visualEditor->Modify(true);

	//attach the held entry to the form (execute + redo)
	m_entry = m_form->AttachAttribute(std::move(m_detached));

	m_visualEditor->RefreshEditor();
	if (m_entry != nullptr)
		m_visualEditor->SetCurrentElement(m_entry);   // reveal the new attribute through the ONE selector (soft)
}

void ibVisualEditorInsertAttributeCmd::DoRestore()
{
	m_visualEditor->Modify(true);

	//detach, taking ownership back so redo re-attaches the same entry
	m_detached = m_form->DetachAttribute(m_entry);
	m_entry = nullptr;

	m_visualEditor->RefreshEditor();
}

//-----------------------------------------------------------------------------

ibVisualEditorRemoveAttributeCmd::ibVisualEditorRemoveAttributeCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibValueForm* form, ibFormAttributeValue* entry) : m_visualEditor(visualEditor),
m_form(form), m_entry(entry)
{
}

void ibVisualEditorRemoveAttributeCmd::DoExecute()
{
	m_visualEditor->Modify(true);

	//detach the entry, keeping it alive for redo
	m_detached = m_form->DetachAttribute(m_entry);

	m_visualEditor->RefreshEditor();
}

void ibVisualEditorRemoveAttributeCmd::DoRestore()
{
	m_visualEditor->Modify(true);

	//re-attach the same entry on undo
	m_entry = m_form->AttachAttribute(std::move(m_detached));

	m_visualEditor->RefreshEditor();
	if (m_entry != nullptr)
		m_visualEditor->SetCurrentElement(m_entry);   // undo re-attaches: reveal it through the ONE selector (soft)
}

//-----------------------------------------------------------------------------

ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::CreateObject(const wxString& name)
{
	ibValueFrame* obj = nullptr;
	wxASSERT(m_valueForm);
	try
	{
		//LogDebug("[ibApplicationData::CreateObject] New " + name );
		ibValueFrame* old_selected = GetSelectedObject();
		ibValueFrame* parent = old_selected;

		if (parent)
		{
			bool created = false;

			// For convenience: if the object cannot be created under the selected object,
			// try the selected object's parent, and keep walking up until we either
			// create the object or run out of parents.

			while (parent && !created)
			{
				// the object is inserted right after the selected object
				obj = m_valueForm->CreateObject(_STDSTR(name), parent);

				if (obj)
				{
					int pos = CalcPositionOfInsertion(GetSelectedObject(), parent);

					Execute(new ibVisualEditorInsertObjectCmd(this, obj, parent, pos));
					created = true;
				}
				else
				{
					// keep trying with the parent — but careful, the parent cannot be an item!
					parent = parent->GetParent();

					while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
						parent = parent->GetParent();
				}
			}
		}

		// Select the object; if it is an item, select the object it contains.
		// (Does it ever make sense to have an item under an item?)
		while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
			obj = (obj->GetChildCount() > 0 ? obj->GetChild(0) : nullptr);

		NotifyObjectCreated(obj);

		if (obj)
		{
			SelectObject(obj, true, true);
		}
		else
		{
			SelectObject(old_selected, true, true);
		}
	}
	// THE PROJECT'S OWN EXCEPTION FIRST — ibBackendException does NOT derive from std::exception,
	// so without this branch it flies straight past both handlers and out of the event handler.
	// Creating a control walks the metadata (property manager, attribute / command values), which
	// is exactly the code that raises it.
	catch (const ibBackendException& err)
	{
		wxLogError(wxT("%s"), err.GetErrorDescription());
	}
	catch (const std::exception& ex)
	{
		wxLogError(wxT("%s"), wxString::FromUTF8(ex.what()));
	}

	return obj;
}

// Bind a just-created control to a source path — the drop counterpart of BuildForm's per-control
// SetSource. Goes through the COMMON source factory: SetDefaultSourceType is a non-virtual helper over the
// mutable GetSourceDesc (twin of SetDefaultMetaType over GetTypeDesc), so the designer needs no concrete-
// control header and the virtual GetSourceDesc dispatch keeps the resolve inside frontend.dll.
static void SetBoundSource(ibValueFrame* control, const ibSourceDescription& desc)
{
	ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(control);
	if (factory == nullptr)
		return;
	factory->SetDefaultSourceType(desc);

	// Give the dropped control a NAME (identifier), like BuildForm, so it isn't a bare "Textctrl1".
	// A NESTED binding (a NON-main head attribute + a deeper leaf, e.g. Catalog1.Section) CONCATENATES
	// its segments — the dotted path becomes one joined identifier ("Catalog1Section"), never a literal
	// dot. A direct field on the (implicit) MAIN attribute keeps just the field name; a BARE attribute
	// (no leaf column) falls back to the attribute name. The TITLE (form label) is NOT assigned here —
	// the control DERIVES it on render from the binding (GetControlTitle: Title -> field synonym ->
	// attribute Synonym/Name).
	wxString name;
	ibBackendFormAttributeValue* head = !desc.IsOk() ? nullptr : factory->FindSourceHolder(desc.GetFirst());
	if (head != nullptr && !head->IsMain())
		name = head->GetName();
	if (const ibBackendSourceColumn* leaf = factory->GetSourceAttributeObject())
		name += leaf->GetName();
	if (name.IsEmpty() && head != nullptr)
		name = head->GetName();

	if (!name.IsEmpty()) {
		control->SetControlName(name);
		// Make it UNIQUE on the form through the SAME conflict-resolver a normal create / paste runs
		// (frame.cpp Init / PasteObject) — otherwise a second drop of the same field yields two
		// identically-named controls. The control is already attached (CreateObject added it), so the
		// resolver sees every sibling name and bumps the numeric suffix if the base is taken.
		if (ibValueForm* form = control->GetOwnerForm())
			form->ResolveNameConflict(control);
	}
}

// Resolve the control class for a dropped source path — the DROP side's type->control choice (the canon:
// the drag carries the path, the drop picks the class from the type AT the leaf). Mirrors BuildForm's
// per-field choice: a list / table-section leaf -> Tablebox, a single-clsid boolean -> Checkbox, everything
// else (scalar / reference / multi-type) -> Textctrl. Empty = nothing to create (a bad path).
static wxString ResolveDropControlClass(ibValueForm* form, const ibSourceDescription& desc)
{
	const std::vector<ibSourceHop>& path = desc.GetPath();
	if (form == nullptr || path.empty())
		return wxEmptyString;
	ibFormAttributeValue* head = form->FindAttributeById(desc.GetFirst());
	if (head == nullptr)
		return wxEmptyString;
	ibSourceDataObject* source = head->GetSourceValue();

	// The leaf is either the whole attribute (a 1-hop path) or a node reached by walking the head's source
	// explorer down the remaining hops. Deeper hops go through THE shared structure walk (WalkColumns — the
	// same door the caption / type / gate resolvers use), so a dotted path THROUGH A REFERENCE resolves: the
	// reference vends its target's columns and the walk descends into them. The hand-rolled FindById loop this
	// replaced was a SECOND walk that could not hop a reference — its leaf came back null, so every field
	// behind a reference (DeletionMark, Code, …) declined the drop.
	bool isTable = false, containerIsTable = false;
	ibTypeDescription typeDesc;
	if (path.size() == 1) {
		isTable = source != nullptr && source->IsTableSource();
		typeDesc = head->GetTypeDesc();
	}
	else {
		const ibBackendSourceColumn* leaf = nullptr;
		if (source == nullptr || !source->WalkColumns(path, 1, leaf, nullptr, &isTable, &containerIsTable))
			return wxEmptyString;   // broken binding (a hop missed) -> nothing to create
		// A field inside a list / table section is a COLUMN, bindable only INTO that table; the drop's tablebox
		// branch serves the valid drop, so a canvas / non-table drop here declines instead of a broken control.
		if (containerIsTable)
			return wxEmptyString;
		if (!isTable) {
			if (leaf == nullptr)
				return wxEmptyString;   // no descriptor and not a table -> nothing bindable
			typeDesc = leaf->GetTypeDesc();
		}
	}

	if (isTable)
		return wxT("Tablebox");
	// ⭐ A COMPOSITION OR A SPREADSHEET DOCUMENT DROPS AS A GRIDBOX (Max, 2026-08-19). Both are shown
	// by the same control and by no other: the document IS what a gridbox displays, and a composition
	// composes one. Dropping either therefore builds the box that can show it, bound to the attribute
	// that was dragged.
	if (typeDesc.GetClsidCount() == 1 &&
		(typeDesc.ContainType(g_valueDataCompositionCLSID) || typeDesc.ContainType(g_valueSpreadsheetCLSID)))
		return wxT("Gridbox");
	if (typeDesc.GetClsidCount() == 1 && typeDesc.ContainType(ibValueTypes::TYPE_BOOLEAN))
		return wxT("Checkbox");
	// A SCHEDULE is shown, not typed — the static text renders it as its own sentence and opens
	// the four-tab editor on a click. Same choice the runtime auto-build makes (formObject.cpp),
	// so dropping one and generating one give the same form.
	if (typeDesc.GetClsidCount() == 1 && typeDesc.ContainType(g_valueScheduleCLSID))
		return wxT("Statictext");
	return wxT("Textctrl");
}

void ibVisualEditorNotebook::ibVisualEditor::CreateCommandButton(ibValueFrame* target, const ibCommandDescription& desc)
{
	if (m_valueForm == nullptr || !desc.IsOk())
		return;

	// Resolve the display name by matching the dropped id path against the form's gathered command sources (the
	// ONE gather that also feeds the navigator + picker) — no per-hop kind needed. Only the caption is seeded;
	// the icon stays LIVE (BuildCommands / the button's Update re-resolve it from the command).
	wxString caption;   // the command-source CELL text = the full PATH (fullName), so the binding reads as its path
	for (const ibCommandSourceEntry& entry : GatherFormCommands(m_valueForm))
		if (entry.desc == desc) { caption = entry.fullName.IsEmpty() ? entry.label : entry.fullName; break; }

	// A drop that landed ON a command-bar TOOLBAR (the form's own or a tablebox's — the resolver tagged it) joins
	// THAT bar as a new item, never a free button. Read + clear the per-drop channel so it can't leak to the next drop.
	if (ibValueCommandBar* dropBar = m_pendingDropBar) {
		m_pendingDropBar = nullptr;
		ibValueCommandBarItem* item = dropBar->AddCommandItem();   // fresh unique name + SetBar
		item->SetCommandDesc(desc, caption);   // the item's Caption stays unset -> the command supplies the DEFAULT look
		Modify(true);
		RefreshEditor();
		return;
	}

	// A drop onto a TABLEBOX projects the command onto ITS OWN command bar (a bar item where table-scoped commands
	// live). Every other drop — the form or a plain control — creates a BUTTON control bound to the command.
	for (ibValueFrame* box = target; box != nullptr; box = box->GetParent()) {
		if (box->GetClassType() != g_controlTableBoxCLSID)
			continue;
		if (!box->HasCommandBar())
			break;
		ibValueCommandBarItem* item = box->GetCommandBar()->AddCommandItem();   // fresh unique name + SetBar
		item->SetCommandDesc(desc, caption);   // bind the command id path + its display name; the CAPTION is left
		// unset on purpose so the command provides the DEFAULT look (BuildCommands fills it via the command door).
		Modify(true);
		RefreshEditor();
		return;
	}

	// A drop ALWAYS ADDS a new command button — even onto an existing button — never overwrites the target's
	// binding. (The old "drop onto a button -> rebind THAT button" silently replaced the button's command, which
	// read as a bug: you dropped a second command and the first vanished.) The new button lands next to the target.

	// Form / plain-control drop -> a NEW Button control bound to the command, through the SAME create flow any
	// dropped control uses (CreateControlOfClass). The bind leaves the Title UNSET so the command supplies the
	// DEFAULT caption + icon (button->Update resolves them live via the command door).
	CreateControlOfClass(target, wxT("Button"), [&desc, &caption](ibValueFrame* control) {
		if (control != nullptr && control->GetClassType() == g_controlButtonCLSID) {   // type by clsid, not dynamic_cast
			ibValueButton* button = static_cast<ibValueButton*>(control);
			button->SetCommandDesc(desc, caption);
			button->SetCaption(wxEmptyString);
		}
	});
}

ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::CreateBoundControl(
	ibValueFrame* parentHint, const ibSourceDescription& desc)
{
	wxASSERT(m_valueForm);

	// A drop onto a TABLEBOX means "add a column", not "create a control". Climb from the hit-tested target
	// to the nearest tablebox (type by clsid). A field UNDER the table's own bound source (its path a strict
	// prefix of the field's) becomes a ROW column; a single-value source from elsewhere (an attribute /
	// object field) becomes a FOREIGN / object column (Mode 2). Either way append a view column bound to the
	// field — added at the END and bound in ONE step. Bind BEFORE the widget builds so it renders visible.
	for (ibValueFrame* box = parentHint; box != nullptr; box = box->GetParent()) {
		if (box->GetClassType() != g_controlTableBoxCLSID)
			continue;
		const ibTypeControlFactory* boxFactory = dynamic_cast<const ibTypeControlFactory*>(box);
		if (boxFactory == nullptr)
			break;
		const std::vector<ibSourceHop>& boxPath = boxFactory->GetSourceDesc().GetPath();
		const std::vector<ibSourceHop>& fieldPath = desc.GetPath();
		if (fieldPath.empty() || fieldPath == boxPath)
			break;                                             // the table's own bare source -> nothing to add
		bool underTable = fieldPath.size() > boxPath.size();   // a field PAST the table's own source
		for (size_t i = 0; underTable && i < boxPath.size(); i++)
			if (fieldPath[i].m_id != boxPath[i].m_id)
				underTable = false;                            // diverges from the table's source (structural, by id)
		if (!underTable) {
			// Not a column of THIS table's source. Allow a SINGLE-value source (a scalar / reference
			// attribute or object field) as a FOREIGN / object column (Mode 2 — one value per row); a column
			// of a DIFFERENT table (ResolveDropControlClass declines it) has no per-row value here.
			const wxString foreignClass = ResolveDropControlClass(m_valueForm, desc);
			if (foreignClass != wxT("Textctrl") && foreignClass != wxT("Checkbox"))
				break;
		}

		const wxString colClass = wxT("TableboxColumn");
		ibValueFrame* column = m_valueForm->CreateObject(_STDSTR(colClass), box);
		if (column == nullptr)
			break;
		SetBoundSource(column, desc);                                    // bind BEFORE build -> renders visible
		Execute(new ibVisualEditorInsertObjectCmd(this, column, box));   // append at the end (undoable)
		// Refresh ONLY the object tree — a table column provisions NO new form attribute, so the attribute
		// tree must NOT rebuild: that would collapse the expanded source and drop the user's selection right
		// as they finish the drag. (This is why we don't call the full NotifyObjectCreated here.)
		m_objectTree->OnObjectCreated(column);
		SelectObject(column, true, true);
		return column;
	}

	const wxString className = ResolveDropControlClass(m_valueForm, desc);
	if (className.IsEmpty())
		return nullptr;
	// A scalar control shows its value / a tablebox refills its columns on first build because the source is
	// bound BEFORE the widget is created — CreateControlOfClass runs the bind step ahead of the insert.
	return CreateControlOfClass(parentHint, className, [this, &desc](ibValueFrame* control) {
		SetBoundSource(control, desc);
	});
}

// The shared create-a-control flow behind CreateBoundControl (source drop) and CreateCommandButton (command drop).
// Climb to a parent that can hold `className`, create + unwrap it, run `bind` BEFORE the widget builds (so its
// Update renders it already bound — no stale-until-reselect), insert undoably, notify, refill-if-source, select.
ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::CreateControlOfClass(
	ibValueFrame* parentHint, const wxString& className, const std::function<void(ibValueFrame*)>& bind)
{
	// Walk up until a parent that can hold the control (an item / leaf can't be a parent — climb to its container).
	ibValueFrame* parent = (parentHint != nullptr) ? parentHint : m_valueForm;
	ibValueFrame* obj = nullptr;
	while (parent != nullptr && obj == nullptr) {
		obj = m_valueForm->CreateObject(_STDSTR(className), parent);
		if (obj == nullptr) {
			parent = parent->GetParent();
			while (parent != nullptr && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
				parent = parent->GetParent();
		}
	}
	if (obj == nullptr)
		return nullptr;

	// Unwrap the sizer-item wrapper to the real control, then bind BEFORE the build: InsertObjectCmd runs the
	// control's Create -> Update pipeline, and Update reads the binding — so it renders correct on first paint.
	ibValueFrame* control = obj;
	while (control != nullptr && control->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		control = control->GetChildCount() > 0 ? control->GetChild(0) : nullptr;
	if (control != nullptr && bind)
		bind(control);

	const int pos = CalcPositionOfInsertion(parentHint, parent);
	Execute(new ibVisualEditorInsertObjectCmd(this, obj, parent, pos));   // builds the widget (already bound)
	NotifyObjectCreated(obj);

	// A source-bound control (a tablebox) fills its DEFAULT columns from its source explorer now the widget
	// exists — the designer twin of the runtime CreateColumnCollection. A no-op for a scalar / a plain button
	// (not an ibTypeControlFactory).
	if (ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(control))
		factory->RefillFromSource();

	if (control != nullptr)
		SelectObject(control, true, true);
	return control;
}

void ibVisualEditorNotebook::ibVisualEditor::WireTableboxDrops(ibValueFrame* obj)
{
	if (obj == nullptr)
		return;
	// Set ONE drop target on the tablebox's composite chrome; the composite forwards it to its inner grid
	// (ibCanvasWindow::SetDropTarget) — no reaching past the composite into the grid's windows. Re-wired on
	// every editor refresh, since the widgets rebuild and take their drop targets with them.
	if (obj->GetClassType() == g_controlTableBoxCLSID) {
		ibValueFrame* box = obj;
		if (wxWindow* chrome = wxDynamicCast(obj->GetWxObject(), wxWindow))
			// A drop ON the tablebox grid targets THIS table (fixed target = box, the point is irrelevant): a source
			// path → a bound column, a command → a command on this table. DEFERRED — this target
			// lives ON the grid, and the apply rebuilds / re-wires that grid, freeing THIS very target mid-callback
			// (use-after-free); the target clones the decoded item and runs the apply after the drag unwinds.
			chrome->SetDropTarget(new ibSourceDragDropTarget(
				// Drop on the tablebox grid targets THIS table; clear the per-drop command-bar channel (no form toolbar
				// here) so a command drop climbs to the table's OWN bar instead of reading a stale bar from a prior drop.
				[this, box](wxCoord, wxCoord) -> ibValueFrame* { m_pendingDropBar = nullptr; return box; },
				[this](const ibFormDragItem& item, ibValueFrame* target) { item.ApplyDrop(this, target); },
				/*deferred*/ true));
	}
	for (unsigned int i = 0; i < obj->GetChildCount(); i++)
		WireTableboxDrops(obj->GetChild(i));
}

void ibVisualEditorNotebook::ibVisualEditor::InsertObject(ibValueFrame* obj, ibValueFrame* parent)
{
	Execute(new ibVisualEditorInsertObjectCmd(this, obj, parent));
	NotifyObjectCreated(obj);
	// WHAT WAS JUST MADE IS WHAT YOU ARE WORKING ON — so it is the selection, and its
	// properties are the ones in the inspector. Said once here rather than in every
	// "Add column / Add group / Add page / Add button", which is why the drag-and-drop
	// path (which selects for itself) looked different from the menu one.
	SelectObject(obj, true, true);
}

void ibVisualEditorNotebook::ibVisualEditor::CopyObject(ibValueFrame* obj)
{
	// Make a copy of the object on the clipboard, otherwise
	// modifications to the object after the copy will also
	// be made on the clipboard.
	ibValueFrame* objParent = obj->GetParent();
	wxASSERT(m_valueForm);
	ibValueForm::CopyObject(
		objParent != nullptr && objParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM ?
		objParent : obj
	);
}

#include <wx/clipbrd.h>

bool ibVisualEditorNotebook::ibVisualEditor::PasteObject(ibValueFrame* dstObject)
{
	wxASSERT(m_valueForm);

	if (!m_valueForm->IsEditable())
		return false;

	try {

		// if the object could not be created, hang it off "parent"'s parent
		// and insert it at the position right after "parent"
		ibValueFrame* objParent =
			dstObject != nullptr && dstObject->GetComponentType() == COMPONENT_TYPE_SIZERITEM ? dstObject->GetParent() : dstObject;

		ibValueFrame* clipboard = ibValueForm::PasteObject(m_valueForm, objParent);
		if (clipboard == nullptr)
			return false;

		// Anchor the pasted subtree root across the detach/reparent dance below:
		// under owning children a RemoveChild on a sole-owned node would destroy
		// it. The `clipboard` variable is reassigned to sub/ancestor nodes later,
		// but they all stay alive transitively through this root anchor.
		ibValuePtr<ibValueFrame> clipboardKeep(clipboard);

		// Remove parent/child relationship from clipboard object
		ibValueFrame* clipParent = clipboard->GetParent();
		if (clipParent) {
			clipParent->RemoveChild(clipboard);
			clipboard->SetParent(nullptr);
		}

		// if the object could not be created, hang it off "parent"'s parent
		// and insert it at the position right after "parent"
		ibValueFrame* parentObject = dstObject;
		if (parentObject->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			parentObject = parentObject->GetParent();
		}

		if (clipboard->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			clipboard = clipboard->GetChild(0);
		}

		ibValueFrame* obj = nullptr;
		ibValuePtr<ibValueFrame> objKeep; // anchor obj across its detach below (owning children)
		while (obj == nullptr) {
			obj = m_valueForm->CreateObject(_STDSTR(clipboard->GetClassName()), parentObject);
			if (obj != nullptr) {
				objKeep = obj; // own before RemoveChild drops parentObject's handle
				if (parentObject) {
					obj->SetParent(nullptr);
					parentObject->RemoveChild(obj);
				}
				ibValueFrame* clipParent = clipboard->GetParent();
				if (clipParent && clipParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
					clipboard = clipParent;
				}
			}
			else {
				parentObject = parentObject->GetParent();
				if (parentObject->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
					parentObject = parentObject->GetParent();
				}
			}
			if (parentObject == nullptr)
				break;
		}

		if (!obj) {
			return false;
		}

		ibValueFrame* aux = obj;

		while (aux && aux != clipboard)
			aux = (aux->GetChildCount() > 0 ? aux->GetChild(0) : nullptr);

		int pos = CalcPositionOfInsertion(dstObject, parentObject);

		if (aux && aux != obj) {
			// replace aux with clipboard
			ibValueFrame* auxParent = aux->GetParent();
			auxParent->RemoveChild(aux);
			aux->SetParent(nullptr);

			auxParent->AddChild(clipboard);
			clipboard->SetParent(auxParent);
		}
		else {
			obj = clipboard;
		}

		// and finally insert into the tree
		Execute(new ibVisualEditorInsertObjectCmd(this, obj, parentObject, pos));
		NotifyObjectCreated(obj);

		// keep the newly created object selected — but note it is very likely the
		// created object is an "item"
		while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			assert(obj->GetChildCount() > 0);
			obj = obj->GetChild(0);
		}

		SelectObject(obj, true, true);
	}
	// The project's own exception first — see the twin note above.
	catch (const ibBackendException& err)
	{
		wxLogError(wxT("%s"), err.GetErrorDescription());
		return false;
	}
	catch (const std::exception& ex)
	{
		wxLogError(wxT("%s"), wxString::FromUTF8(ex.what()));
		return false;
	}

	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::ExpandObject(ibValueFrame* obj, bool expand)
{
	Execute(new ibVisualEditorExpandObjectCmd(this, obj, expand));

	// collapse also all children ...
	PropagateExpansion(obj, expand, !expand);
	NotifyObjectExpanded(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::RemoveObject(ibValueFrame* obj)
{
	DoRemoveObject(obj, false);
}

void ibVisualEditorNotebook::ibVisualEditor::CutObject(ibValueFrame* obj, bool force)
{
	ibValueFrame* objParent = obj->GetParent();
	wxASSERT(m_valueForm);
	ibValueForm::CopyObject(
		objParent != nullptr && objParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM ?
		objParent : obj, false
	);

	DoRemoveObject(obj, true, force);
}

bool ibVisualEditorNotebook::ibVisualEditor::SelectObject(ibValueFrame* obj, bool force, bool notify)
{
	if ((obj == GetCurrentElement()) && !force)   // compare with OUR selection state, not the inspector's
		return false;

	// The canvas control path is just the frame case of the ONE selector: it sets the highlight + the selection and
	// soft-lights the inspector. Only the canvas adds a NOTIFY (scroll / observers) on top.
	SetCurrentElement(obj);
	if (notify)
		NotifyObjectSelected(obj, force);
	return true;
}

// ibEditorSelection — the KIND is captured by the ctor's PARAMETER TYPE (a control / attribute / command / other
// property), so it can never be set wrong and AsFrame is a STATIC cast, never RTTI. The upcast to ibPropertyObject*
// lives here (out-of-line) because visualEditor.h only forward-declares the attribute / command types.
ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::ibEditorSelection(ibValueFrame* frame)
	: m_object(frame), m_kind(frame != nullptr ? Frame : None) {}
ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::ibEditorSelection(ibFormAttributeValue* attribute)
	: m_object(attribute), m_kind(attribute != nullptr ? Attribute : None) {}
ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::ibEditorSelection(ibFormCommandValue* command)
	: m_object(command), m_kind(command != nullptr ? Command : None) {}
ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::ibEditorSelection(ibPropertyObject* other)
	: m_object(other), m_kind(other != nullptr ? Property : None) {}

// The control view: a STATIC cast, valid because Frame was set from an ibValueFrame* at construction. A mismatch
// (the selection is an attribute / command / nothing) simply returns null — you asked for a control, there is none.
ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::AsFrame() const
{
	return m_kind == Frame ? static_cast<ibValueFrame*>(m_object) : nullptr;
}

ibFormAttributeValue* ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::AsAttribute() const
{
	return m_kind == Attribute ? static_cast<ibFormAttributeValue*>(m_object) : nullptr;
}

ibFormCommandValue* ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::AsCommand() const
{
	return m_kind == Command ? static_cast<ibFormCommandValue*>(m_object) : nullptr;
}

// The frame VIEW of the ONE selection — DERIVED, never stored. The control when the selection IS a control;
// otherwise (an attribute / command / nothing) the form root, so canvas ops always get a valid frame.
ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::GetSelectedObject() const
{
	ibValueFrame* frame = m_selected.AsFrame();
	return frame != nullptr ? frame : m_valueForm;
}

// LIGHT the inspector on this selection — THE only spot that reaches objectInspector to reveal a selection. forceOpen
// raises a hidden inspector; the soft path fills only an already-open one (SelectObject is a no-op while hidden).
void ibVisualEditorNotebook::ibVisualEditor::ibEditorSelection::Reveal(bool forceOpen) const
{
	if (m_object == nullptr)
		return;
	if (forceOpen) {
		if (!objectInspector->IsShownInspector())
			objectInspector->ShowInspector();           // SHOW: raise the panel only when hidden
	}
	else if (!objectInspector->IsShownInspector())
		return;                                          // SOFT: no panel -> nothing to reveal / refresh
	objectInspector->SelectObject(m_object, true);      // force-rebuild the grid on this object
}

// THE one entry point — set the selection, sync the canvas highlight, and light the inspector through the selection.
void ibVisualEditorNotebook::ibVisualEditor::SetCurrentElement(ibEditorSelection element, bool forceOpen)
{
	m_selected = element;
	if (ibValueFrame* frame = element.AsFrame())
		m_visualEditor->SetObjectSelect(frame);   // a control -> the canvas highlight follows the selection
	else
		m_visualEditor->ClearObjectSelect();       // anything else -> drop the canvas highlight (no "two selected")
	m_selected.Reveal(forceOpen);                  // the selector lights the inspector — nothing else does
}

// Re-reveal the current element unchanged (explicit "Properties") — a thin wrapper over the one selector.
void ibVisualEditorNotebook::ibVisualEditor::ShowCurrentElement(bool forceOpen)
{
	SetCurrentElement(m_selected, forceOpen);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::MovePosition(ibValueFrame* obj, unsigned int toPos)
{
	Execute(new ibVisualEditorShiftChildCmd(this, obj, toPos));
	NotifyEditorRefresh();
	SelectObject(obj, true);
}

void ibVisualEditorNotebook::ibVisualEditor::MovePosition(ibValueFrame* obj, bool right, unsigned int num)
{
	ibValueFrame* noItemObj = obj;
	ibValueFrame* parent = obj->GetParent();

	if (parent != nullptr) {
		// If the object is contained within an item, the item must be
		// shifted

		while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			obj = parent;
			parent = obj->GetParent();
		}

		unsigned int pos = parent->GetChildPosition(obj);

		// make sure the bounds are correct
		unsigned int children_count = parent->GetChildCount();

		if ((right && num + pos < children_count) ||
			(!right && (num <= pos))) {
			pos = (right ? pos + num : pos - num);
			Execute(new ibVisualEditorShiftChildCmd(this, obj, pos));
			NotifyEditorRefresh();
			SelectObject(noItemObj, true);
		}
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ScrollToObject(ibValueFrame* obj)
{
	m_visualEditor->ScrollToObject(obj);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::ModifyProperty(ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (oldValue != newValue) {
		Execute(new ibVisualEditorModifyPropertyCmd(this, prop, oldValue, newValue));
		NotifyPropertyModified(prop);
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ModifyEvent(ibEvent* evt, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (oldValue != newValue) {
		Execute(new ibVisualEditorModifyEventCmd(this, evt, oldValue, newValue));
		NotifyEventModified(evt);
	}
}

//Attributes
void ibVisualEditorNotebook::ibVisualEditor::InsertAttribute(ibValuePtr<ibFormAttributeValue> holder)
{
	ibValueForm* form = GetValueForm();
	if (form != nullptr && holder != nullptr)
		Execute(new ibVisualEditorInsertAttributeCmd(this, form, std::move(holder)));
}

void ibVisualEditorNotebook::ibVisualEditor::RemoveAttribute(ibFormAttributeValue* entry)
{
	ibValueForm* form = GetValueForm();
	if (form != nullptr && entry != nullptr)
		Execute(new ibVisualEditorRemoveAttributeCmd(this, form, entry));
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::PropagateExpansion(ibValueFrame* obj, bool expand, bool up)
{
	if (obj != nullptr) {
		if (up) {
			ibValueFrame* child = nullptr;
			for (unsigned int i = 0; i < obj->GetChildCount(); i++) {
				child = obj->GetChild(i);
				Execute(new ibVisualEditorExpandObjectCmd(this, child, expand));
				PropagateExpansion(child, expand, up);
			}
		}
		else
		{
			PropagateExpansion(obj->GetParent(), expand, up);
			Execute(new ibVisualEditorExpandObjectCmd(this, obj, expand));
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::DoRemoveObject(ibValueFrame* obj, bool cutObject, bool force)
{
	// Note:
	//  When removing an object it is important that the "item" objects
	// are not left behind
	ibValueFrame* parent = obj->GetParent();
	ibValueFrame* deleted_obj = obj;

	if (parent) {
		// Get the top item
		while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			obj = parent;
			parent = obj->GetParent();
		}

		if (!cutObject) {
			if (!deleted_obj->CanDeleteControl()) {
				return;
			}
		}

		NotifyObjectRemoved(deleted_obj);

		if (cutObject) {
			Execute(new ibVisualEditorCutObjectCmd(this, obj, force));
		}
		else {
			Execute(new ibVisualEditorRemoveObjectCmd(this, obj));
		}

		SelectObject(GetSelectedObject(), true, true);
	}
	else {
		if (obj->GetClassType() != g_controlFormCLSID) {
			assert(false);
		}
	}
}

void ibVisualEditorNotebook::ibVisualEditor::DetermineObjectToSelect(ibValueFrame* parent, unsigned int pos)
{
	// get position of next control or last control
	ibValueFrame* objToSelect = nullptr;
	unsigned int count = parent->GetChildCount();
	if (0 == count) {
		objToSelect = parent;
	}
	else {
		pos = (pos < count ? pos : count - 1);
		objToSelect = parent->GetChild(pos);
	}

	while (objToSelect && objToSelect->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		objToSelect = objToSelect->GetChild(0);
	}

	SelectObject(objToSelect);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::Undo()
{
	m_cmdProc->Undo();
	m_document->Modify(!m_cmdProc->IsAtSavePoint());
	NotifyEditorRefresh();
	NotifyObjectSelected(GetSelectedObject());
}

void ibVisualEditorNotebook::ibVisualEditor::Redo()
{
	m_cmdProc->Redo();
	m_document->Modify(!m_cmdProc->IsAtSavePoint());
	NotifyEditorRefresh();
	NotifyObjectSelected(GetSelectedObject());
}

#include <wx/clipbrd.h>

bool ibVisualEditorNotebook::ibVisualEditor::CanPasteObject() const
{
	ibValueFrame* obj = GetSelectedObject();
	if (obj != nullptr) {
		bool canPasteObject = wxTheClipboard->Open() &&
			wxTheClipboard->IsSupported(oes_clipboard_frame);
		wxTheClipboard->Close();
		return canPasteObject;
	}
	return false;
}

bool ibVisualEditorNotebook::ibVisualEditor::CanCopyObject() const
{
	ibValueFrame* obj = GetSelectedObject();
	if (obj && obj->GetClassType() != g_controlFormCLSID)
		return true;
	return false;
}

int ibVisualEditorNotebook::ibVisualEditor::CalcPositionOfInsertion(ibValueFrame* selected, ibValueFrame* parent)
{
	int pos = wxNOT_FOUND;
	if (parent && selected) {
		ibValueFrame* parentSelected = selected->GetParent();
		while (parentSelected && parentSelected != parent) {
			selected = parentSelected;
			parentSelected = selected->GetParent();
		}
		if (parentSelected && parentSelected == parent) {
			pos = parent->GetChildPosition(selected) + 1;
		}
	}
	return pos;
}
