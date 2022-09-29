////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "frontend/mainFrame.h"
#include "frontend/objinspect/objinspect.h"

#include "common/propertyObject.h"
#include "utils/stringUtils.h"
#include "utils/typeconv.h"

///////////////////////////////////////////////////////////////////////////////
// Comandos
///////////////////////////////////////////////////////////////////////////////

class BaseVisualCmd : public CCommand
{
public:

	BaseVisualCmd(CVisualEditorContextForm* visualData = NULL) : m_visualData(visualData) {}

	virtual void Execute() { CCommand::Execute(); }
	virtual void Restore() { CCommand::Restore(); }

protected:

	CVisualEditorContextForm* m_visualData;
};

/** Command for expanding an object in the object tree */

class ExpandObjectCmd : public BaseVisualCmd
{
	IValueFrame* m_object = NULL;
	bool m_expand;

public:

	ExpandObjectCmd(CVisualEditorContextForm* data, IValueFrame* object, bool expand);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* Comando para insertar un objeto en el arbol.
*/

class InsertObjectCmd : public BaseVisualCmd
{
	IValueFrame* m_parent = NULL;
	IValueFrame* m_object = NULL;
	int m_pos;
	IValueFrame* m_oldSelected;
	bool m_firstCreated;

public:

	InsertObjectCmd(CVisualEditorContextForm* data, IValueFrame* object, IValueFrame* parent, int pos = -1, bool firstCreated = true);

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
	IValueFrame* m_parent = NULL;
	IValueFrame* m_object = NULL;
	int m_oldPos;
	IValueFrame* m_oldSelected = NULL;

public:

	RemoveObjectCmd(CVisualEditorContextForm* data, IValueFrame* object);
	~RemoveObjectCmd();

protected:

	void GenerateId();
	void ResetId();

	void OnObjectRemoved(wxFrameObjectEvent& event);

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

	ModifyPropertyCmd(CVisualEditorContextForm* data, Property* prop, const wxVariant& newValue);

protected:
	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* BaseVisualCmd for modifying an event
*/

class ModifyEventHandlerCmd : public BaseVisualCmd
{
	Event* m_event = NULL;
	wxVariant m_oldValue, m_newValue;

public:
	ModifyEventHandlerCmd(CVisualEditorContextForm* data, Event* event, const wxString& value);

protected:
	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

/**
* Comando para mover de posicion un objeto.
*/

class ShiftChildCmd : public BaseVisualCmd
{
	IValueFrame* m_object = NULL;
	int m_oldPos, m_newPos;

public:
	ShiftChildCmd(CVisualEditorContextForm* data, IValueFrame* object, int pos);

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
	IValueFrame* m_parent = NULL;
	IValueFrame* m_object = NULL;
	int m_oldPos;
	IValueFrame* m_oldSelected = NULL;

	bool m_needEvent;

public:

	CutObjectCmd(CVisualEditorContextForm* data, IValueFrame* object, bool force);
	~CutObjectCmd();

protected:

	void GenerateId();
	void ResetId();

	void OnObjectRemoved(wxFrameObjectEvent& event);

	virtual void DoExecute() override;
	virtual void DoRestore() override;
};

///////////////////////////////////////////////////////////////////////////////
// Implementacion de los Comandos
///////////////////////////////////////////////////////////////////////////////

ExpandObjectCmd::ExpandObjectCmd(CVisualEditorContextForm* data, IValueFrame* object, bool expand) : BaseVisualCmd(data),
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

InsertObjectCmd::InsertObjectCmd(CVisualEditorContextForm* data, IValueFrame* object, IValueFrame* parent, int pos, bool firstCreated) : BaseVisualCmd(data),
m_parent(parent), m_object(object), m_pos(pos), m_firstCreated(firstCreated)
{
	m_oldSelected = data->GetSelectedObject();

	if (m_parent) {
		m_parent->RemoveChild(m_object);
		m_object->SetParent(NULL);
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
	CVisualEditorContextForm::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();
	wxASSERT(visulEditor);
	visulEditor->CreateControl(m_object, NULL, m_firstCreated);

	//select object
	m_visualData->SelectObject(obj, false, false);
}

void InsertObjectCmd::DoRestore()
{
	//remove control in visual editor
	CVisualEditorContextForm::CVisualEditorHost* m_visualEditor =
		m_visualData->GetVisualEditor();

	m_visualEditor->RemoveControl(m_object);

	m_parent->RemoveChild(m_object);
	m_object->SetParent(NULL);

	ResetId();

	m_visualData->SelectObject(m_oldSelected);
}

//-----------------------------------------------------------------------------

RemoveObjectCmd::RemoveObjectCmd(CVisualEditorContextForm* data, IValueFrame* object) : BaseVisualCmd(data)
{
	m_object = object;
	m_parent = object->GetParent();
	m_oldPos = m_parent->GetChildPosition(object);
	m_oldSelected = data->GetSelectedObject();

	wxEvtHandler::Connect(wxEVT_OBJECT_REMOVED, wxFrameObjectEventHandler(RemoveObjectCmd::OnObjectRemoved), NULL, this);
}

RemoveObjectCmd::~RemoveObjectCmd()
{
	wxEvtHandler::Disconnect(wxEVT_OBJECT_REMOVED, wxFrameObjectEventHandler(RemoveObjectCmd::OnObjectRemoved), NULL, this);
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

void RemoveObjectCmd::OnObjectRemoved(wxFrameObjectEvent& event)
{
	//remove control in visual editor
	CVisualEditorContextForm::CVisualEditorHost* visulEditor = m_visualData->GetVisualEditor();

	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	visulEditor->RemoveControl(m_object);

	m_parent->RemoveChild(m_object);
	m_object->SetParent(NULL);
}

void RemoveObjectCmd::DoExecute()
{
	IValueFrame* obj = m_object;
	while (obj && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			obj = obj->GetChild(0);
		}
		else return;
	}

	if (m_object->GetParent() != NULL) {
		m_parent->RemoveChild(m_object);
		m_object->SetParent(NULL);
	}

	ResetId();

	m_visualData->DetermineObjectToSelect(m_parent, m_oldPos);

	wxFrameObjectEvent event(wxEVT_OBJECT_REMOVED, m_object);
	wxEvtHandler::AddPendingEvent(event);
}

void RemoveObjectCmd::DoRestore()
{
	if (m_object->GetParent() == NULL) {
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
	CVisualEditorContextForm::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();
	visulEditor->CreateControl(m_object);
}

//-----------------------------------------------------------------------------

ModifyPropertyCmd::ModifyPropertyCmd(CVisualEditorContextForm* data, Property* prop, const wxVariant& newValue) : BaseVisualCmd(data),
m_property(prop), m_oldValue(prop->GetValue()), m_newValue(newValue)
{
}

void ModifyPropertyCmd::DoExecute()
{
	CVisualEditorContextForm::CVisualEditorHost* visulEditor = m_visualData->GetVisualEditor();
	// Get the IValueFrame from the event
	IValueFrame* m_object = dynamic_cast<IValueFrame*>(m_property->GetObject());
	m_property->SetValue(m_newValue);

	if (m_object->GetClsid() == g_controlFormCLSID) {
		visulEditor->UpdateVisualEditor();
	}
	else {
		visulEditor->UpdateControl(m_object);
	}

	//objectInspector->RefreshProperty();
}

void ModifyPropertyCmd::DoRestore()
{
	CVisualEditorContextForm::CVisualEditorHost* visulEditor = m_visualData->GetVisualEditor();
	// Get the IValueFrame from the event
	IValueFrame* m_object = dynamic_cast<IValueFrame*>(m_property->GetObject());

	m_property->SetValue(m_oldValue);

	if (m_object->GetClsid() == g_controlFormCLSID) {
		visulEditor->UpdateVisualEditor();
	}
	else {
		visulEditor->UpdateControl(m_object);
	}

	objectInspector->RefreshProperty();
}

//-----------------------------------------------------------------------------

ModifyEventHandlerCmd::ModifyEventHandlerCmd(CVisualEditorContextForm* data, Event* event, const wxString& value) : BaseVisualCmd(data),
m_event(event), m_newValue(value)
{
	m_oldValue = event->GetValue();
}

void ModifyEventHandlerCmd::DoExecute()
{
	m_event->SetValue(m_newValue);
}

void ModifyEventHandlerCmd::DoRestore()
{
	m_event->SetValue(m_oldValue);
}

//-----------------------------------------------------------------------------

ShiftChildCmd::ShiftChildCmd(CVisualEditorContextForm* data, IValueFrame* object, int pos) : BaseVisualCmd(data)
{
	m_object = object;
	IValueFrame* parent = object->GetParent();

	assert(parent);

	m_oldPos = parent->GetChildPosition(object);
	m_newPos = pos;
}

void ShiftChildCmd::DoExecute()
{
	CVisualEditorContextForm::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	if (m_oldPos != m_newPos) {
		IValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object, m_newPos);

		visulEditor->UpdateControl(m_object);
	}
}

void ShiftChildCmd::DoRestore()
{
	CVisualEditorContextForm::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	if (m_oldPos != m_newPos) {
		IValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object, m_oldPos);

		visulEditor->UpdateControl(m_object);
	}
}

//-----------------------------------------------------------------------------

CutObjectCmd::CutObjectCmd(CVisualEditorContextForm* data, IValueFrame* object, bool force) : BaseVisualCmd(data), m_needEvent(!force)
{
	m_object = object;
	m_parent = object->GetParent();
	m_oldPos = m_parent->GetChildPosition(object);
	m_oldSelected = data->GetSelectedObject();

	if (m_needEvent) {
		wxEvtHandler::Connect(wxEVT_OBJECT_REMOVED, wxFrameObjectEventHandler(CutObjectCmd::OnObjectRemoved), NULL, this);
	}
}

CutObjectCmd::~CutObjectCmd()
{
	if (m_needEvent) {
		wxEvtHandler::Disconnect(wxEVT_OBJECT_REMOVED, wxFrameObjectEventHandler(CutObjectCmd::OnObjectRemoved), NULL, this);
	}
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

void CutObjectCmd::OnObjectRemoved(wxFrameObjectEvent& event)
{
	//remove control in visual editor
	CVisualEditorContextForm::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	m_parent->AddChild(m_object);
	m_object->SetParent(m_parent);

	visulEditor->RemoveControl(m_object);

	//remove control in visual editor
	m_parent->RemoveChild(m_object);
	m_object->SetParent(NULL);
}

void CutObjectCmd::DoExecute()
{
	m_visualData->SetClipboardObject(m_object);

	if (!m_needEvent) {
		//remove control in visual editor
		CVisualEditorContextForm::CVisualEditorHost* visulEditor =
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
	m_object->SetParent(NULL);

	//determine object to select
	m_visualData->DetermineObjectToSelect(m_parent, m_oldPos);

	//reset id, guid
	ResetId();

	if (m_needEvent) {
		wxFrameObjectEvent event(wxEVT_OBJECT_REMOVED, m_object);
		wxEvtHandler::AddPendingEvent(event);
	}
}

void CutObjectCmd::DoRestore()
{
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
	m_visualData->SetClipboardObject(NULL);
	m_visualData->SelectObject(m_oldSelected, true, false);

	//create control in visual editor
	CVisualEditorContextForm::CVisualEditorHost* visulEditor =
		m_visualData->GetVisualEditor();

	visulEditor->CreateControl(m_object, m_parent);
}

//-----------------------------------------------------------------------------

IValueFrame* CVisualEditorContextForm::CreateObject(const wxString& name)
{
	IValueFrame* obj = NULL;
	wxASSERT(m_valueForm);
	try
	{
		//LogDebug("[ApplicationData::CreateObject] New " + name );
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
			obj = (obj->GetChildCount() > 0 ? obj->GetChild(0) : NULL);

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

void CVisualEditorContextForm::InsertObject(IValueFrame* obj, IValueFrame* parent)
{
	Execute(new InsertObjectCmd(this, obj, parent));
	NotifyObjectCreated(obj);
}

void CVisualEditorContextForm::CopyObject(IValueFrame* obj)
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

bool CVisualEditorContextForm::PasteObject(IValueFrame* dstObject, IValueFrame* objToPaste)
{
	wxASSERT(m_valueForm);

	try {
		IValueFrame* clipboard = NULL;
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
			clipboard->SetParent(NULL);
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

		IValueFrame* obj = NULL;
		while (obj == NULL) {
			obj = m_valueForm->CreateObject(_STDSTR(clipboard->GetClassName()), parentObject);
			if (obj != NULL) {
				if (parentObject) {
					obj->SetParent(NULL);
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
			if (parentObject == NULL)
				break;
		}

		if (!obj) {
			return false;
		}

		IValueFrame* aux = obj;

		while (aux && aux != clipboard)
			aux = (aux->GetChildCount() > 0 ? aux->GetChild(0) : NULL);

		int pos = CalcPositionOfInsertion(dstObject, parentObject);

		if (aux && aux != obj) {
			// sustituimos aux por clipboard
			IValueFrame* auxParent = aux->GetParent();
			auxParent->RemoveChild(aux);
			aux->SetParent(NULL);

			auxParent->AddChild(clipboard);
			clipboard->SetParent(auxParent);
		}
		else {
			obj = clipboard;
		}

		// y finalmente insertamos en el arbol
		Execute(new InsertObjectCmd(this, obj, parentObject, pos));

		if (!m_copyOnPaste)
			m_clipboard = NULL;

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

IValueFrame* CVisualEditorContextForm::SearchSizerInto(IValueFrame* obj)
{
	IValueFrame* theSizer = NULL;

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

void CVisualEditorContextForm::ExpandObject(IValueFrame* obj, bool expand)
{
	Execute(new ExpandObjectCmd(this, obj, expand));

	// collapse also all children ...
	PropagateExpansion(obj, expand, !expand);
	NotifyObjectExpanded(obj);
}

void CVisualEditorContextForm::RemoveObject(IValueFrame* obj)
{
	DoRemoveObject(obj, false);
}

void CVisualEditorContextForm::CutObject(IValueFrame* obj, bool force)
{
	DoRemoveObject(obj, true, force);
}

bool CVisualEditorContextForm::SelectObject(IValueFrame* obj, bool force, bool notify)
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

void CVisualEditorContextForm::MovePosition(IValueFrame* obj, unsigned int toPos)
{
	Execute(new ShiftChildCmd(this, obj, toPos));
	NotifyProjectRefresh();
	SelectObject(obj, true);
}

void CVisualEditorContextForm::MovePosition(IValueFrame* obj, bool right, unsigned int num)
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

void CVisualEditorContextForm::ModifyProperty(Property* prop, const wxVariant& newValue)
{
	IPropertyObject* object = prop->GetObject();
	if (newValue != prop->GetValue()) {
		Execute(new ModifyPropertyCmd(this, prop, newValue));
		NotifyPropertyModified(prop);
	}
}

void CVisualEditorContextForm::ModifyEventHandler(Event* evt, const wxVariant& value)
{
	IPropertyObject* object = evt->GetObject();
	if (value != evt->GetValue()) {
		Execute(new ModifyEventHandlerCmd(this, evt, value));
		NotifyEventModified(evt);
	}
}

/////////////////////////////////////////////////////////////////////////////////////

void CVisualEditorContextForm::PropagateExpansion(IValueFrame* obj, bool expand, bool up)
{
	if (obj) {
		if (up) {
			IValueFrame* child = NULL;
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

bool CVisualEditorContextForm::IsCorrectName(const wxString& controlName) const
{
	wxASSERT(m_valueForm);
	return m_valueForm->FindControlByName(controlName) == NULL;
}

/////////////////////////////////////////////////////////////////////////////////////

void CVisualEditorContextForm::DoRemoveObject(IValueFrame* obj, bool cutObject, bool force)
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

void CVisualEditorContextForm::DetermineObjectToSelect(IValueFrame* parent, unsigned int pos)
{
	// get position of next control or last control
	IValueFrame* objToSelect = NULL;
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

void CVisualEditorContextForm::Undo()
{
	m_cmdProc->Undo();
	m_document->Modify(!m_cmdProc->IsAtSavePoint());
	NotifyProjectRefresh();
	NotifyObjectSelected(GetSelectedObject());
}

void CVisualEditorContextForm::Redo()
{
	m_cmdProc->Redo();
	m_document->Modify(!m_cmdProc->IsAtSavePoint());
	NotifyProjectRefresh();
	NotifyObjectSelected(GetSelectedObject());
}

bool CVisualEditorContextForm::CanPasteObject() const
{
	IValueFrame* obj = GetSelectedObject();

	if (obj)
		return (m_clipboard != NULL);

	return false;
}

bool CVisualEditorContextForm::CanCopyObject() const
{
	IValueFrame* obj = GetSelectedObject();

	if (obj && obj->GetObjectTypeName() != wxT("form"))
		return true;

	return false;
}

bool CVisualEditorContextForm::IsModified() const {
	return m_document->IsModified();
}

int CVisualEditorContextForm::CalcPositionOfInsertion(IValueFrame* selected, IValueFrame* parent)
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

void CVisualEditorContextForm::ToggleBorderFlag(IValueFrame* obj, int border)
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

	value = TypeConv::ClearFlag(wxT("wxALL"), value);
	value = TypeConv::ClearFlag(wxT("wxTOP"), value);
	value = TypeConv::ClearFlag(wxT("wxBOTTOM"), value);
	value = TypeConv::ClearFlag(wxT("wxRIGHT"), value);
	value = TypeConv::ClearFlag(wxT("wxLEFT"), value);

	int intVal = propFlag->GetValueAsInteger();
	intVal ^= border;

	if ((intVal & wxALL) == wxALL) {
		value = TypeConv::SetFlag(wxT("wxALL"), value);
	}
	else {
		if ((intVal & wxTOP) != 0) value = TypeConv::SetFlag(wxT("wxTOP"), value);
		if ((intVal & wxBOTTOM) != 0) value = TypeConv::SetFlag(wxT("wxBOTTOM"), value);
		if ((intVal & wxRIGHT) != 0) value = TypeConv::SetFlag(wxT("wxRIGHT"), value);
		if ((intVal & wxLEFT) != 0) value = TypeConv::SetFlag(wxT("wxLEFT"), value);
	}

	ModifyProperty(propFlag, value);
}

void CVisualEditorContextForm::CreateBoxSizerWithObject(IValueFrame* obj)
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
