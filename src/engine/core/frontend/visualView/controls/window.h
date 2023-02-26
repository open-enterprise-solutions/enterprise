#ifndef _WINDOW_BASE_H_
#define _WINDOW_BASE_H_

#include "controlInterface.h"

#define FORM_ACTION 1

class IValueWindow : public IValueControl {
	wxDECLARE_ABSTRACT_CLASS(IValueWindow);
protected:

	PropertyCategory* m_categoryWindow = IPropertyObject::CreatePropertyCategory({ "window", _("window") });
	Property* m_propertyMinSize = IPropertyObject::CreateProperty(m_categoryWindow, { "minimum_size", "minimum size", "Sets the minimum size of the window, to indicate to the sizer layout mechanism that this is the minimum required size." }, PropertyType::PT_WXSIZE, wxDefaultSize);
	Property* m_propertyMaxSize = IPropertyObject::CreateProperty(m_categoryWindow, { "maximum_size", "maximum size", "Sets the maximum size of the window, to indicate to the sizer layout mechanism that this is the maximum allowable size." }, PropertyType::PT_WXSIZE, wxDefaultSize);
	Property* m_propertyFont = IPropertyObject::CreateProperty(m_categoryWindow, { "font" , "font", "Sets the font for this window. This should not be use for a parent window if you don't want its font to be inherited by its children" }, PropertyType::PT_WXFONT);
	Property* m_propertyFG = IPropertyObject::CreateProperty(m_categoryWindow, { "fg", "fg", "Sets the foreground colour of the window." }, PropertyType::PT_WXCOLOUR, wxColour(0, 120, 215));
	Property* m_propertyBG = IPropertyObject::CreateProperty(m_categoryWindow, { "bg", "bg", "Sets the background colour of the window." }, PropertyType::PT_WXCOLOUR, wxColour(240, 240, 240));
	Property* m_propertyTooltip = IPropertyObject::CreateProperty(m_categoryWindow, { "tooltip", "tooltip", "Attach a tooltip to the window." }, PropertyType::PT_WXSTRING);
	Property* m_propertyEnabled = IPropertyObject::CreateProperty(m_categoryWindow, { "enabled", "enabled", "Enable or disable the window for user input. Note that when a parent window is disabled, all of its children are disabled as well and they are reenabled again when the parent is." }, PropertyType::PT_BOOL, true);
	Property* m_propertyVisible = IPropertyObject::CreateProperty(m_categoryWindow, { "visible", "visible", "Indicates that a pane caption should be visible." }, PropertyType::PT_BOOL, true);
public:

	void EnableWindow(bool enable = true) const {
		m_propertyEnabled->SetValue(enable);
	}

	void VisibleWindow(bool visible = true) const {
		m_propertyVisible->SetValue(visible);
	}

	IValueWindow();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_WINDOW;
	}

protected:

	void UpdateWindow(wxWindow* window);
	void UpdateLabelSize(class IDynamicBorder* textCtrl);
};

#endif 