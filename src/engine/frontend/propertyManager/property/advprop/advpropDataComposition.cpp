#include "advpropDataComposition.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"

#include "backend/propertyManager/property/propertyDataComposition.h"
#include "backend/propertyManager/property/propertyComposition.h"   // ibPropertyComposition — editor-less by design

// -----------------------------------------------------------------------
// ibPGDataCompositionProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataCompositionProperty, wxPGProperty, HyperLink)

// register frontend property — matched by the BACKEND type, which is why the two
// settings actions need no branch of their own.
class ibPropertyDataCompositionLoader
{
public:
	ibPropertyDataCompositionLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyDataComposition* prop) -> wxPGProperty* {
			return new ibPGDataCompositionProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});

		// Editor-less BY DESIGN — the OTHER composition property, the one the composer metatype
		// keeps its content in. It carries the composition itself, and the composition is edited in
		// the composer's own window, never in a grid cell. Declared here so the inspector answers
		// null quietly instead of asserting on a Register nobody forgot.
		ibPropertyRegistry::RegisterNoEditor<ibPropertyComposition>();
	}
}g_dataCompositionLoader;

ibPGDataCompositionProperty::ibPGDataCompositionProperty(ibPropertyObject* ownerProperty, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_ownerProperty(ownerProperty) {

	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	wxPGProperty::SetValue(wxVariant(false, wxT("hyperLink_clicked")));
}

ibPGDataCompositionProperty::~ibPGDataCompositionProperty()
{
}

wxString ibPGDataCompositionProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return _("Setup...");
}

bool ibPGDataCompositionProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

#include "backend/system/value/valueDataComposition.h"
#include "frontend/win/dlgs/settings/composer/composerSettings.h"

void ibPGDataCompositionProperty::OnSetValue()
{
	if (wxT("hyperLink_clicked") == m_value.GetName() && m_value.GetBool()) {
		// Owner = the property-object that CREATED this "Settings" action — the composition
		// itself (it CreateProperty's its own Settings property; whatever carries it only
		// copies that property onto itself). So the composition is reached through the owner
		// directly, and this file names no attribute and no control.
		ibValueDataComposition* composer = dynamic_cast<ibValueDataComposition*>(m_ownerProperty);
		if (composer != nullptr) {
			// CallAfter, not here: the click is still being delivered to the grid cell, and a
			// modal window opened inside that delivery takes the mouse with it.
			wxTheApp->CallAfter([composer]() { ibDialogComposerSettings::ShowComposerSettings(composer); });
		}
	}
}

void ibPGDataCompositionProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
