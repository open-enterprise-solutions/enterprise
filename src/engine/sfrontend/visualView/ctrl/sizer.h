#ifndef _SIZER_H_
#define _SIZER_H_

#include "control.h"

class SFRONTEND_API ibValueSizer : public ibValueControl {
	public:

	ibValueSizer() : ibValueControl() {}

	virtual int GetComponentType() const { return COMPONENT_TYPE_SIZER; }

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

protected:
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("SizerItem"), _("Sizer"));
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categorySizer, wxT("MinimumSize"), _("Minimum size"), _("The smallest size the sizer keeps for the area it lays out, in pixels; -1 on a side means no limit there. The form cannot shrink the area below it."), wxDefaultSize);
};

//////////////////////////////////////////////////////////////////////////////

class SFRONTEND_API ibValueSizerItem : public ibValueFrame {
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

	virtual int GetComponentType() const {
		return COMPONENT_TYPE_SIZERITEM;
	}

	//get metadata
	virtual const ibMetaData* GetMetaData() const override;

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
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	//frame owner 
	ibValueForm* m_formOwner;

	ibPropertyCategory* m_categorySizerItem = ibPropertyObject::CreatePropertyCategory(wxT("SizerItem"), _("Sizer item"));
	ibPropertyUInteger* m_propertyProportion = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizerItem, wxT("Proportion"), _("Proportion"),
		_("How much of its sizer's spare room the control takes along the sizer's direction. 0 (the default): the control keeps its natural size. Otherwise the spare room is shared in these proportions - controls with 1 and 2 get one third and two thirds."),
		0);
	ibPropertyCategory* m_categorySizerBorder = ibPropertyObject::CreatePropertyCategory(wxT("SizerItemBorder"), _("Border"));
	ibPropertyUInteger* m_propertyBorder = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizerBorder, wxT("BorderSize"), _("Size"),
		_("The empty margin kept around the control, in pixels, on the sides switched on below. Default 5."), 5);
	ibPropertyBoolean* m_propertyFlagBorderLeft = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderLeft"), _("Left"),
		_("Whether the margin (Size) is kept on the control's left side."), true);
	ibPropertyBoolean* m_propertyFlagBorderRight = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderRight"), _("Right"),
		_("Whether the margin (Size) is kept on the control's right side."), true);
	ibPropertyBoolean* m_propertyFlagBorderTop = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderTop"), _("Top"),
		_("Whether the margin (Size) is kept above the control."), true);
	ibPropertyBoolean* m_propertyFlagBorderBottom = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, wxT("BorderBottom"), _("Bottom"),
		_("Whether the margin (Size) is kept below the control."), true);
	ibPropertyEnum<ibValueEnumStretch>* m_propertyFlagState = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumStretch>>(m_categorySizerItem, wxT("Stretch"), _("Stretch"),
		_("The control's size across its sizer's direction. Shrink (the default): its natural size. Expand: the full width of a vertical sizer, or the full height of a horizontal one."),
		wxStretch::wxSHRINK);
};

//////////////////////////////////////////////////////////////////////////////

class ibValueBoxSizer : public ibValueSizer {
	public:

	ibValueBoxSizer();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("How the sizer lays out its controls: vertically (the default, one under another) or horizontally (side by side)."),
		wxVERTICAL);
};

class ibValueWrapSizer : public ibValueSizer {
	public:

	ibValueWrapSizer();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("The direction controls are laid out in. When a row (or column) has no room for the next control, it wraps to a new one. Horizontal by default."),
		wxHORIZONTAL);
};

class ibValueStaticBoxSizer : public ibValueSizer {
	public:

	ibValueStaticBoxSizer();

	//get title
	virtual wxString GetControlTitle() const { return m_propertyTitle->GetValueAsTranslateString(); }

	// The framed group is the one sizer that is also seen: its caption in the user's language, and whether
	// the frame is enabled and shown — shown only while the functional options of this base leave it available.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("How the framed group lays out its controls: horizontally (the default, side by side) or vertically (one under another)."),
		wxHORIZONTAL);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categorySizer, wxT("Title"), _("Title"),
		_("The caption drawn on the group's frame. Empty: a frame with no caption. Can be written per language."), wxT(""));
	ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categorySizer, wxT("Font"), _("Font"), _("The font of the frame's caption. Unset: the form's font."));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categorySizer, wxT("ForegroundColour"), _("Foreground"), _("The colour of the frame's caption. Default: the system colour."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categorySizer, wxT("BackgroundColour"), _("Background"), _("The frame's background colour. Default: the system colour."), wxDefaultStypeBGColour);
	ibPropertyString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyString>(m_categorySizer, wxT("Tooltip"), _("Tooltip"), _("Text shown when the mouse pointer rests over the frame. Empty: no tooltip."), wxT(""));
	ibPropertyBoolean* m_propertyContextMenu = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizer, wxT("ContextMenu"), _("Context menu"), _("Saved with the form; nothing reads it yet, so it has no effect on a framed group."));
	ibPropertyString* m_propertyContextHelp = ibPropertyObject::CreateProperty<ibPropertyString>(m_categorySizer, wxT("ContextHelp"), _("Context help"), _("Saved with the form; nothing reads it yet, so no context help is shown for a framed group."), wxEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizer, wxT("Enabled"), _("Enabled"), _("Whether the controls inside the frame take input. Off: the frame and everything in it are greyed out."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizer, wxT("Visible"), _("Visible"), _("Whether the frame is shown. Hiding it hides the frame itself; its controls keep their data."), true);
};

class ibValueGridSizer : public ibValueSizer {
	public:

	ibValueGridSizer();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyUInteger* m_propertyRows = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizer, wxT("Rows"), _("Rows"),
		_("The number of rows in the grid. 0 (the default): as many as the controls need, given the column count."), 0);
	ibPropertyUInteger* m_propertyCols = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizer, wxT("Cols"), _("Cols"),
		_("The number of columns in the grid; controls fill it row by row, left to right. All cells get the same size. Default 2."), 2);
};

#endif 