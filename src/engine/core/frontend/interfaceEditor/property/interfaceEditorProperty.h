#ifndef _INTERFACE_EDITOR_PROPERTY_H_
#define _INTERFACE_EDITOR_PROPERTY_H_

#include "common/propertyObject.h"

enum eMenuType {
	eMenu,
	eSubMenu,
	eSeparator
};

class CInterfaceEditorProperty : public IPropertyObject {

	OptionList* GetMenuType(PropertyOption*) {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("menu"), eMenu); 
		optionlist->AddOption(_("sub menu"), eSubMenu);
		optionlist->AddOption(_("separator"), eSeparator);
		return optionlist;
	}

protected:

	PropertyCategory* m_interfaceCategory = IPropertyObject::CreatePropertyCategory("Interface");
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_interfaceCategory, "caption", PropertyType::PT_TEXT);
	Property* m_propertyAction = IPropertyObject::CreateProperty(m_interfaceCategory, "menuType", &CInterfaceEditorProperty::GetMenuType, eMenuType::eMenu);

public:

	void SetCaption(const wxString &caption) {
		m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	CInterfaceEditorProperty();
	virtual ~CInterfaceEditorProperty();

	//system override 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_ABSTRACT;
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("interfaceEditor");
	}

	virtual wxString GetClassName() const override {
		return wxT("interfaceEditor";)
	}
};

#endif 