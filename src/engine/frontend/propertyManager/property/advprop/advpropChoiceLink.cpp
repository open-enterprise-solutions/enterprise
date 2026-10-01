#include "advpropChoiceLink.h"

#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"
#include "frontend/win/dlgs/choiceLink/choiceLink.h"               // where the governing field is chosen
#include "frontend/win/dlgs/choiceParameters/choiceParameters.h"   // …and where the rows are edited

// -----------------------------------------------------------------------
// ibPGChoiceLinkProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGChoiceLinkProperty, wxPGProperty, TextCtrlAndButton)

// register frontend property
class ibPropertyChoiceLinkLoader
{
public:
	ibPropertyChoiceLinkLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyChoiceLink* prop) -> wxPGProperty* {
			return new ibPGChoiceLinkProperty(prop, prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
		ibPropertyRegistry::Register([](ibPropertyChoiceParameters* prop) -> wxPGProperty* {
			return new ibPGChoiceParametersProperty(prop, prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
} g_choiceLinkLoader;

ibPGChoiceLinkProperty::ibPGChoiceLinkProperty(ibPropertyChoiceLink* property, const wxString& label,
	const wxString& strName, const wxVariant& value)
	: wxPGProperty(label, strName), m_property(property)
{
	SetValue(value);
}

bool ibPGChoiceLinkProperty::OnEvent(wxPropertyGrid* propgrid, wxWindow* WXUNUSED(primary), wxEvent& event)
{
	if (!propgrid->IsMainButtonEvent(event) || m_property == nullptr)
		return false;

	ibVariantDataChoiceLink* data = property_cast(m_value, ibVariantDataChoiceLink);
	if (data == nullptr)
		return false;

	// ⭐ THE WINDOW EDITS A COPY, and the value is posted only if it says something changed — the same
	// arrangement as its companion below, for the same two reasons: Cancel stays a real cancel all the
	// way up to the property, and an OK that changed nothing does not mark the configuration modified.
	//
	// ⭐ THE CLONE IS HELD BY A VARIANT FROM THE MOMENT IT EXISTS — Clone() hands back a raw pointer and
	// there are two ways out of here, refused or set; a variant owns what it is given, so the refused
	// way releases it instead of leaking.
	ibVariantDataChoiceLink* clone = data->Clone();
	const wxVariant held(clone);

	if (!ibDialogChoiceLink::ShowChoiceLinkDialog(m_property, clone->GetLinkDesc()))
		return false;

	propgrid->EditorsValueWasModified();
	SetValueInEvent(held);
	return true;
}


// -----------------------------------------------------------------------
// ibPGChoiceParametersProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGChoiceParametersProperty, wxPGProperty, TextCtrlAndButton)

ibPGChoiceParametersProperty::ibPGChoiceParametersProperty(ibPropertyChoiceParameters* property, const wxString& label,
	const wxString& strName, const wxVariant& value)
	: wxPGProperty(label, strName), m_property(property)
{
	SetValue(value);
}

bool ibPGChoiceParametersProperty::OnEvent(wxPropertyGrid* propgrid, wxWindow* WXUNUSED(primary), wxEvent& event)
{
	if (!propgrid->IsMainButtonEvent(event) || m_property == nullptr)
		return false;

	ibVariantDataChoiceParameters* data = property_cast(m_value, ibVariantDataChoiceParameters);
	if (data == nullptr)
		return false;

	// ⭐ THE WINDOW EDITS A COPY, and the value is posted only if it says something changed. Two things
	// depend on that: Cancel stays a real cancel all the way up to the property, and an OK that changed
	// nothing does not mark the configuration modified.
	//
	// ⭐ THE CLONE IS HELD BY A VARIANT FROM THE MOMENT IT EXISTS — Clone() hands back a raw pointer and
	// there are two ways out of here, refused or set; a variant owns what it is given, so the refused
	// way releases it instead of leaking (the composition property records the same lesson).
	ibVariantDataChoiceParameters* clone = data->Clone();
	const wxVariant held(clone);

	if (!ibDialogChoiceParameters::ShowChoiceParametersDialog(m_property, clone->GetParametersDesc()))
		return false;

	propgrid->EditorsValueWasModified();
	SetValueInEvent(held);
	return true;
}
