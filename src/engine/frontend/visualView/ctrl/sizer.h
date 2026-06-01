#ifndef _SIZER_H_
#define _SIZER_H_

#include "control.h"

class FRONTEND_API ibValueSizer : public ibValueControl {
	public:

	ibValueSizer() : ibValueControl() {}

	virtual int GetComponentType() const { return COMPONENT_TYPE_SIZER; }

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer = ibWriterMemory());

protected:
	// Cross-platform sizer-update helper. Desktop pushes MinSize + Layout
	// onto the live wxSizer; web is a no-op (CSS handles layout, no live
	// server-side object to poke). Typedef makes the parameter compile
	// against wxSizer on desktop and ibWebSizer on web so callers share
	// one line — the per-build body handles the actual difference.
	void UpdateSizer(ibFrontendSizer* sizer);

protected:
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("SizerItem"), _("Sizer"));
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categorySizer, wxT("MinimumSize"), _("Minimum size"), _("Sets the minimum size of the window, to indicate to the sizer layout mechanism that this is the minimum required size."), wxDefaultSize);
};

// Free helper — rebind an existing child's layout params (proportion /
// flag bitmask / border) inside its parent sizer, optionally forcing
// a specific position (idx >= 0). Doesn't touch any ibValueSizer state,
// so it lives in a namespace, not as a member. Hides the platform-
// specific mechanism:
//   * Desktop: wxSizer has no in-place SetItemProportion /
//     SetItemBorder — the only way to change params is Detach + Add
//     (or Detach + Insert(idx, …) if position matters).
//   * Web: ibWebSizer stores per-child params in its own `Item`
//     vector, so UpdateItemParams writes them directly; the vector
//     order is already in sync with the ibValueFrame tree, so idx
//     is ignored on this build.
namespace ibSizerOps {
	void FRONTEND_API SetChildParams(ibFrontendSizer* sizer, wxObject* child,
		int proportion, int flag, int border, int idx = -1);
}

//////////////////////////////////////////////////////////////////////////////

class FRONTEND_API ibValueSizerItem : public ibValueFrame {
	public:

	void SetProportion(int proportion) {
		m_propertyProportion->SetValue(proportion);
	}

	int GetProportion() const {
		return m_propertyProportion->GetValueAsUInteger();
	}

	void SetFlagBorder(long flag_border) const {
		m_propertyFlagBorderLeft->SetValue((flag_border & (wxLEFT)) != 0);
		m_propertyFlagBorderRight->SetValue((flag_border & (wxRIGHT)) != 0);
		m_propertyFlagBorderTop->SetValue((flag_border & (wxUP)) != 0);
		m_propertyFlagBorderBottom->SetValue((flag_border & (wxDOWN)) != 0);
	}

	long GetFlagBorder() const {
		long flag = 0;
		if (m_propertyFlagBorderLeft->GetValueAsBoolean()) flag |= wxLEFT;
		if (m_propertyFlagBorderRight->GetValueAsBoolean()) flag |= wxRIGHT;
		if (m_propertyFlagBorderTop->GetValueAsBoolean()) flag |= wxUP;
		if (m_propertyFlagBorderBottom->GetValueAsBoolean()) flag |= wxDOWN;
		return flag;
	}

	void SetFlagState(const wxStretch& s) const {
		m_propertyFlagState->SetValue(s);
	}

	wxStretch GetFlagState() const {
		return m_propertyFlagState->GetValueAsEnum();
	}

	int GetBorder() const {
		return m_propertyBorder->GetValueAsUInteger();
	}

	void SetBorder(long border) const {
		m_propertyBorder->SetValue(border);
	}

	ibValueSizerItem();

	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;

	virtual int GetComponentType() const {
		return COMPONENT_TYPE_SIZERITEM;
	}

	//get metadata
	virtual ibMetaData* GetMetaData() const override;

	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }
	virtual void SetOwnerForm(ibValueForm* ownerForm) { m_formOwner = ownerForm; }

	// allow getting value in control
	virtual bool HasValueInControl() const { return false; }

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { return true; }

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer = ibWriterMemory());

private:

	//frame owner 
	ibValueForm* m_formOwner;

	ibPropertyCategory* m_categorySizerItem = ibPropertyObject::CreatePropertyCategory(wxT("SizerItem"), _("Sizer item"));
	ibPropertyUInteger* m_propertyProportion = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizerItem, wxT("Proportion"), _("Proportion"), 0);
	ibPropertyCategory* m_categorySizerBorder = ibPropertyObject::CreatePropertyCategory(wxT("SizerItemBorder"), _("Border"));
	ibPropertyUInteger* m_propertyBorder = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizerBorder, wxT("BorderSize"), _("Size"), 5);
	ibPropertyBoolean* m_propertyFlagBorderLeft = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderLeft"), _("Left"), true);
	ibPropertyBoolean* m_propertyFlagBorderRight = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderRight"), _("Right"), true);
	ibPropertyBoolean* m_propertyFlagBorderTop = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderTop"), _("Top"), true);
	ibPropertyBoolean* m_propertyFlagBorderBottom = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderBottom"), _("Bottom"), true);
	ibPropertyEnum<ibValueEnumStretch>* m_propertyFlagState = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumStretch>>(m_categorySizerItem, wxT("Stretch"), _("Stretch"), wxStretch::wxSHRINK);
};

//////////////////////////////////////////////////////////////////////////////

class ibValueBoxSizer : public ibValueSizer {
	public:

	ibValueBoxSizer();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer = ibWriterMemory());

private:
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"), wxVERTICAL);
};

#include <wx/wrapsizer.h>

class ibValueWrapSizer : public ibValueSizer {
	public:

	ibValueWrapSizer();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer = ibWriterMemory());

private:
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

class ibValueStaticBoxSizer : public ibValueSizer {
	public:

	ibValueStaticBoxSizer();

	//get title
	virtual wxString GetControlTitle() const { return m_propertyTitle->GetValueAsTranslateString(); }

	//control factory
	virtual wxObject* Create(ibFrontendWindow* parent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer = ibWriterMemory());

private:
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"), wxHORIZONTAL);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categorySizer, wxT("Title"), _("Title"), wxT(""));
	ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categorySizer, wxT("Font"), _("Font"), _("Sets the font for this window. This should not be use for a parent window if you don't want its font to be inherited by its children"));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categorySizer, wxT("ForegroundColour"), _("Foreground"), _("Sets the foreground colour of the window."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categorySizer, wxT("BackgroundColour"), _("Background"), _("Sets the background colour of the window."), wxDefaultStypeBGColour);
	ibPropertyString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyString>(m_categorySizer, wxT("Tooltip"), _("Tooltip"), _("Attach a tooltip to the window."), wxT(""));
	ibPropertyBoolean* m_propertyContextMenu = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizer, wxT("ContextMenu"), _("Context menu"), _("Generates event handler for displaying of menu assigned to this widgets as a context menu."));
	ibPropertyString* m_propertyContextHelp = ibPropertyObject::CreateProperty<ibPropertyString>(m_categorySizer, wxT("ContextHelp"), _("Context help"), _("Attach context-sensitive help to the window. Note: The Project's &quot;help_provider&quot; property must be set for context-sensitive help to work."), wxEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizer, wxT("Enabled"), _("Enabled"), _("Enable or disable the window for user input.Note that when a parent window is disabled, all of its children are disabled as well and they are reenabled again when the parent is."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizer, wxT("Visible"), _("Visible"), _("Indicates that a pane caption should be visible."), true);
};

class ibValueGridSizer : public ibValueSizer {
	public:

	ibValueGridSizer();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer = ibWriterMemory());

private:
	ibPropertyUInteger* m_propertyRows = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizer, wxT("Rows"), _("Rows"), 0);
	ibPropertyUInteger* m_propertyCols = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizer, wxT("Cols"), _("Cols"), 2);
};

#endif 