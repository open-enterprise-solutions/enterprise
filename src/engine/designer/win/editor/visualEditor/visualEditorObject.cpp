////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "backend/wrapper/propertyInfo.h"



///////////////////////////////////////////////////////////////////////////////
// Comandos
///////////////////////////////////////////////////////////////////////////////

class BaseVisualCmd : public CCommand
{
public:
	BaseVisualCmd(CVisualEditorNotebook::CVisualEditor* visualData = nullptr) :
		m_visualData(visualData) {
	}

	virtual void Execute() {
		CCommand::Execute();
	}
	virtual void Restore() {
		CCommand::Restore();
	}
protected:
	CVisualEditorNotebook::CVisualEditor* m_visualData;
};

/** Command for expanding an object in the object tree */

class ExpandObjectCmd : public BaseVisualCmd
{
	IValueFrame* m_object = nullptr;
	bool m_expand;

public:

	ExpandObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, bool expand);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* Comando para insertar un objeto en el arbol.
*/

class InsertObjectCmd : public BaseVisualCmd
{
	IValueFrame* m_parent = nullptr;
	IValueFrame* m_object = nullptr;
	int m_pos;
	IValueFrame* m_oldSelected;
	bool m_firstCreated;

public:

	InsertObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, IValueFrame* parent, int pos = -1, bool firstCreated = true);

protected:

	void GenerateId();
	void ResetId();

	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* Comando para borrar un objeto.
*/

class RemoveObjectCmd : public BaseVisualCmd,
	public wxEvtHandler
{
	IValueFrame* m_parent = nullptr;
	IValueFrame* m_object = nullptr;
	int m_oldPos;
	IValueFrame* m_oldSelected = nullptr;

public:

	RemoveObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object);
	~RemoveObjectCmd();

protected:

	void GenerateId();
	void ResetId();

	void RemoveObject();

	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* Comando para modificar una propiedad.
*/

class ModifyPropertyCmd : public BaseVisualCmd
{
	Property* m_property;
	wxVariant m_oldValue, m_newValue;

public:

	ModifyPropertyCmd(CVisualEditorNotebook::CVisualEditor* data, Property* prop, const wxVariant& oldValue, const wxVariant& newValue);

protected:
	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* BaseVisualCmd for modifying an event
*/

class ModifyEventCmd : public BaseVisualCmd
{
	Event* m_event = nullptr;
	wxVariant m_oldValue, m_newValue;

public:
	ModifyEventCmd(CVisualEditorNotebook::CVisualEditor* data, Event* event, const wxVariant& oldValue, const wxVariant& newValue);

protected:
	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* Comando para mover de posicion un objeto.
*/

class ShiftChildCmd : public BaseVisualCmd
{
	IValueFrame* m_object = nullptr;
	int m_oldPos, m_newPos;

public:
	ShiftChildCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, int pos);

protected:
	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* CutObjectCmd ademas de eliminar el objeto del arbol se asegura
* de eliminar la referencia "clipboard" deshacer el cambio.
*/

class CutObjectCmd : public BaseVisualCmd,
	public wxEvtHandler

{
	// necesario para consultar/modificar el objeto "clipboard"
	IValueFrame* m_parent = nullptr;
	IValueFrame* m_object = nullptr;
	int m_oldPos;
	IValueFrame* m_oldSelected = nullptr;

	bool m_needEvent;

public:

	CutObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, bool force);
	~CutObjectCmd();

protected:

	void GenerateId();
	void ResetId();

	void RemoveObject();

	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

///////////////////////////////////////////////////////////////////////////////
// Implementacion de los Comandos
///////////////////////////////////////////////////////////////////////////////

ExpandObjectCmd::ExpandObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, bool expand) : BaseVisualCmd(data),
m_object(object), m_expand(expand)
{
}

void ExpandObjectCmd::DoExecute()
{
	m_object->SetExpanded(m_expand);
}

void ExpandObjectCmd::DoRestore()
{
	m_object->SetExpanded(!m_expand);
}

InsertObjectCmd::InsertObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, IValueFrame* parent, int pos, bool firstCreated) : BaseVisualCmd(data),
m_parent(parent), m_object(object), m_pos(pos), m_firstCreated(firstCreated)
{
	m_oldSelected = data->GetSelectedObject();

	if (m_parent) {
		m_parent->RemoveChild(m_object);
		m_object->SetParent(nullptr);
	}

	ResetId();
}

void InsertObjectCmd::GenerateId()
{
	std::function<void(IValueFrame*)> reset = [&reset](IValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() == 0) {
			object->GenerateGuid();
			object->GenerateNewID();
		}
		};
	reset(m_object);
}

void InsertObjectCmd::ResetId()
{
	std::function<void(IValueFrame*)> reset = [&reset](IValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() != 0) {
			object->ResetGuid();
			object->SetControlID(0);
		}
		};
	reset(m_object);
}

void InsertObjectCmd::DoExecute()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	if (m_parent) {
		m_parent->AddChild(m_object);
		m_object->SetParent(m_parent);
	}

	GenerateId();

	if (m_pos >= 0) {
		m_parent->ChangeChildPosition(m_object, m_pos);
	}

	IValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	//create control in visual editor
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();
	wxASSERT(visulEditor);
	visulEditor->CreateControl(m_object, nullptr, m_firstCreated);

	//select object
	m_visualData->SelectObject(obj, false, false);
}

void InsertObjectCmd::DoRestore()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	//remove control in visual editor
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* m_visualEditor =
		m_visualData->GetVisualEditor();

	m_visualEditor->RemoveControl(m_object);

	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);

	ResetId();

	m_visualData->SelectObject(m_oldSelected);
}

//-----------------------------------------------------------------------------

RemoveObjectCmd::RemoveObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object) : BaseVisualCmd(data)
{
	m_object = object;
	m_parent = object->GetParent();
	m_oldPos = m_parent->GetChildPosition(object);
	m_oldSelected = data->GetSelectedObject();
}

RemoveObjectCmd::~RemoveObjectCmd()
{
}

void RemoveObjectCmd::GenerateId()
{
	std::function<void(IValueFrame*)> reset = [&reset](IValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() == 0) {
			object->GenerateGuid();
			object->GenerateNewID();
		}
		};
	reset(m_object);
}

void RemoveObjectCmd::ResetId()
{
	std::function<void(IValueFrame*)> reset = [&reset](IValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() != 0) {
			object->ResetGuid();
			object->SetControlID(0);
		}
		};
	reset(m_object);
}

void RemoveObjectCmd::RemoveObject()
{
	//remove control in visual editor
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor = m_visualData->GetVisualEditor();

	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	visulEditor->RemoveControl(m_object);

	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);
}

void RemoveObjectCmd::DoExecute()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	IValueFrame* obj = m_object;
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

	ResetId();
	m_visualData->DetermineObjectToSelect(m_parent, m_oldPos);

	wxEvtHandler::CallAfter(&RemoveObjectCmd::RemoveObject);
}

void RemoveObjectCmd::DoRestore()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	if (m_object->GetParent() == nullptr) {
		m_parent->AddChild(m_object);
		m_object->SetParent(m_parent);
	}

	IValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	GenerateId();

	// restauramos la posicion
	m_parent->ChangeChildPosition(m_object, m_oldPos);
	m_visualData->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	visulEditor->CreateControl(m_object);
}

//-----------------------------------------------------------------------------

ModifyPropertyCmd::ModifyPropertyCmd(CVisualEditorNotebook::CVisualEditor* data, Property* prop, const wxVariant& oldValue, const wxVariant& newValue) : BaseVisualCmd(data),
m_property(prop), m_oldValue(oldValue), m_newValue(newValue)
{
}

void ModifyPropertyCmd::DoExecute()
{
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor = m_visualData->GetVisualEditor();
	// Get the IValueFrame from the event
	IValueFrame* m_object = dynamic_cast<IValueFrame*>(m_property->GetObject());
	m_property->SetValue(m_newValue);

	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	if (g_controlFormCLSID == m_object->GetClassType()) {
		visulEditor->UpdateVisualEditor();
	}
	else {
		visulEditor->UpdateControl(m_object);
	}
}

void ModifyPropertyCmd::DoRestore()
{
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor = m_visualData->GetVisualEditor();
	// Get the IValueFrame from the event
	IValueFrame* m_object = dynamic_cast<IValueFrame*>(m_property->GetObject());

	m_property->SetValue(m_oldValue);

	if (g_controlFormCLSID == m_object->GetClassType()) {
		visulEditor->UpdateVisualEditor();
	}
	else {
		visulEditor->UpdateControl(m_object);
	}

	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	objectInspector->SelectObject(m_object);
}

//-----------------------------------------------------------------------------

ModifyEventCmd::ModifyEventCmd(CVisualEditorNotebook::CVisualEditor* data, Event* event, const wxVariant& oldValue, const wxVariant& newValue) : BaseVisualCmd(data),
m_event(event), m_oldValue(oldValue), m_newValue(newValue)
{
}

void ModifyEventCmd::DoExecute()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	m_event->SetValue(m_newValue);
}

void ModifyEventCmd::DoRestore()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	m_event->SetValue(m_oldValue);
}

//-----------------------------------------------------------------------------

ShiftChildCmd::ShiftChildCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, int pos) : BaseVisualCmd(data)
{
	m_object = object;
	IValueFrame* parent = object->GetParent();

	assert(parent);

	m_oldPos = parent->GetChildPosition(object);
	m_newPos = pos;
}

void ShiftChildCmd::DoExecute()
{
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	if (m_oldPos != m_newPos) {
		IValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object, m_newPos);

		visulEditor->UpdateControl(m_object);
	}

	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);
}

void ShiftChildCmd::DoRestore()
{
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	if (m_oldPos != m_newPos) {
		IValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object, m_oldPos);

		visulEditor->UpdateControl(m_object);
	}

	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);
}

//-----------------------------------------------------------------------------

CutObjectCmd::CutObjectCmd(CVisualEditorNotebook::CVisualEditor* data, IValueFrame* object, bool force) : BaseVisualCmd(data), m_needEvent(!force)
{
	m_object = object;
	m_parent = object->GetParent();
	m_oldPos = m_parent->GetChildPosition(object);
	m_oldSelected = data->GetSelectedObject();
}

CutObjectCmd::~CutObjectCmd()
{
}

void CutObjectCmd::GenerateId()
{
	std::function<void(IValueFrame*)> reset = [&reset](IValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() == 0) {
			object->GenerateGuid();
			object->GenerateNewID();
		}
		};
	reset(m_object);
}

void CutObjectCmd::ResetId()
{
	std::function<void(IValueFrame*)> reset = [&reset](IValueFrame* object) {
		wxASSERT(object);
		for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
			reset(object->GetChild(idx));
		}
		if (object->GetControlID() != 0) {
			object->ResetGuid();
			object->SetControlID(0);
		}
		};
	reset(m_object);
}

void CutObjectCmd::RemoveObject()
{
	//remove control in visual editor
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	visulEditor->RemoveControl(m_object);

	//remove control in visual editor
	m_parent->RemoveChild(m_object);
	m_object->SetParent(nullptr);
}

void CutObjectCmd::DoExecute()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	m_visualData->SetClipboardObject(m_object);

	if (!m_needEvent) {
		//remove control in visual editor
		CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
			m_visualData->GetVisualEditor();
		visulEditor->RemoveControl(m_object);
	}

	IValueFrame* obj = m_object;
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
	m_visualData->DetermineObjectToSelect(m_parent, m_oldPos);

	//reset id, guid
	ResetId();

	if (m_needEvent) {
		wxEvtHandler::CallAfter(&CutObjectCmd::RemoveObject);
	}
}

void CutObjectCmd::DoRestore()
{
	if (m_visualData->m_document != nullptr)
		m_visualData->m_document->Modify(true);

	// reubicamos el objeto donde estaba
	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	IValueFrame* obj = m_object;
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
	m_visualData->SetClipboardObject(nullptr);
	m_visualData->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	CVisualEditorNotebook::CVisualEditor::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	visulEditor->CreateControl(m_object, m_parent);
}

//-----------------------------------------------------------------------------

IValueFrame* CVisualEditorNotebook::CVisualEditor::CreateObject(const wxString& name)
{
	IValueFrame* obj = nullptr;
	wxASSERT(m_valueForm);
	try
	{
		//LogDebug("[CApplicationData::CreateObject] New " + name );
		IValueFrame* old_selected = GetSelectedObject();
		IValueFrame* parent = old_selected;

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

					Execute(new InsertObjectCmd(this, obj, parent, pos));
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

void CVisualEditorNotebook::CVisualEditor::InsertObject(IValueFrame* obj, IValueFrame* parent)
{
	Execute(new InsertObjectCmd(this, obj, parent));
	NotifyObjectCreated(obj);
}

void CVisualEditorNotebook::CVisualEditor::CopyObject(IValueFrame* obj)
{
	m_copyOnPaste = true;

	// Make a copy of the object on the clipboard, otherwise
	// modifications to the object after the copy will also
	// be made on the clipboard.
	IValueFrame* objParent = obj->GetParent();
	wxASSERT(m_valueForm);
	if (objParent && objParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		m_clipboard = m_valueForm->CopyObject(objParent);
	}
	else {
		m_clipboard = m_valueForm->CopyObject(obj);
	}
}

bool CVisualEditorNotebook::CVisualEditor::PasteObject(IValueFrame* dstObject, IValueFrame* objToPaste)
{
	wxASSERT(m_valueForm);

	try {
		IValueFrame* clipboard = nullptr;
		if (objToPaste) {
			clipboard = objToPaste;
		}
		else if (m_clipboard) {
			if (m_copyOnPaste) {
				clipboard = m_valueForm->CopyObject(m_clipboard);
			}
			else {
				clipboard = m_clipboard;
			}
		}

		if (!clipboard)
			return false;

		// Remove parent/child relationship from clipboard object
		IValueFrame* clipParent = clipboard->GetParent();
		if (clipParent) {
			clipParent->RemoveChild(clipboard);
			clipboard->SetParent(nullptr);
		}

		// si no se ha podido crear el objeto vamos a intentar crearlo colgado
		// del padre de "parent" y ademas vamos a insertarlo en la posicion
		// siguiente a "parent"
		IValueFrame* parentObject = dstObject;
		if (parentObject->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			parentObject = parentObject->GetParent();
		}

		if (clipboard->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			clipboard = clipboard->GetChild(0);
		}

		IValueFrame* obj = nullptr;
		while (obj == nullptr) {
			obj = m_valueForm->CreateObject(_STDSTR(clipboard->GetClassName()), parentObject);
			if (obj != nullptr) {
				if (parentObject) {
					obj->SetParent(nullptr);
					parentObject->RemoveChild(obj);
				}
				IValueFrame* clipParent = clipboard->GetParent();
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

		IValueFrame* aux = obj;

		while (aux && aux != clipboard)
			aux = (aux->GetChildCount() > 0 ? aux->GetChild(0) : nullptr);

		int pos = CalcPositionOfInsertion(dstObject, parentObject);

		if (aux && aux != obj) {
			// sustituimos aux por clipboard
			IValueFrame* auxParent = aux->GetParent();
			auxParent->RemoveChild(aux);
			aux->SetParent(nullptr);

			auxParent->AddChild(clipboard);
			clipboard->SetParent(auxParent);
		}
		else {
			obj = clipboard;
		}

		// y finalmente insertamos en el arbol
		Execute(new InsertObjectCmd(this, obj, parentObject, pos));

		if (!m_copyOnPaste)
			m_clipboard = nullptr;

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

IValueFrame* CVisualEditorNotebook::CVisualEditor::SearchSizerInto(IValueFrame* obj)
{
	IValueFrame* theSizer = nullptr;

	if (obj->IsSubclassOf(wxT("boxsizer")) || obj->IsSubclassOf(wxT("wrapsizer")) ||
		obj->IsSubclassOf(wxT("staticboxsizer")) || obj->IsSubclassOf(wxT("gridsizer"))) {
		theSizer = obj;
	}
	else
	{
		for (unsigned int i = 0; !theSizer && i < obj->GetChildCount(); i++)
			theSizer = SearchSizerInto(obj->GetChild(i));
	}

	return theSizer;

}

void CVisualEditorNotebook::CVisualEditor::ExpandObject(IValueFrame* obj, bool expand)
{
	Execute(new ExpandObjectCmd(this, obj, expand));

	// collapse also all children ...
	PropagateExpansion(obj, expand, !expand);
	NotifyObjectExpanded(obj);
}

void CVisualEditorNotebook::CVisualEditor::RemoveObject(IValueFrame* obj)
{
	DoRemoveObject(obj, false);
}

void CVisualEditorNotebook::CVisualEditor::CutObject(IValueFrame* obj, bool force)
{
	DoRemoveObject(obj, true, force);
}

bool CVisualEditorNotebook::CVisualEditor::SelectObject(IValueFrame* obj, bool force, bool notify)
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

void CVisualEditorNotebook::CVisualEditor::MovePosition(IValueFrame* obj, unsigned int toPos)
{
	Execute(new ShiftChildCmd(this, obj, toPos));
	NotifyProjectRefresh();
	SelectObject(obj, true);
}

void CVisualEditorNotebook::CVisualEditor::MovePosition(IValueFrame* obj, bool right, unsigned int num)
{
	IValueFrame* noItemObj = obj;
	IValueFrame* parent = obj->GetParent();

	if (parent) {
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
			Execute(new ShiftChildCmd(this, obj, pos));
			NotifyProjectRefresh();
			SelectObject(noItemObj, true);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditor::ModifyProperty(Property* prop, const wxVariant& oldValue, const wxVariant& newValue)
{
	IPropertyObject* object = prop->GetObject();
	if (oldValue != newValue) {
		Execute(new ModifyPropertyCmd(this, prop, oldValue, newValue));
		NotifyPropertyModified(prop);
	}
}

void CVisualEditorNotebook::CVisualEditor::ModifyEvent(Event* evt, const wxVariant& oldValue, const wxVariant& newValue)
{
	IPropertyObject* object = evt->GetObject();
	if (oldValue != newValue) {
		Execute(new ModifyEventCmd(this, evt, oldValue, newValue));
		NotifyEventModified(evt);
	}
}

/////////////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditor::PropagateExpansion(IValueFrame* obj, bool expand, bool up)
{
	if (obj != nullptr) {
		if (up) {
			IValueFrame* child = nullptr;
			for (unsigned int i = 0; i < obj->GetChildCount(); i++) {
				child = obj->GetChild(i);
				Execute(new ExpandObjectCmd(this, child, expand));
				PropagateExpansion(child, expand, up);
			}
		}
		else
		{
			PropagateExpansion(obj->GetParent(), expand, up);
			Execute(new ExpandObjectCmd(this, obj, expand));
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditor::DoRemoveObject(IValueFrame* obj, bool cutObject, bool force)
{
	// Note:
	//  When removing an object it is important that the "item" objects
	// are not left behind
	IValueFrame* parent = obj->GetParent();
	IValueFrame* deleted_obj = obj;

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
			m_copyOnPaste = false;
			Execute(new CutObjectCmd(this, obj, force));
		}
		else {
			Execute(new RemoveObjectCmd(this, obj));
		}

		SelectObject(GetSelectedObject(), true, true);
	}
	else {
		if (obj->GetObjectTypeName() != wxT("form")) {
			assert(false);
		}
	}
}

void CVisualEditorNotebook::CVisualEditor::DetermineObjectToSelect(IValueFrame* parent, unsigned int pos)
{
	// get position of next control or last control
	IValueFrame* objToSelect = nullptr;
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

void CVisualEditorNotebook::CVisualEditor::Undo()
{
	m_cmdProc->Undo();
	m_document->Modify(!m_cmdProc->IsAtSavePoint());
	NotifyProjectRefresh();
	NotifyObjectSelected(GetSelectedObject());
}

void CVisualEditorNotebook::CVisualEditor::Redo()
{
	m_cmdProc->Redo();
	m_document->Modify(!m_cmdProc->IsAtSavePoint());
	NotifyProjectRefresh();
	NotifyObjectSelected(GetSelectedObject());
}

bool CVisualEditorNotebook::CVisualEditor::CanPasteObject() const
{
	IValueFrame* obj = GetSelectedObject();

	if (obj)
		return (m_clipboard != nullptr);

	return false;
}

bool CVisualEditorNotebook::CVisualEditor::CanCopyObject() const
{
	IValueFrame* obj = GetSelectedObject();

	if (obj && obj->GetObjectTypeName() != wxT("form"))
		return true;

	return false;
}

bool CVisualEditorNotebook::CVisualEditor::IsModified() const {
	return m_document->IsModified();
}

int CVisualEditorNotebook::CVisualEditor::CalcPositionOfInsertion(IValueFrame* selected, IValueFrame* parent)
{
	int pos = wxNOT_FOUND;
	if (parent && selected) {
		IValueFrame* parentSelected = selected->GetParent();
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

/////////////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditor::ToggleBorderFlag(IValueFrame* obj, int border)
{
	if (!obj)
		return;
	IValueFrame* parent = obj->GetParent();
	if (!parent)
		return;
	if (!parent->IsSubclassOf(wxT("sizerItem")))
		return;
	Property* propFlag = parent->GetProperty(wxT("flag"));
	if (!propFlag)
		return;

	wxString value = propFlag->GetValueAsString();

	value = typeConv::ClearFlag(wxT("wxALL"), value);
	value = typeConv::ClearFlag(wxT("wxTOP"), value);
	value = typeConv::ClearFlag(wxT("wxBOTTOM"), value);
	value = typeConv::ClearFlag(wxT("wxRIGHT"), value);
	value = typeConv::ClearFlag(wxT("wxLEFT"), value);

	int intVal = propFlag->GetValueAsInteger();
	intVal ^= border;

	if ((intVal & wxALL) == wxALL) {
		value = typeConv::SetFlag(wxT("wxALL"), value);
	}
	else {
		if ((intVal & wxTOP) != 0) value = typeConv::SetFlag(wxT("wxTOP"), value);
		if ((intVal & wxBOTTOM) != 0) value = typeConv::SetFlag(wxT("wxBOTTOM"), value);
		if ((intVal & wxRIGHT) != 0) value = typeConv::SetFlag(wxT("wxRIGHT"), value);
		if ((intVal & wxLEFT) != 0) value = typeConv::SetFlag(wxT("wxLEFT"), value);
	}

	ModifyProperty(propFlag, propFlag->GetValueAsString(), value);
}

void CVisualEditorNotebook::CVisualEditor::CreateBoxSizerWithObject(IValueFrame* obj)
{
	IValueFrame* parent = obj->GetParent();
	if (!parent)
		return;
	IValueFrame* grandParent = parent->GetParent();
	if (!grandParent)
		return;
	int childPos = -1;
	if (parent->IsSubclassOf(wxT("sizerItem"))) {
		childPos = (int)grandParent->GetChildPosition(parent);
		parent = grandParent;
	}

	wxASSERT(m_valueForm);

	// Must first cut the old object in case it is the only allowable object
	IValueFrame* clipboard = m_clipboard;

	CutObject(obj);

	// Create the wxBoxSizer
	IValueFrame* newSizer = m_valueForm->CreateObject(wxT("boxSizer"), parent);

	if (newSizer) {
		Execute(new InsertObjectCmd(this, newSizer, parent, childPos));
		if (newSizer->GetObjectTypeName() == wxT("sizerItem"))
			newSizer = newSizer->GetChild(0);
		PasteObject(newSizer);
		m_clipboard = clipboard;
		NotifyProjectRefresh();
	}
	else {
		Undo();
		m_clipboard = clipboard;
	}
}
