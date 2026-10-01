#ifndef __ADVPROP_CHOICE_LINK_H__
#define __ADVPROP_CHOICE_LINK_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/props.h>
#include "backend/backend_type.h"
#include "backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class BACKEND_API ibPropertyChoiceLink;
class BACKEND_API ibPropertyChoiceParameters;

// -----------------------------------------------------------------------
// ibPGChoiceLinkProperty
// -----------------------------------------------------------------------

// ⭐ THE LINK BY TYPE — the row NAMES what governs this field, and the "…" opens the window where it is
// chosen (win/dlgs/choiceLink), exactly as its companion below does for the parameters.
//
// 🛑 IT WAS A DROP-DOWN ROW THAT UNFOLDED into a second one, and a row can show a name and nothing
// else. Choosing the field that decides this one's TYPE is a consequence an author cannot read off a
// name: they want the picture, the company the candidates keep, and a sentence saying what the choice
// would mean (Max, 2026-09-23). Typing is refused for the same reason as next door — a row that took
// text it cannot turn into a link would be answering a question it does not understand.
class ibPGChoiceLinkProperty : public wxPGProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGChoiceLinkProperty);
public:

	ibPGChoiceLinkProperty(ibPropertyChoiceLink* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return value.GetString(); }

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return false; }

	// The click on "…" — the window edits a COPY and the value is posted only when it says something
	// changed, which is what keeps Cancel a real cancel all the way up to the property.
	virtual bool OnEvent(wxPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;

protected:

	ibPropertyChoiceLink* m_property = nullptr;
};

// -----------------------------------------------------------------------
// ibPGChoiceParametersProperty
// -----------------------------------------------------------------------

// The choice parameters are a TABLE, and a table is not edited on a property row: the row NAMES what it
// holds and the "…" opens the window where the rows live (win/dlgs/choiceParameters). Typing is refused
// for the same reason — a row that took text it cannot turn into rows would be answering a question it
// does not understand.
class ibPGChoiceParametersProperty : public wxPGProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGChoiceParametersProperty);
public:

	ibPGChoiceParametersProperty(ibPropertyChoiceParameters* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return value.GetString(); }

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return false; }

	// The click on "…" — the window edits a COPY and the value is posted only when it says something
	// changed, which is what keeps Cancel a real cancel all the way up to the property.
	virtual bool OnEvent(wxPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;

protected:

	ibPropertyChoiceParameters* m_property = nullptr;
};

#endif
