#include "advpropColour.h"

#include <wx/propgrid/advprops.h>   // wxPG_COLOUR_CUSTOM
#include <wx/colordlg.h>
#include <wx/odcombo.h>

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the colour's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/win/typeconv.h"   // ColourToWxColour / WxColourToColour — the dialog's and the painter's colour

namespace {

// The value of a colour: the same property's (the cell's own), its node the colour's text `r,g,b` as the server
// stores it, read as that text.
wxVariant WriteColour(const wxVariant& value, const ibColour& colour)
{
	const wxString text = colour.ToString();
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::String(text), text));
}

} // namespace

// -----------------------------------------------------------------------
// ibPGColourProperty
// -----------------------------------------------------------------------

static const char* const gs_cp_es_normcolour_labels[] = {
	ibTRANSLATE("Black"),
	ibTRANSLATE("Maroon"),
	ibTRANSLATE("Navy"),
	ibTRANSLATE("Purple"),
	ibTRANSLATE("Teal"),
	ibTRANSLATE("Gray"),
	ibTRANSLATE("Green"),
	ibTRANSLATE("Olive"),
	ibTRANSLATE("Brown"),
	ibTRANSLATE("Blue"),
	ibTRANSLATE("Fuchsia"),
	ibTRANSLATE("Red"),
	ibTRANSLATE("Orange"),
	ibTRANSLATE("Silver"),
	ibTRANSLATE("Lime"),
	ibTRANSLATE("Aqua"),
	ibTRANSLATE("Yellow"),
	ibTRANSLATE("White"),
	/* TRANSLATORS: Custom colour choice entry */ ibTRANSLATE("Custom"),
	nullptr
};

static const long gs_cp_es_normcolour_values[] = {
	0,
	1,
	2,
	3,
	4,
	5,
	6,
	7,
	8,
	9,
	10,
	11,
	12,
	13,
	14,
	15,
	16,
	17,
	wxPG_COLOUR_CUSTOM
};

static const ibColour gs_cp_es_normcolour_colours[] = {
	ibColour(0, 0, 0),
	ibColour(128, 0, 0),
	ibColour(0, 0, 128),
	ibColour(128, 0, 128),
	ibColour(0, 128, 128),
	ibColour(128, 128, 128),
	ibColour(0, 128, 0),
	ibColour(128, 128, 0),
	ibColour(166, 124, 81),
	ibColour(0, 0, 255),
	ibColour(255, 0, 255),
	ibColour(255, 0, 0),
	ibColour(247, 148, 28),
	ibColour(192, 192, 192),
	ibColour(0, 255, 0),
	ibColour(0, 255, 255),
	ibColour(255, 255, 0),
	ibColour(255, 255, 255)
};

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGColourProperty, wxEnumProperty, TextCtrlAndButton)

// register frontend property
class ibPropertyColourLoader
{
public:
	ibPropertyColourLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyColour"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGColourProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}s_enumLoader;

static wxPGChoices gs_ibPGColourProperty_choicesCache;

ibPGColourProperty::ibPGColourProperty(const wxString& label, const wxString& name,
	const wxVariant& value)
	: wxEnumProperty(label, name, gs_cp_es_normcolour_labels, gs_cp_es_normcolour_values,
		&gs_ibPGColourProperty_choicesCache)
{
	m_flags |= wxPGPropertyFlags_StaticChoices; // Colour selection cannot be changed.

	m_value = value;

	OnSetValue();
}

ibColour ibPGColourProperty::GetVal(const wxVariant* pVariant) const
{
	if (!pVariant)
		pVariant = &m_value;

	ibColour colour;
	colour.FromString(ibVariantDataSerializable::GetNodeValue(*pVariant).AsString());
	return colour;
}

void ibPGColourProperty::OnSetValue()
{
	const ibColour colour = GetVal();

	if (!colour.IsOk())
	{
		SetIndex(wxNOT_FOUND);
		return;
	}

	int ind = ColToInd(colour);

	if (ind == wxNOT_FOUND)
		ind = GetCustomColourIndex();

	SetIndex(ind);
}

ibColour ibPGColourProperty::GetColour(int index) const
{
	const int value = m_choices.GetValue(index);
	if (value < 0 || value >= (int)WXSIZEOF(gs_cp_es_normcolour_colours))
		return ibColour();   // Custom — no colour of its own
	return gs_cp_es_normcolour_colours[value];
}

int ibPGColourProperty::ColToInd(const ibColour& colour) const
{
	const unsigned int i_max = m_choices.GetCount();
	for (unsigned int i = 0; i < i_max; i++)
	{
		const int ind = m_choices[i].GetValue();
		// Skip custom colour
		if (ind == wxPG_COLOUR_CUSTOM)
			continue;

		if (colour == GetColour(i))
			return i;
	}
	return wxNOT_FOUND;
}

int ibPGColourProperty::GetCustomColourIndex() const
{
	return m_choices.Index(wxPG_COLOUR_CUSTOM);
}

wxString ibPGColourProperty::ValueToString(wxVariant& value,
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	const ibColour colour = GetVal(&value);

	if (!colour.IsOk())
		return wxEmptyString;

	const int index = ColToInd(colour);

	if (index != wxNOT_FOUND)
		return m_choices.GetLabel(index);

	return wxString::Format(wxS("(%i,%i,%i)"),
		(int)colour.m_red,
		(int)colour.m_green,
		(int)colour.m_blue);
}

bool ibPGColourProperty::StringToValue(wxVariant& value, const wxString& text, wxPGPropValFormatFlags flags) const
{
	const int custIndex = GetCustomColourIndex();
	wxString custColName;
	if (custIndex != wxNOT_FOUND)
		custColName = m_choices.GetLabel(custIndex);

	wxString colStr(text);
	colStr.Trim(true);
	colStr.Trim(false);

	if (colStr == custColName)
	{
		if (!(flags & wxPGPropValFormatFlags::EditableValue))
			return false;

		if (!QueryColourFromUser(value))
		{
			if (!(flags & wxPGPropValFormatFlags::PropertySpecific))
				return false;
			// If query for value comes from the event handler
			// use current pending value to be processed later on in OnEvent().
			SetValueInEvent(value);
		}
		return true;
	}

	// A choice by its label first — the labels are ours, translated, and some (Brown, Orange) are
	// not the colour database's.
	int index;
	wxVariant choice;
	if (ValueFromString_(choice, &index, colStr, flags) && index >= 0)
	{
		value = WriteColour(m_value, GetColour(index));
		return true;
	}

	if (colStr.Find(wxS("(")) == 0)
	{
		// Eliminate whitespace
		colStr.Replace(wxS(" "), wxString());

		// Convert (R,G,B) to rgb(R,G,B)
		if (colStr.Freq(wxS(',')) == 2)
			colStr = wxS("rgb") + colStr;
	}

	wxColour customColour;
	if (!customColour.Set(colStr))
		return false;

	value = WriteColour(m_value, typeConv::WxColourToColour(customColour));
	return true;
}

bool ibPGColourProperty::IntToValue(wxVariant& variant, int number, wxPGPropValFormatFlags flags) const
{
	if (m_choices.GetValue(number) == wxPG_COLOUR_CUSTOM)
	{
		if (!(flags & wxPGPropValFormatFlags::PropertySpecific))
			return QueryColourFromUser(variant);

		// Call from event handler.
		// User will be asked for custom color later on in OnEvent().
		variant = m_value;
	}
	else
	{
		variant = WriteColour(m_value, GetColour(number));
	}

	return true;
}

bool ibPGColourProperty::ValidateValue(wxVariant& value, wxPGValidationInfo& WXUNUSED(validationInfo)) const
{
	// wxEnumProperty looks a text value up among the labels; ours is the colour's `r,g,b` (empty for none).
	const wxString text = ibVariantDataSerializable::GetNodeValue(value).AsString();
	if (text.empty())
		return true;

	ibColour colour;
	return colour.FromString(text);
}

bool ibPGColourProperty::QueryColourFromUser(wxVariant& variant) const
{
	wxPropertyGrid* propgrid = GetGrid();
	wxASSERT(propgrid);

	// Must only occur when user triggers event
	if (!propgrid->HasInternalFlag(wxPropertyGrid::wxPG_FL_IN_HANDLECUSTOMEDITOREVENT))
		return false;

	wxColourData data;
	data.SetChooseFull(true);
	data.SetColour(typeConv::ColourToWxColour(GetVal()));
	for (int i = 0; i < wxColourData::NUM_CUSTOM; i++)
	{
		unsigned char n = i * (256 / wxColourData::NUM_CUSTOM);
		data.SetCustomColour(i, wxColour(n, n, n));
	}

	wxColourDialog dialog(propgrid, &data);
	if (dialog.ShowModal() == wxID_OK)
	{
		variant = WriteColour(m_value, typeConv::WxColourToColour(dialog.GetColourData().GetColour()));

		SetValueInEvent(variant);

		return true;
	}

	return false;
}

// Need to do some extra event handling.
bool ibPGColourProperty::OnEvent(wxPropertyGrid* propgrid,
	wxWindow* WXUNUSED(primary),
	wxEvent& event)
{
	bool askColour = false;

	if (propgrid->IsMainButtonEvent(event))
	{
		// We need to handle button click in case editor has been
		// switched to one that has wxButton as well.
		askColour = true;
	}
	else if (event.GetEventType() == wxEVT_COMBOBOX)
	{
		// Must override index detection since at this point GetIndex()
		// will return old value.
		wxOwnerDrawnComboBox* cb =
			static_cast<wxOwnerDrawnComboBox*>(propgrid->GetEditorControl());

		if (cb && cb->GetSelection() == GetCustomColourIndex())
			askColour = true;
	}

	if (askColour && !propgrid->WasValueChangedInEvent())
	{
		wxVariant variant;
		if (QueryColourFromUser(variant))
			return true;
	}
	return false;
}

wxSize ibPGColourProperty::OnMeasureImage(int) const
{
	return wxPG_DEFAULT_IMAGE_SIZE;
}

void ibPGColourProperty::OnCustomPaint(wxDC& dc, const wxRect& rect,
	wxPGPaintData& paintdata)
{
	ibColour col;

	if (paintdata.m_choiceItem >= 0 &&
		paintdata.m_choiceItem < (int)m_choices.GetCount() &&
		paintdata.m_choiceItem != GetCustomColourIndex())
	{
		col = GetColour(paintdata.m_choiceItem);
	}
	else
	{
		col = GetVal();
	}

	if (col.IsOk())
	{
		dc.SetBrush(typeConv::ColourToWxColour(col));
		dc.DrawRectangle(rect);
	}
}
