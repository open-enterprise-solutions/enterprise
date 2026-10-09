#ifndef __WINDOW_BASE_H__
#define __WINDOW_BASE_H__

#include "control.h"
#include "backend/backend_command.h"   // ibBackendCommandSender — a composite control (tablebox) is a command source

#define FORM_ACTION 1

class ibValueWindow : public ibValueControl {
	public:

	void EnableWindow(bool enable = true) const { m_propertyEnabled->SetValue(enable); }
	void VisibleWindow(bool visible = true) const { m_propertyVisible->SetValue(visible); }

	ibValueWindow();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

	// Enabled, shown and the tooltip as they are NOW — each one a decision the saved property alone does
	// not make (the form may be disabled, the control unavailable or unbound). Sizes, font and colours
	// are the schema's: the client reads them off the properties as saved.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

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

//********************************************************************************************
//*                     Value Window — composite (chrome) variant                            *
//********************************************************************************************

// The composite variant of a window control: a control that carries chrome of its own —
// the command bar today, a search row later — as LAYERS beside it. A control opts in by
// deriving from here instead of ibValueWindow (tableBox first).
class ibValueWindowComposite : public ibValueWindow, public ibBackendCommandSender {
public:

	// ibBackendCommandSender — a COMPOSITE control (a tablebox) IS a command source (it carries a command bar),
	// unlike a plain control / sizer. The form's command walk DESCENDS into it; it TERMINATES on a standard action
	// of its OWN bus (the composite is the action's runtime — the caller runs CallAsAction). Mirror of a source hop.
	virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) override;

	// Every composite window carries a command bar (its toolbar layer) — created
	// here so any composite gets one by default; non-composite controls leave
	// GetCommandBar() null (that's how the host knows there is none).
	ibValueWindowComposite();

	// The window's state, and the command bar's under `CommandBar` — absent while the bar is SUPPRESSED (the
	// control bound to the form's main attribute: HasCommandBar), since the form's own bar carries those commands.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Serialize the "Layers" block (the command bar today; more layers later) alongside the
	// window's own data. A derived composite (tableBox) chains to THIS, not straight to ibValueWindow.
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;
};

#endif