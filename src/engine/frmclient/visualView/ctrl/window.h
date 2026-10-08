#ifndef _FRMCLIENT_VIEW_WINDOW_BASE_H__
#define _FRMCLIENT_VIEW_WINDOW_BASE_H__

#include "control.h"

#include "frmclient/backend/propertyManager/property/propertyBoolean.h"
#include "frmclient/backend/propertyManager/property/propertyColour.h"
#include "frmclient/backend/propertyManager/property/propertyFont.h"
#include "frmclient/backend/propertyManager/property/propertySize.h"

// A CONTROL THAT IS A WINDOW — the desktop's ibValueWindow (frontend/visualView/ctrl/window.h): its sizes, its look,
// its tooltip, whether it is enabled and shown — as its properties, read from the frame as the desktop's are read from
// the form.
class ibValueWindow : public ibValueControl {
public:

	void EnableWindow(bool enable = true) const { m_propertyEnabled->SetValue(enable); }
	void VisibleWindow(bool visible = true) const { m_propertyVisible->SetValue(visible); }

	ibValueWindow(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	virtual int GetComponentType() const override { return COMPONENT_TYPE_WINDOW; }

protected:

	ibPropertyCategory* m_categoryWindow = ibPropertyObject::CreatePropertyCategory(wxT("Window"), _("Window"));
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categoryWindow, wxT("MinimumSize"), _("Minimum size"), _("The smallest size the form layout may give the control, in pixels; -1 on a side means no limit there. The layout never shrinks the control below it, even when the form is made smaller."), wxDefaultSize);
	ibPropertySize* m_propertyMaxSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categoryWindow, wxT("MaximumSize"), _("Maximum size"), _("The largest size the form layout may give the control, in pixels; -1 on a side means no limit there. Useful to keep a stretched field from growing across a wide form."), wxDefaultSize);
	ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categoryWindow, wxT("Font"), _("Font"), _("The control's font. Unset: the parent's font is used. A font set on a group or page is inherited by the controls inside it that have no font of their own."));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryWindow, wxT("ForegroundColour"), _("Foreground"), _("The control's text colour. Default: the system colour, which follows the user's theme."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryWindow, wxT("BackgroundColour"), _("Background"), _("The control's background colour. Default: the system colour, which follows the user's theme."), wxDefaultStypeBGColour);
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryWindow, wxT("Tooltip"), _("Tooltip"), _("Text shown when the mouse pointer rests over the control. Empty: no tooltip. Can be written per language."), wxT(""));
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryWindow, wxT("Enabled"), _("Enabled"), _("Whether the user can interact with the control. A disabled control is shown greyed out and takes no input; disabling a group or page disables everything inside it. Code can change it while the form is open."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryWindow, wxT("Visible"), _("Visible"), _("Whether the control is shown on the form. A hidden control keeps its data and can be shown again from code; the layout closes the gap it leaves."), true);
};

#endif
