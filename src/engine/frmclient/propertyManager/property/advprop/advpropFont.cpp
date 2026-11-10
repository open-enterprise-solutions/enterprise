#include "advpropFont.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the font's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "deskgui/typeconv.h"   // FontToWxFont / WxFontToFont — the dialog's font, and back

namespace {

// The value of a font: the same property's (the cell's own), its node the font's text as the server stores it, read as
// that text.
ibVariant WriteFont(const ibFont& font)
{
	const wxString text = font.ToString();
	return ibVariant(new ibVariantSerializable(ibDataValue::String(text), text));
}

} // namespace

// -----------------------------------------------------------------------
// ibPGFontProperty
// -----------------------------------------------------------------------

#include <wx/fontdlg.h>
#include <wx/fontenum.h>

// wx's tables, over ibFont's words — the same numbers. ibFont has no Unknown family.
static const wxChar* gs_fp_es_family_labels[] = {
	wxT("Default"), wxT("Decorative"),
	wxT("Roman"), wxT("Script"),
	wxT("Swiss"), wxT("Modern"),
	wxT("Teletype"),
	(const wxChar*)nullptr
};

static long gs_fp_es_family_values[] = {
	ibFONTFAMILY_DEFAULT, ibFONTFAMILY_DECORATIVE,
	ibFONTFAMILY_ROMAN, ibFONTFAMILY_SCRIPT,
	ibFONTFAMILY_SWISS, ibFONTFAMILY_MODERN,
	ibFONTFAMILY_TELETYPE
};

static const wxChar* gs_fp_es_style_labels[] = {
	wxT("Normal"),
	wxT("Slant"),
	wxT("Italic"),
	(const wxChar*)nullptr
};

static long gs_fp_es_style_values[] = {
	ibFONTSTYLE_NORMAL,
	ibFONTSTYLE_SLANT,
	ibFONTSTYLE_ITALIC
};

static const wxChar* gs_fp_es_weight_labels[] = {
	wxT("Thin"),
	wxT("ExtraLight"),
	wxT("Light"),
	wxT("Normal"),
	wxT("Medium"),
	wxT("SemiBold"),
	wxT("Bold"),
	wxT("ExtraBold"),
	wxT("Heavy"),
	wxT("ExtraHeavy"),
	(const wxChar*)nullptr
};

static long gs_fp_es_weight_values[] = {
	ibFONTWEIGHT_THIN,
	ibFONTWEIGHT_EXTRALIGHT,
	ibFONTWEIGHT_LIGHT,
	ibFONTWEIGHT_NORMAL,
	ibFONTWEIGHT_MEDIUM,
	ibFONTWEIGHT_SEMIBOLD,
	ibFONTWEIGHT_BOLD,
	ibFONTWEIGHT_EXTRABOLD,
	ibFONTWEIGHT_HEAVY,
	ibFONTWEIGHT_EXTRAHEAVY
};

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGFontProperty, ibEditorDialogProperty, TextCtrlAndButton)

// register frontend property — the cell holds the font as the server writes it, so it goes back as it is
class ibPropertyFontLoader
{
public:
	ibPropertyFontLoader() {
		ibPropertyRegistry::Register(wxT("ibPropertyFont"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGFontProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
} g_fontLoader;

ibPGFontProperty::ibPGFontProperty(const wxString& label, const wxString& name,
	const ibVariant& value)
	: ibEditorDialogProperty(label, name)
{
	SetValue(value);

	// Initialize font family choices list
	if (!ibPGGlobalVars->m_fontFamilyChoices)
	{
		wxArrayString faceNames = wxFontEnumerator::GetFacenames();

		faceNames.Sort();

		ibPGGlobalVars->m_fontFamilyChoices = new ibPGChoices(faceNames);
	}

	const ibFont font = GetVal();

	/* TRANSLATORS: Label of font point size */
	AddPrivateChild(new ibIntProperty(_("Point Size"),
		wxS("Point Size"), (long)font.m_pointSize));

	const wxString faceName = font.m_faceName;
	// If font was not in there, add it now
	if (!faceName.empty() &&
		ibPGGlobalVars->m_fontFamilyChoices->Index(faceName) == wxNOT_FOUND)
		ibPGGlobalVars->m_fontFamilyChoices->AddAsSorted(faceName);

	/* TRANSLATORS: Label of font face name */
	ibPGProperty* p = new ibEnumProperty(_("Face Name"), wxS("Face Name"),
		*ibPGGlobalVars->m_fontFamilyChoices);

	p->SetValueFromString(faceName, ibPGPropValFormatFlags::FullValue);

	AddPrivateChild(p);

	/* TRANSLATORS: Label of font style */
	AddPrivateChild(new ibEnumProperty(_("Style"), wxS("Style"),
		gs_fp_es_style_labels, gs_fp_es_style_values,
		font.m_style));

	/* TRANSLATORS: Label of font weight */
	AddPrivateChild(new ibEnumProperty(_("Weight"), wxS("Weight"),
		gs_fp_es_weight_labels, gs_fp_es_weight_values,
		font.m_weight));

	/* TRANSLATORS: Label of underlined font */
	AddPrivateChild(new ibBoolProperty(_("Underlined"), wxS("Underlined"),
		font.m_underlined));

	/* TRANSLATORS: Label of font family */
	AddPrivateChild(new ibEnumProperty(_("Family"), wxS("Family"),
		gs_fp_es_family_labels, gs_fp_es_family_values,
		font.m_family));
}

bool ibPGFontProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
	wxFontData data;
	data.SetInitialFont(typeConv::FontToWxFont(GetVal(&value)));
	data.SetColour(*wxBLACK);

	wxFontDialog dlg(pg->GetPanel(), data);
	if (!m_dlgTitle.empty())
	{
		dlg.SetTitle(m_dlgTitle);
	}
	if (dlg.ShowModal() == wxID_OK)
	{
		value = WriteFont(typeConv::WxFontToFont(dlg.GetFontData().GetChosenFont()));
		return true;
	}

	return false;
}

ibFont ibPGFontProperty::GetVal(const ibVariant* pVariant) const
{
	if (!pVariant)
		pVariant = &m_value;

	ibFont font;
	if (!font.FromString(ibVariantSerializable::GetNodeValue(*pVariant).AsString()) || !font.IsOk())
		font = typeConv::WxFontToFont(*wxNORMAL_FONT);
	return font;
}

void ibPGFontProperty::RefreshChildren()
{
	if (!GetChildCount()) return;
	const ibFont font = GetVal();
	Item(0)->SetValue((long)font.m_pointSize);
	Item(1)->SetValueFromString(font.m_faceName, ibPGPropValFormatFlags::FullValue);
	Item(2)->SetValue((long)font.m_style);
	Item(3)->SetValue((long)font.m_weight);
	Item(4)->SetValue(font.m_underlined);
	Item(5)->SetValue((long)font.m_family);
}

ibVariant ibPGFontProperty::ChildChanged(ibVariant& thisValue,
	int ind,
	ibVariant& childValue) const
{
	ibFont font = GetVal(&thisValue);

	if (ind == 0)
	{
		font.m_pointSize = childValue.GetInteger();
	}
	else if (ind == 1)
	{
		wxString faceName;
		int faceIndex = static_cast<int>(childValue.GetInteger());

		if (faceIndex >= 0)
			faceName = ibPGGlobalVars->m_fontFamilyChoices->GetLabel(faceIndex);

		font.m_faceName = faceName;
	}
	else if (ind == 2)
	{
		int st = static_cast<int>(childValue.GetInteger());
		if (st != ibFONTSTYLE_NORMAL &&
			st != ibFONTSTYLE_SLANT &&
			st != ibFONTSTYLE_ITALIC)
			st = ibFONTSTYLE_NORMAL;
		font.m_style = static_cast<ibFontStyle>(st);
	}
	else if (ind == 3)
	{
		int wt = static_cast<int>(childValue.GetInteger());
		if (wt < ibFONTWEIGHT_THIN || wt > ibFONTWEIGHT_EXTRAHEAVY)
			wt = ibFONTWEIGHT_NORMAL;
		font.m_weight = wt;
	}
	else if (ind == 4)
	{
		font.m_underlined = childValue.GetBoolean();
	}
	else if (ind == 5)
	{
		int fam = static_cast<int>(childValue.GetInteger());
		if (fam < ibFONTFAMILY_DEFAULT ||
			fam > ibFONTFAMILY_TELETYPE)
			fam = ibFONTFAMILY_DEFAULT;
		font.m_family = static_cast<ibFontFamily>(fam);
	}

	return WriteFont(font);
}
