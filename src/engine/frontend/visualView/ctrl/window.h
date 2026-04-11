#ifndef __WINDOW_BASE_H__
#define __WINDOW_BASE_H__

#include "control.h"

#define FORM_ACTION 1

class ibValueWindow : public ibValueControl {
	wxDECLARE_ABSTRACT_CLASS(ibValueWindow);
public:

	void EnableWindow(bool enable = true) const { m_propertyEnabled->SetValue(enable); }
	void VisibleWindow(bool visible = true) const { m_propertyVisible->SetValue(visible); }

	ibValueWindow();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

protected:

	void UpdateWindow(wxWindow* window);

	ibPropertyCategory* m_categoryWindow = ibPropertyObject::CreatePropertyCategory(wxT("Window"), _("Window"));
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categoryWindow, wxT("MinimumSize"), _("Minimum size"), _("Sets the minimum size of the window, to indicate to the sizer layout mechanism that this is the minimum required size."), wxDefaultSize);
	ibPropertySize* m_propertyMaxSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categoryWindow, wxT("MaximumSize"), _("Maximum size"), _("Sets the maximum size of the window, to indicate to the sizer layout mechanism that this is the maximum allowable size."), wxDefaultSize);
	ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categoryWindow, wxT("Font"), _("Font"), _("Sets the font for this window. This should not be use for a parent window if you don't want its font to be inherited by its children"));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryWindow, wxT("ForegroundColour"), _("Foreground"), _("Sets the foreground colour of the window."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryWindow, wxT("BackgroundColour"), _("Background"), _("Sets the background colour of the window."), wxDefaultStypeBGColour);
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryWindow, wxT("Tooltip"), _("Tooltip"), _("Attach a tooltip to the window."), wxT(""));
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryWindow, wxT("Enabled"), _("Enabled"), _("Enable or disable the window for user input.Note that when a parent window is disabled, all of its children are disabled as well and they are reenabled again when the parent is."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryWindow, wxT("Visible"), _("Visible"), _("Indicates that a pane caption should be visible."), true);

};

#endif 