#ifndef _INTERFACE_EDITOR_PROPERTY_H_
#define _INTERFACE_EDITOR_PROPERTY_H_

#include "core/common/propertyInfo.h"

enum eMenuType {
	eMenu,
	eSubMenu,
	eSeparator
};

class CInterfaceEditorProperty : public IPropertyObject {

	OptionList* GetMenuType(PropertyOption*) {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("menu"), eMenu); 
		optionlist->AddOption(_("subMenu"), eSubMenu);
		optionlist->AddOption(_("separator"), eSeparator);
		return optionlist;
	}

	OptionList* GetSubMenu(PropertyOption*) {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("<Custom submenu>"), wxNOT_FOUND);
		optionlist->AddOption(_("file"), wxID_FILE);
		optionlist->AddOption(_("edit"), wxID_EDIT);
		//optionlist->AddOption(_("all operations..."), wxID_ENTERPRISE_ALL_OPERATIONS);
		return optionlist;
	}

protected:

	PropertyCategory* m_interfaceCategory = IPropertyObject::CreatePropertyCategory("general");
	Property* m_propertyMenuType = IPropertyObject::CreateProperty(m_interfaceCategory, "menuType", &CInterfaceEditorProperty::GetMenuType, eMenuType::eMenu);
	Property* m_propertySubMenu = IPropertyObject::CreateProperty(m_interfaceCategory, "subMenu", &CInterfaceEditorProperty::GetSubMenu, wxNOT_FOUND);
	Property* m_propertyAction = IPropertyObject::CreateProperty(m_interfaceCategory, "action", PropertyType::PT_TEXT);

	PropertyCategory* m_interfacePresentation = IPropertyObject::CreatePropertyCategory("presentation");
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_interfacePresentation, "caption", PropertyType::PT_TEXT);
	Property* m_propertyToolTip = IPropertyObject::CreateProperty(m_interfacePresentation, "tooltip", PropertyType::PT_TEXT);
	Property* m_propertyDescription = IPropertyObject::CreateProperty(m_interfacePresentation, "tooltip", PropertyType::PT_TEXT);
	Property* m_propertyPicture = IPropertyObject::CreateProperty(m_interfacePresentation, "picture", PropertyType::PT_BITMAP);

public:

	void SetCaption(const wxString &caption) {
		m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	CInterfaceEditorProperty(eMenuType menuType) {
		m_propertyMenuType->SetValue(menuType);
	}

	virtual ~CInterfaceEditorProperty();

	//system override 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_ABSTRACT;
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("interfaceEditor");
	}

	virtual wxString GetClassName() const override {
		return wxT("interfaceEditor");
	}

	/**
	* Property events
	*/
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg,
		class wxPGProperty* pgProperty, Property* property);
};

#endif 