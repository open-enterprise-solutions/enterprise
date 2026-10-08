#ifndef _FRMCLIENT_VIEW_SIZER_H__
#define _FRMCLIENT_VIEW_SIZER_H__

#include <wx/statbox.h>
#include <wx/wrapsizer.h>

#include "frmclient/visualView/ctrl/control.h"
#include "frmclient/visualView/controlEnum.h"   // the sizer's enums — orient, stretch

#include "frmclient/backend/propertyManager/property/propertyBoolean.h"
#include "frmclient/backend/propertyManager/property/propertyColour.h"
#include "frmclient/backend/propertyManager/property/propertyEnum.h"
#include "frmclient/backend/propertyManager/property/propertyFont.h"
#include "frmclient/backend/propertyManager/property/propertyNumber.h"
#include "frmclient/backend/propertyManager/property/propertySize.h"

// THE SIZERS OF A VIEW — the desktop's (frontend/visualView/ctrl/sizer.h): a sizer is no window, it lays out what is
// in it; its properties are the desktop's, read from the frame (ReadData).

// What every sizer does with its minimum size (the desktop's ibValueSizer::UpdateSizer).
void ibViewUpdateSizer(wxSizer* sizer, const ibProtocolNode& node);

class ibValueSizer : public ibValueControl {
public:

	ibValueSizer(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual int GetComponentType() const override { return COMPONENT_TYPE_SIZER; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("SizerItem"), _("Sizer"));
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categorySizer, wxT("MinimumSize"), _("Minimum size"), _("The smallest size the sizer keeps for the area it lays out, in pixels; -1 on a side means no limit there. The form cannot shrink the area below it."), wxDefaultSize);
};

// A SIZER ITEM — the desktop's ibValueSizerItem: how its one control sits in the sizer above — its share of the room,
// its stretch, its border. Built by the host's walk as the desktop's was; it draws nothing.
class ibValueSizerItem : public ibValueFrame {
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

	ibValueSizerItem(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId) {}

	virtual void Create(wxWindow* WXUNUSED(parent), const ibProtocolNode& WXUNUSED(node)) override {}
	// The desktop's OnUpdated: its child's place in the sizer it stands in, set to what it holds now.
	virtual void Update(const ibProtocolNode& node) override;

	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_SIZERITEM;
	}

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

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

class ibValueBoxSizer : public ibValueSizer {
public:
	ibValueBoxSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override { return m_sizer; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	wxBoxSizer* m_sizer = nullptr;
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("How the sizer lays out its controls: vertically (the default, one under another) or horizontally (side by side)."),
		wxVERTICAL);
};

class ibValueWrapSizer : public ibValueSizer {
public:
	ibValueWrapSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override { return m_sizer; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	wxWrapSizer* m_sizer = nullptr;
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("The direction controls are laid out in. When a row (or column) has no room for the next control, it wraps to a new one. Horizontal by default."),
		wxHORIZONTAL);
};

class ibValueGridSizer : public ibValueSizer {
public:
	ibValueGridSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override { return m_sizer; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	wxGridSizer* m_sizer = nullptr;
	ibPropertyUInteger* m_propertyRows = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizer, wxT("Rows"), _("Rows"),
		_("The number of rows in the grid. 0 (the default): as many as the controls need, given the column count."), 0);
	ibPropertyUInteger* m_propertyCols = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizer, wxT("Cols"), _("Cols"),
		_("The number of columns in the grid; controls fill it row by row, left to right. All cells get the same size. Default 2."), 2);
};

// A framed group — a sizer with a box of its own, which what is in it is built in.
class ibValueStaticBoxSizer : public ibValueSizer {
public:
	ibValueStaticBoxSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer*  GetSizer() const override { return m_sizer; }
	virtual wxWindow* GetChildParent() const override { return m_sizer != nullptr ? m_sizer->GetStaticBox() : nullptr; }

	virtual wxString GetControlTitle() const override { return m_propertyTitle->GetValueAsTranslateString(); }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	wxStaticBoxSizer* m_sizer = nullptr;
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

#endif
