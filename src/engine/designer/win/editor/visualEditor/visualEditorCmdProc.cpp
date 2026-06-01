////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "backend/propertyManager/propertyManager.h"

void ibVisualEditorNotebook::ibVisualEditor::Execute(ibVisualEditorCmd* cmd)
{
	if (m_cmdProc != nullptr) m_cmdProc->Execute(cmd);
	NotifyEditorSaved();
}

///////////////////////////////////////////////////////////////////////////////
// Comandos
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
* Comando para insertar un objeto en el arbol.
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
* Comando para borrar un objeto.
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
* Comando para modificar una propiedad.
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
* Comando para mover de posicion un objeto.
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
* ibVisualEditorCutObjectCmd ademas de eliminar el objeto del arbol se asegura
* de eliminar la referencia "clipboard" deshacer el cambio.
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

	// necesario para consultar/modificar el objeto "clipboard"
	ibValueFrame* m_parent = nullptr;
	ibValuePtr<ibValueFrame> m_object; // owns the in-flight control (RAII +1/-1)
	int m_oldPos;
	ibValueFrame* m_oldSelected = nullptr;

	bool m_needEvent;
};

///////////////////////////////////////////////////////////////////////////////
// Implementacion de los Comandos
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
	if (valueForm != nullptr) valueForm->PrepareNames();

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
	if (valueForm != nullptr) valueForm->PrepareNames();

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
	if (valueForm != nullptr) valueForm->PrepareNames();
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

	// restauramos la posicion
	m_parent->ChangeChildPosition(m_object, m_oldPos);
	m_visualEditor->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	visualEditor->CreateControl(m_object);

	ibValueForm* valueForm = visualEditor->GetValueForm();
	if (valueForm != nullptr) valueForm->PrepareNames();
}

//-----------------------------------------------------------------------------

ibVisualEditorModifyPropertyCmd::ibVisualEditorModifyPropertyCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) : m_visualEditor(visualEditor),
m_property(prop), m_oldValue(prop), m_newValue(newValue)
{
}

void ibVisualEditorModifyPropertyCmd::DoExecute()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control = dynamic_cast<ibValueFrame*>(m_property->GetPropertyObject());
	wxASSERT(control);

	m_property->SetValue(m_newValue);
	m_visualEditor->Modify(true);

	if (g_controlFormCLSID == control->GetClassType()) {
		visualEditor->UpdateVisualHost();
	}
	else {
		visualEditor->UpdateControl(control);
	}
}

void ibVisualEditorModifyPropertyCmd::DoRestore()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control = dynamic_cast<ibValueFrame*>(m_property->GetPropertyObject());
	wxASSERT(control);

	m_property->SetValue(m_oldValue);

	if (g_controlFormCLSID == control->GetClassType()) {
		visualEditor->UpdateVisualHost();
	}
	else {
		visualEditor->UpdateControl(control);
	}

	m_visualEditor->Modify(true);

	objectInspector->SelectObject(control);
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
	ibValueFrame* control = dynamic_cast<ibValueFrame*>(m_event->GetPropertyObject());
	wxASSERT(control);

	m_event->SetValue(m_newValue);
	m_visualEditor->Modify(true);

	if (g_controlFormCLSID == control->GetClassType()) {
		visualEditor->UpdateVisualHost();
	}
	else {
		visualEditor->UpdateControl(control);
	}
}

void ibVisualEditorModifyEventCmd::DoRestore()
{
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor = m_visualEditor->GetVisualEditor();

	// Get the ibValueFrame from the event
	ibValueFrame* control = dynamic_cast<ibValueFrame*>(m_event->GetPropertyObject());
	wxASSERT(control);

	m_event->SetValue(m_oldValue);
	m_visualEditor->Modify(true);

	if (g_controlFormCLSID == control->GetClassType()) {
		visualEditor->UpdateVisualHost();
	}
	else {
		visualEditor->UpdateControl(control);
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

	// reubicamos el objeto donde estaba
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

	// restauramos el clipboard
	//m_visualEditor->SetClipboardObject(nullptr);
	m_visualEditor->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost* visualEditor =
		m_visualEditor->GetVisualEditor();

	visualEditor->CreateControl(m_object, m_parent);
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

			// Para que sea mas practico, si el objeto no se puede crear debajo
			// del objeto seleccionado vamos a intentarlo en el padre del seleccionado
			// y seguiremos subiendo hasta que ya no podamos crear el objeto.

			while (parent && !created)
			{
				// ademas, el objeto se insertara a continuacion del objeto seleccionado
				obj = m_valueForm->CreateObject(_STDSTR(name), parent);

				if (obj)
				{
					int pos = CalcPositionOfInsertion(GetSelectedObject(), parent);

					Execute(new ibVisualEditorInsertObjectCmd(this, obj, parent, pos));
					created = true;
				}
				else
				{
					// lo vamos a seguir intentando con el padre, pero cuidado, el padre
					// no puede ser un item!
					parent = parent->GetParent();

					while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
						parent = parent->GetParent();
				}
			}
		}

		// Seleccionamos el objeto, si este es un item entonces se selecciona
		// el objeto contenido. ?Tiene sentido tener un item debajo de un item?
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

		// si no se ha podido crear el objeto vamos a intentar crearlo colgado
		// del padre de "parent" y ademas vamos a insertarlo en la posicion
		// siguiente a "parent"
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

		// si no se ha podido crear el objeto vamos a intentar crearlo colgado
		// del padre de "parent" y ademas vamos a insertarlo en la posicion
		// siguiente a "parent"
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
			// sustituimos aux por clipboard
			ibValueFrame* auxParent = aux->GetParent();
			auxParent->RemoveChild(aux);
			aux->SetParent(nullptr);

			auxParent->AddChild(clipboard);
			clipboard->SetParent(auxParent);
		}
		else {
			obj = clipboard;
		}

		// y finalmente insertamos en el arbol
		Execute(new ibVisualEditorInsertObjectCmd(this, obj, parentObject, pos));
		NotifyObjectCreated(obj);

		// vamos a mantener seleccionado el nuevo objeto creado
		// pero hay que tener en cuenta que es muy probable que el objeto creado
		// sea un "item"
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
		// Si el objeto está incluido dentro de un item hay que desplazar
		// el item

		while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			obj = parent;
			parent = obj->GetParent();
		}

		unsigned int pos = parent->GetChildPosition(obj);

		// nos aseguramos de que los límites son correctos
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
