////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "backend/propertyManager/propertyManager.h"

#include "frontend/visualView/ctrl/formAttribute.h"   // ibFormAttributeValue (form attribute add command)
#include "frontend/visualView/ctrl/tableBox.h"         // g_controlTableBoxCLSID — a drop onto a tablebox appends a column
#include "backend/query/queryColumn.h"                 // ibBackendSourceColumn::GetName (name a dropped control after its field)
#include "backend/srcDataObject.h"                     // ibSourceExplorer — resolve a dropped path's leaf type -> control class
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
		// RefreshEditor rebuilds the object tree, which drops the tree selection — and with it the inspector's
		// focus. Re-select the object whose property changed so the inspector STAYS on it (else toggling e.g.
		// a command bar's AutoFill bounces the inspector off the bar onto the form).
		if (ibPropertyObject* owner = m_property->GetPropertyObject())
			objectInspector->SelectObject(owner, true);
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
	// Re-select the object whose property was restored — GetPropertyObject() generalises `control` to a LAYER
	// owner (command bar / item) too, so undo keeps the inspector on it instead of dropping to null.
	if (ibPropertyObject* owner = m_property->GetPropertyObject())
		objectInspector->SelectObject(owner, true);
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
		objectInspector->SelectObject(m_entry, true);
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
		objectInspector->SelectObject(m_entry, true);
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
	catch (const std::exception& ex)
	{
		wxLogError(ex.what());
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
	// explorer down the remaining hops (a table section is a CHILD of the object's explorer; a list IS the head).
	bool isTable = false;
	ibTypeDescription typeDesc;
	if (path.size() == 1) {
		isTable = source != nullptr && source->IsTableSource();
		typeDesc = head->GetTypeDesc();
	}
	else {
		// Walk to the leaf, tracking whether its CONTAINER is a list / table section. A field inside one is
		// a COLUMN, bindable only INTO that table (a standalone scalar's path gates on the table row and
		// shows <not selected>). Such a column is served by the drop's tablebox branch; here we decline, so a
		// canvas / non-table drop creates nothing instead of a broken control.
		bool containerIsList = source != nullptr && source->IsTableSource();   // head itself a value-table
		const ibSourceExplorer* explorer = source != nullptr ? source->GetSourceExplorer() : nullptr;
		for (size_t i = 1; i + 1 < path.size() && explorer != nullptr; i++) {
			explorer = explorer->FindById(path[i].m_id);
			containerIsList = explorer != nullptr && explorer->IsTableSection();
		}
		const ibSourceExplorer* leaf = explorer != nullptr ? explorer->FindById(desc.GetLeaf()) : nullptr;
		if (leaf == nullptr)
			return wxEmptyString;
		if (containerIsList)
			return wxEmptyString;   // a table column -> table-only (the tablebox branch handles the valid drop)
		isTable = leaf->IsTableSection();
		typeDesc = leaf->GetTypeDesc();
	}

	if (isTable)
		return wxT("Tablebox");
	if (typeDesc.GetClsidCount() == 1 && typeDesc.ContainType(ibValueTypes::TYPE_BOOLEAN))
		return wxT("Checkbox");
	return wxT("Textctrl");
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
	ibValueFrame* obj = nullptr;
	ibValueFrame* parent = (parentHint != nullptr) ? parentHint : m_valueForm;

	// Walk up until a parent that can hold the control (same convenience as CreateObject: an item /
	// leaf can't be a parent, so climb to its container).
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

	// Insert right after the dropped-on sibling when it shares the resolved parent, else append (-1).
	// Unwrap the sizer-item wrapper to the real control and bind it BEFORE the widget is built, so the
	// widget is created ALREADY bound: the source shows immediately and a tablebox refills its columns
	// from the source. Binding AFTER the build left the control stale until a manual reselect / reopen.
	ibValueFrame* control = obj;
	while (control != nullptr && control->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		control = control->GetChildCount() > 0 ? control->GetChild(0) : nullptr;
	if (control != nullptr)
		SetBoundSource(control, desc);

	// The bind is set on the object ABOVE, BEFORE this: InsertObjectCmd -> ibVisualHost::CreateControl
	// runs the control's own pipeline (Create -> Update -> OnCreated -> OnUpdated), and Update reads the
	// source — a textctrl shows its value, a tablebox refills its columns (CreateColumnCollection). So the
	// drop renders through the SAME event flow as any created control; no extra editor refresh is needed.
	const int pos = CalcPositionOfInsertion(parentHint, parent);
	Execute(new ibVisualEditorInsertObjectCmd(this, obj, parent, pos));   // builds the widget (already bound)
	NotifyObjectCreated(obj);

	// A tablebox fills its DEFAULT columns from the bound source explorer now that its widget exists
	// (the runtime CreateColumnCollection is designer-gated; this is the designer twin, the SAME fill the
	// inspector's Source-change refill uses). No-op for scalar controls.
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
			chrome->SetDropTarget(new ibSourceDragDropTarget([this, box](wxCoord, wxCoord, const ibSourceDescription& d) {
				// DEFER out of the OS drop callback: this target lives ON the tablebox's grid, and adding a
				// column rebuilds / re-wires that grid — freeing THIS very target mid-callback (use-after-free
				// crash). Run the add after the drag-drop unwinds. desc is copied into the deferred call.
				wxTheApp->CallAfter([this, box, d]() { CreateBoundControl(box, d); });
			}));
	}
	for (unsigned int i = 0; i < obj->GetChildCount(); i++)
		WireTableboxDrops(obj->GetChild(i));
}

void ibVisualEditorNotebook::ibVisualEditor::InsertObject(ibValueFrame* obj, ibValueFrame* parent)
{
	Execute(new ibVisualEditorInsertObjectCmd(this, obj, parent));
	NotifyObjectCreated(obj);
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
	catch (const std::exception& ex)
	{
		wxLogError(ex.what());
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
	if ((obj == objectInspector->GetSelectedObject()) && !force)
		return false;

	m_visualEditor->SetObjectSelect(obj); m_selObj = obj;

	if (notify) {
		NotifyObjectSelected(obj, force);
	}

	objectInspector->SelectObject(obj, this);
	return true;
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
	ibPropertyObject* object = prop->GetPropertyObject();
	if (oldValue != newValue) {
		Execute(new ibVisualEditorModifyPropertyCmd(this, prop, oldValue, newValue));
		NotifyPropertyModified(prop);
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ModifyEvent(ibEvent* evt, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibPropertyObject* object = evt->GetPropertyObject();
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
