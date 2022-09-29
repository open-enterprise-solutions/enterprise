#include "form.h"

#include "frontend/visualView/controls/form.h"
#include "frontend/visualView/controls/sizers.h"
#include "frontend/visualView/controls/widgets.h"
#include "frontend/visualView/controls/toolbar.h"
#include "frontend/visualView/controls/notebook.h"

#include "frontend/visualView/controls/tableBox.h"
#include "frontend/visualView/controls/gridbox.h"
#include "frontend/visualView/controls/htmlbox.h"
#include "frontend/visualView/controls/chartBox.h"

inline wxString GetClassType(const wxString& className)
{
	IControlValueAbstract* objectSingle =
		dynamic_cast<IControlValueAbstract*>(CValue::GetAvailableObject(className));
	wxASSERT(objectSingle);
	return objectSingle->GetControlType();
}

inline void SetDefaultLayoutProperties(CValueSizerItem* sizerItem)
{
	IValueFrame* child = sizerItem->GetChild(0);
	wxString obj_type = child->GetObjectTypeName();

	if (obj_type == wxT("sizer"))
	{
		sizerItem->SetBorder(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("splitter") ||
		child->GetClassName() == wxT("spacer"))
	{
		sizerItem->SetProportion(1);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("staticline"))
	{
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("toolbar"))
	{
		sizerItem->SetBorder(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("notebook"))
	{
		sizerItem->SetProportion(1);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (obj_type == wxT("widget") ||
		obj_type == wxT("statusbar"))
	{
		sizerItem->SetProportion(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxSHRINK);
	}
	else if (obj_type == wxT("container"))
	{
		sizerItem->SetProportion(1);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
}

IValueFrame* CValueForm::NewObject(const wxString& classControl, IValueFrame* controlParent, bool generateId)
{
	IValueFrame* newControl =
		CValue::CreateAndConvertObjectRef< IValueFrame>(classControl);
	wxASSERT(newControl);
	;
	if (controlParent != NULL) {
		controlParent->AddChild(newControl);
		newControl->SetParent(controlParent);
	}

	wxASSERT(newControl);

	newControl->SetOwnerForm(this);
	newControl->SetClsid(
		CValue::GetIDObjectFromString(classControl)
	);

	CValueForm::ResolveNameConflict(newControl);

	if (generateId) {
		newControl->GenerateGuid();
		newControl->GenerateNewID();
	}

	if (!newControl->Init()) {
		controlParent->RemoveChild(newControl);
		newControl->SetParent(NULL);
		wxDELETE(newControl);
		return NULL;
	}

	newControl->IncrRef();
	return newControl;
}

void CValueForm::ResolveNameConflict(IValueFrame* control)
{
	// Save the original name for use later.
	wxString originalName =
		control->GetControlName();

	if (originalName.IsEmpty()) {
		originalName = control->GetClassName();
	}

	// el nombre no puede estar repetido dentro del mismo form
	IValueFrame* top = control->GetOwnerForm();
	wxASSERT(top);

	// construimos el conjunto de nombres
	std::set<wxString> name_set;

	std::function<void(IValueFrame*, IValueFrame*, std::set< wxString >&)> buildNameSet = [&buildNameSet](IValueFrame* object, IValueFrame* top, std::set< wxString >& name_set) {
		wxASSERT(object);
		if (object != top) {
			name_set.emplace(top->GetControlName());
		}
		for (unsigned int i = 0; i < top->GetChildCount(); i++) {
			buildNameSet(object, top->GetChild(i), name_set);
		}
	};

	buildNameSet(control, top, name_set);

	// comprobamos si hay conflicto
	std::set<wxString>::iterator it =
		name_set.find(originalName);

	int i = 0;

	wxString name = originalName; // The name that gets incremented.

	while (it != name_set.end()) {
		i++;
		name = wxString::Format(wxT("%s%i"), originalName.c_str(), i);
		it = name_set.find(name);
	}

	control->SetControlName(name);
}

IValueFrame* CValueForm::CreateObject(const wxString& className, IValueFrame* controlParent)
{
	IValueFrame* object = NULL;
	wxString classType = ::GetClassType(className);

	if (controlParent) {
		bool bSizer = false;

		if (classType == wxT("form")) bSizer = true;
		else if (classType == wxT("sizer")) bSizer = controlParent->GetObjectTypeName() == wxT("sizer") || controlParent->GetObjectTypeName() == wxT("form") ? false : true;

		//FIXME! Esto es un parche para evitar crear los tipos menubar,statusbar y
		//toolbar en un form que no sea wxFrame.
		//Hay que modificar el conjunto de tipos para permitir tener varios tipos
		//de forms (como childType de project), pero hay mucho código no válido
		//para forms que no sean de tipo "form". Dicho de otra manera, hay
		//código que dependen del nombre del tipo, cosa que hay que evitar.
		if (controlParent->GetObjectTypeName() == wxT("form") && controlParent->GetClassName() != wxT("form") &&
			(classType == wxT("statusbar") ||
				classType == wxT("menubar") ||
				classType == wxT("ribbonbar") ||
				classType == wxT("toolbar")))

			return NULL; // tipo no válido

		// No menu dropdown for wxToolBar until wx 2.9 :(
		if (controlParent->GetObjectTypeName() == wxT("tool"))
		{
			IValueFrame* gParent = controlParent->GetParent();

			if (
				(gParent->GetClassName() == wxT("toolbar")) &&
				(className == wxT("menu"))
				)
				return NULL; // not a valid type
		}

		if (controlParent->GetObjectTypeName() == wxT("toolbar"))
		{
			if (classType == wxT("tool") /*|| classType == wxT("widget")*/)
			{
				object = NewObject(className, controlParent);
				//set enabled item
				object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == wxT("notebook"))
		{
			if (classType == wxT("page")) {
				object = NewObject(className, controlParent);
				//set enabled item
				object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == wxT("container")
			&& controlParent->GetClassName() == wxT("tablebox"))
		{
			if (classType == wxT("tableboxColumn"))
			{
				object = NewObject(className, controlParent);
				//set enabled item
				object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == wxT("page"))
		{
			CValueSizerItem* sizerItem = NewObject<CValueSizerItem>("sizerItem", controlParent);
			IValueFrame* obj = NewObject(className, sizerItem);

			if (controlParent) {
				sizerItem->SetReadOnly(controlParent->IsEditable());
			}

			// la siguiente condición debe cumplirse siempre
			// ya que un item debe siempre contener a otro objeto
			if (obj) {
				//set enabled item
				obj->SetReadOnly(sizerItem->IsEditable());

				// sizerItem es un tipo de objeto reservado, para que el uso sea
				// más práctico se asignan unos valores por defecto en función
				// del tipo de objeto creado
				if (sizerItem->IsSubclassOf(wxT("sizerItem"))) {
					SetDefaultLayoutProperties(sizerItem);
				}

				object = sizerItem;
			}
		}
		else if (controlParent->GetComponentType() == COMPONENT_TYPE_FRAME ||
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZER ||
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		{
			CValueSizerItem* sizerItem = NewObject< CValueSizerItem>("sizerItem", controlParent);
			IValueFrame* obj = NewObject(className, sizerItem);

			if (controlParent) {
				sizerItem->SetReadOnly(controlParent->IsEditable());
			}

			// la siguiente condición debe cumplirse siempre
			// ya que un item debe siempre contener a otro objeto
			if (obj) {
				//set enabled item
				obj->SetReadOnly(sizerItem->IsEditable());
				// sizerItem es un tipo de objeto reservado, para que el uso sea
				// más práctico se asignan unos valores por defecto en función
				// del tipo de objeto creado
				if (sizerItem->IsSubclassOf(wxT("sizerItem"))) {
					SetDefaultLayoutProperties(sizerItem);
				}

				object = sizerItem;
			}
		}
	}
	else // controlParent == NULL;
	{
		object = NewObject(className, NULL);
	}

	return object;
}

IValueFrame* CValueForm::CopyObject(IValueFrame* srcControl)
{
	if (srcControl->GetComponentType() == COMPONENT_TYPE_FRAME)
		return NULL; 

	assert(srcControl);
	IValueFrame* copyObj = NewObject(srcControl->GetClassName(), NULL, false); // creamos la copia
	assert(copyObj);

	// copiamos las propiedades
	unsigned int i;
	unsigned int count = srcControl->GetPropertyCount();
	for (i = 0; i < count; i++) {
		Property* objProp = srcControl->GetProperty(i);
		assert(objProp);

		if (objProp->GetName() == wxT("name"))
			continue;

		Property* copyProp = copyObj->GetProperty(objProp->GetName());
		assert(copyProp);

		wxString propValue = objProp->GetValue();
		copyProp->SetValue(propValue);
	}

	// ...and the event handlers
	count = srcControl->GetEventCount();
	for (i = 0; i < count; i++) {
		Event* event = srcControl->GetEvent(i);
		Event* copyEvent = copyObj->GetEvent(event->GetName());
		copyEvent->SetValue(event->GetValue());
	}

	copyObj->SetParent(srcControl->GetParent());

	copyObj->GenerateGuid();
	copyObj->GenerateNewID();

	// creamos recursivamente los hijos
	count = srcControl->GetChildCount();
	for (i = 0; i < count; i++) {
		IValueFrame* childCopy =
			CopyObject(srcControl->GetChild(i));
		wxASSERT(childCopy);
		if (copyObj) {
			copyObj->AddChild(childCopy);
			childCopy->SetParent(copyObj);
		}
	}

	return copyObj;
}

/////////////////////////////////////////////////////////////////////////////////

#include "frontend/visualView/controls/icons/icons.h"

//frame 
S_CONTROL_VALUE_REGISTER(CValueForm, "form", "form", form_xpm, g_controlFormCLSID);
//sizer 
S_CONTROL_VALUE_REGISTER(CValueSizerItem, "sizerItem", "sizer", NULL, TEXT2CLSID("CT_SIZR"));
CONTROL_VALUE_REGISTER(CValueBoxSizer, "boxsizer", "sizer", sizer_2_xpm, TEXT2CLSID("CT_BSZR"));
CONTROL_VALUE_REGISTER(CValueStaticBoxSizer, "staticboxsizer", "sizer", staticbox_sizer_xpm, TEXT2CLSID("CT_SSZER"));
CONTROL_VALUE_REGISTER(CValueGridSizer, "gridsizer", "sizer", grid_sizer_xpm, TEXT2CLSID("CT_GSZR"));
//widgets  
CONTROL_VALUE_REGISTER(CValueButton, "button", "widget", button_xpm, TEXT2CLSID("CT_BUTN"));
CONTROL_VALUE_REGISTER(CValueStaticText, "statictext", "widget", static_text_xpm, TEXT2CLSID("CT_STTX"));
CONTROL_VALUE_REGISTER(CValueTextCtrl, "textctrl", "widget", text_ctrl_xpm, TEXT2CLSID("CT_TXTC"));
CONTROL_VALUE_REGISTER(CValueCheckbox, "checkbox", "widget", checkbox_xpm, TEXT2CLSID("CT_CHKB"));
CONTROL_VALUE_REGISTER(CValueRadioButton, "radiobutton", "widget", radio_button_xpm, TEXT2CLSID("CT_RDBT"));
CONTROL_VALUE_REGISTER(CValueStaticLine, "staticline", "widget", static_line_xpm, TEXT2CLSID("CT_STLI"));
CONTROL_VALUE_REGISTER(CValueSlider, "slider", "widget", slider_xpm, TEXT2CLSID("CT_SLID"));
CONTROL_VALUE_REGISTER(CValueGauge, "gauge", "widget", gauge_xpm, TEXT2CLSID("CT_GAUG"));
//toolbar
CONTROL_VALUE_REGISTER(CValueToolbar, "toolbar", "toolbar", toolbar_xpm, TEXT2CLSID("CT_TLBR"));
S_CONTROL_VALUE_REGISTER(CValueToolBarItem, "tool", "tool", tool_xpm, TEXT2CLSID("CT_TLIT"));
S_CONTROL_VALUE_REGISTER(CValueToolBarSeparator, "toolSeparator", "tool", toolseparator_xpm, TEXT2CLSID("CT_TLIS"));
//notebook
CONTROL_VALUE_REGISTER(CValueNotebook, "notebook", "notebook", notebook_xpm, TEXT2CLSID("CT_NTBK"));
S_CONTROL_VALUE_REGISTER(CValueNotebookPage, "page", "page", page_xpm, TEXT2CLSID("CT_NTPG"));
//tablebox
CONTROL_VALUE_REGISTER(CValueTableBox, "tablebox", "tablebox", dataviewlist_ctrl_xpm, TEXT2CLSID("CT_TABL"));
S_CONTROL_VALUE_REGISTER(CValueTableBoxColumn, "tableboxColumn", "tableboxColumn", dataviewlist_column_xpm, TEXT2CLSID("CT_TBLC"));
//grid 
CONTROL_VALUE_REGISTER(CValueGridBox, "gridbox", "gridbox", grid_xpm, TEXT2CLSID("CT_GRID"));
//html 
CONTROL_VALUE_REGISTER(CValueHTMLBox, "htmlbox", "htmlbox", htmlwin_xpm, TEXT2CLSID("CT_HTML"));
//chart 
CONTROL_VALUE_REGISTER(CValueChartBox, "chartbox", "chartbox", static_bitmap_xpm, TEXT2CLSID("CT_CHRB"));
