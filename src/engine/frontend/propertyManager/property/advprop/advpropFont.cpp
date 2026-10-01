#include "advpropFont.h"

#include "backend/propertyManager/property/propertyFont.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGFontProperty
// -----------------------------------------------------------------------

#include <wx/fontdlg.h>
#include <wx/fontenum.h>

static const wxChar* gs_fp_es_family_labels[] = {
	wxT("Default"), wxT("Decorative"),
	wxT("Roman"), wxT("Script"),
	wxT("Swiss"), wxT("Modern"),
	wxT("Teletype"), wxT("Unknown"),
	(const wxChar*)nullptr
};

static long gs_fp_es_family_values[] = {
	wxFONTFAMILY_DEFAULT, wxFONTFAMILY_DECORATIVE,
	wxFONTFAMILY_ROMAN, wxFONTFAMILY_SCRIPT,
	wxFONTFAMILY_SWISS, wxFONTFAMILY_MODERN,
	wxFONTFAMILY_TELETYPE, wxFONTFAMILY_UNKNOWN
};

static const wxChar* gs_fp_es_style_labels[] = {
	wxT("Normal"),
	wxT("Slant"),
	wxT("Italic"),
	(const wxChar*)nullptr
};

static long gs_fp_es_style_values[] = {
	wxFONTSTYLE_NORMAL,
	wxFONTSTYLE_SLANT,
	wxFONTSTYLE_ITALIC
};

static const wxChar* gs_fp_es_weight_labels[] = {
	wxT("Normal"),
	wxT("Light"),
	wxT("Bold"),
	(const wxChar*)nullptr
};

static long gs_fp_es_weight_values[] = {
	wxFONTWEIGHT_NORMAL,
	wxFONTWEIGHT_LIGHT,
	wxFONTWEIGHT_BOLD
};

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGFontProperty, wxPGProperty, TextCtrlAndButton)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(ibPGFontProperty, wxPGProperty, wxFont, const wxFont&, TextCtrlAndButton)
#endif

// register frontend property 
class ibPropertyFontLoader
{
public:
	ibPropertyFontLoader() {
		ibPropertyRegistry::Register([](ibPropertyFont* prop) -> wxPGProperty* {
			return new ibPGFontProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsFont());
		});
	}
} g_fontLoader;

#include "backend/typeconv.h"

ibPGFontProperty::ibPGFontProperty(const wxString& label, const wxString& strName,
	const wxFontContainer& value)
	: wxPGProperty(label, strName)
{
	SetValue(WXVARIANT(typeConv::FontToString(value)));

	// Initialize font family choices list
	if (!wxPGGlobalVars->m_fontFamilyChoices)
	{
		wxFontEnumerator enumerator;
		enumerator.EnumerateFacenames();

		wxArrayString faceNames = enumerator.GetFacenames();

		faceNames.Sort();
		faceNames.Insert(wxEmptyString, 0);

		wxPGGlobalVars->m_fontFamilyChoices = new wxPGChoices(faceNames);
	}

	//wxString emptyString(wxEmptyString);

	AddPrivateChild(new wxIntProperty(_("Point Size"), wxT("point Size"),
		value.m_pointSize));

	AddPrivateChild(new wxEnumProperty(_("Family"), wxT("family"),
		gs_fp_es_family_labels, gs_fp_es_family_values,
		value.m_family));

	wxString faceName = value.m_faceName;
	// If font was not in there, add it now
	if (faceName.length() &&
		wxPGGlobalVars->m_fontFamilyChoices->Index(faceName) == wxNOT_FOUND)
		wxPGGlobalVars->m_fontFamilyChoices->AddAsSorted(faceName);

	wxPGProperty* p = new wxEnumProperty(_("Face Name"), wxT("face Name"),
		*wxPGGlobalVars->m_fontFamilyChoices);

	p->SetValueFromString(faceName, wxPGPropValFormatFlags::FullValue);

	AddPrivateChild(p);

	AddPrivateChild(new wxEnumProperty(_("Style"), wxT("style"),
		gs_fp_es_style_labels, gs_fp_es_style_values, value.m_style));

	AddPrivateChild(new wxEnumProperty(_("Weight"), wxT("weight"),
		gs_fp_es_weight_labels, gs_fp_es_weight_values, value.m_weight));

	AddPrivateChild(new wxBoolProperty(_("Underlined"), wxT("underlined"),
		value.m_underlined));
}

ibPGFontProperty::~ibPGFontProperty() {}

void ibPGFontProperty::OnSetValue()
{
	// do nothing
}

wxString ibPGFontProperty::GetValueAsString(wxPGPropValFormatFlags flags) const
{
	return wxPGProperty::GetValueAsString(flags);
}

bool ibPGFontProperty::OnEvent(wxPropertyGrid* propgrid, wxWindow* WXUNUSED(primary),
	wxEvent& event)
{
	if (propgrid->IsMainButtonEvent(event))
	{
		// Update value from last minute changes

		wxFontData data;
		wxFont font = typeConv::StringToFont(m_value.GetString());

		data.SetInitialFont(font);
		data.SetColour(*wxBLACK);

		wxFontDialog dlg(propgrid, data);
		if (dlg.ShowModal() == wxID_OK)
		{
			propgrid->EditorsValueWasModified();

			wxFontContainer fcont(dlg.GetFontData().GetChosenFont());

			wxVariant variant = WXVARIANT(typeConv::FontToString(fcont));
			SetValueInEvent(variant);

			return true;
		}
	}
	return false;
}

void ibPGFontProperty::RefreshChildren()
{
	wxString fstr = m_value.GetString();
	wxFontContainer font = typeConv::StringToFont(fstr);

	Item(0)->SetValue(font.m_pointSize);
	Item(1)->SetValue(font.m_family);
	Item(2)->SetValueFromString(font.m_faceName, wxPGPropValFormatFlags::FullValue);
	Item(3)->SetValue(font.m_style);
	Item(4)->SetValue(font.m_weight);
	Item(5)->SetValue(font.m_underlined);
}

wxVariant ibPGFontProperty::ChildChanged(wxVariant& thisValue, int ind, wxVariant& childValue) const
{
	wxFontContainer font = typeConv::StringToFont(thisValue.GetString());

	if (ind == 0)
	{
		font.m_pointSize = childValue.GetLong();
	}
	else if (ind == 1)
	{
		int fam = childValue.GetLong();
		if (fam < wxFONTFAMILY_DEFAULT || fam > wxFONTFAMILY_TELETYPE) {
			fam = wxFONTFAMILY_DEFAULT;
		}
		font.m_family = static_cast<wxFontFamily>(fam);
	}
	else if (ind == 2)
	{
		wxString faceName;
		int faceIndex = childValue.GetLong();

		if (faceIndex >= 0)
			faceName = wxPGGlobalVars->m_fontFamilyChoices->GetLabel(faceIndex);

		font.m_faceName = faceName;
	}
	else if (ind == 3)
	{
		int st = childValue.GetLong();
		if (st != wxFONTSTYLE_NORMAL && st != wxFONTSTYLE_SLANT && st != wxFONTSTYLE_ITALIC) {
			st = wxFONTSTYLE_NORMAL;
		}
		font.m_style = static_cast<wxFontStyle>(st);
	}
	else if (ind == 4)
	{
		int wt = childValue.GetLong();
		if (wt != wxFONTWEIGHT_NORMAL &&
			wt != wxFONTWEIGHT_LIGHT &&
			wt != wxFONTWEIGHT_BOLD)
			wt = wxFONTWEIGHT_NORMAL;
		font.m_weight = static_cast<wxFontWeight>(wt);
	}
	else if (ind == 5)
	{
		font.m_underlined = childValue.GetBool();
	}

	thisValue = WXVARIANT(typeConv::FontToString(font));
	return thisValue;
}