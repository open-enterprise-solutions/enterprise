#ifndef __WINDOW_BASE_H__
#define __WINDOW_BASE_H__

#include "control.h"
#include "frontend/frontendTypes.h"

class ibCanvasWindow;   // layer canvas widget (visualView/canvasWindow.h)

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

protected:

	// Apply the shared window properties (size/font/colour on desktop,
	// enabled/visible/label on both) onto the frontend object created
	// by the derived control's Create(). ibFrontendWindow resolves to
	// wxWindow on desktop and ibWebWindow on web — web skips the wx-
	// only knobs (sizes, font, colours) and just syncs what ibWebWindow
	// actually stores.
	void UpdateWindow(ibFrontendWindow* window);

	// Apply ONLY the appearance props (font / colours, when set) onto a window —
	// the subset of UpdateWindow reused to sync the chrome layers' look to the
	// control (WITHOUT size / enabled / visible / tooltip, which would wreck the
	// toolbar's proportions).
	void ApplyLook(ibFrontendWindow* window);

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

//********************************************************************************************
//*                     Value Window — composite (chrome) variant                            *
//********************************************************************************************

// The composite variant of a window control: instead of the sibling bar (ibValueControl's
// plain CreateWithLayers), it WRAPS the real control in an ibCanvasWindow — chrome parts
// (command-bar toolbar today, a search row later) stacked above, the control filling the
// rest. The host maps the composite as the control's wxObject. A control opts in by
// deriving from here instead of ibValueWindow (tableBox first). Update stays plain:
// wxCompositeWindow forwards colour/font/focus to the inner, size/show sit on the wrapper.
class ibValueWindowComposite : public ibValueWindow {
public:

	// Every composite window carries a command bar (its toolbar layer) — created
	// here so any composite gets one by default; non-composite controls leave
	// GetCommandBar() null (that's how the host knows there is none).
	ibValueWindowComposite();

	// The real inner widget behind the layer canvas: GetWxObject() returns the
	// ibCanvasWindow wrapper — unwrap it to the control's own widget. A derived control
	// casts this to its concrete type; columns / siblings reach it via GetOwner()->GetInnerWx().
	wxObject* GetInnerWx() const;

	// GLOBAL chrome render — the composite window owns it, and the form calls the SAME entry.
	// BuildLayerParts assembles ALL parts (the toolbar today; a search field / status bar
	// later, added HERE only). Per-part render is a corner case delegated to its owner: the
	// toolbar comes from commandBar.h's BuildCommandBarToolBar. So the two call sites (form +
	// composite control) never enumerate parts themselves. (Web gets these too — the desktop
	// specifics live inside the bodies, not around the functions.)
	static std::vector<ibFrontendWindow*> BuildLayerParts(ibFrontendWindow* parent, ibValueCommandBar* cbar, ibValueForm* form);
	static void UpdateLayerParts(const std::vector<ibFrontendWindow*>& parts, ibValueCommandBar* cbar, ibValueForm* form);   // refresh existing parts IN PLACE

	// All five lifecycle stages route through the layer canvas: Create builds it, the
	// rest unwrap it to the inner control (OnSelected stays plain — not needed). Update
	// additionally applies the window props to the WRAPPER, so wxCompositeWindow forwards
	// colour/font to the toolbar too — the chrome tracks the control's appearance.
	virtual wxObject* CreateWithLayers(ibFrontendWindow* wndParent, ibVisualHost* visualHost) override;
	virtual void OnCreatedWithLayers(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void UpdateWithLayers(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdatedWithLayers(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void CleanupWithLayers(wxObject* wxobject, ibVisualHost* visualHost) override;

	// Serialize the "Layers" block (the command bar today; more layers later) alongside the
	// window's own data. A derived composite (tableBox) chains to THIS, not straight to ibValueWindow.
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;
};

#endif