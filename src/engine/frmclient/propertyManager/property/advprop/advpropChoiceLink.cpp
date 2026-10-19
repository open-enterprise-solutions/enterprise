#include "advpropChoiceLink.h"

#include "frmclient/backend/propertyManager/property/variant/variantChoiceLink.h"

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/dlgs/choiceLink/choiceLink.h"               // where the governing field is chosen
#include "frmclient/win/dlgs/choiceParameters/choiceParameters.h"   // …and where the parameters' rows live
#include "frmclient/backend/propertyManager/property/propertyChoiceLink.h"   // ibChoiceHasLists — what the row brought

// -----------------------------------------------------------------------
// ibPGChoiceLinkProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGChoiceLinkProperty, wxPGProperty, TextCtrlAndButton)

// register frontend property — the two descriptions as the server stores them (ibChoiceLinkDescriptionMemory), named
// as the server named them (the row's Presentation), and back as one.
class ibPropertyChoiceLinkLoader
{
public:
	ibPropertyChoiceLinkLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyChoiceLink"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibChoiceTypeLinkDescription linkDesc;
			ibChoiceLinkDescriptionMemory::ReadNode(property->GetNodeValue(), linkDesc);
			return new ibPGChoiceLinkProperty(property, property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataChoiceLink(property->GetPropertyObject(), linkDesc, property->GetValue().GetString())));
		},
		[](const wxVariant& value) {
			ibDataValue node;
			if (const ibVariantDataChoiceLink* const data = property_cast(value, ibVariantDataChoiceLink))
				ibChoiceLinkDescriptionMemory::WriteNode(node, data->GetLinkDesc());
			return node;
		});
		ibPropertyRegistry::Register(wxT("ibPropertyChoiceParameters"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibChoiceParametersDescription paramsDesc;
			ibChoiceLinkDescriptionMemory::ReadNode(property->GetNodeValue(), paramsDesc);
			return new ibPGChoiceParametersProperty(property, property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataChoiceParameters(property->GetPropertyObject(), paramsDesc, property->GetValue().GetString())));
		},
		[](const wxVariant& value) {
			ibDataValue node;
			if (const ibVariantDataChoiceParameters* const data = property_cast(value, ibVariantDataChoiceParameters))
				ibChoiceLinkDescriptionMemory::WriteNode(node, data->GetParametersDesc());
			return node;
		});
	}
} g_choiceLinkLoader;

ibPGChoiceLinkProperty::ibPGChoiceLinkProperty(ibBackendProperty* property, const wxString& label,
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

ibPGChoiceParametersProperty::ibPGChoiceParametersProperty(ibBackendProperty* property, const wxString& label,
	const wxString& strName, const wxVariant& value)
	: wxPGProperty(label, strName), m_property(property)
{
	SetValue(value);
}

bool ibPGChoiceParametersProperty::OnEvent(wxPropertyGrid* propgrid, wxWindow* WXUNUSED(primary), wxEvent& event)
{
	if (!propgrid->IsMainButtonEvent(event) || m_property == nullptr)
		return false;

	// ⚠ SAID, NOT DROPPED, WHERE THE ROW BROUGHT NO LISTS (an older server). The window's lists are the server
	// property's answers — the fields of what this one refers to, the fields beside it, what each could fill — written
	// on the row (its Targets and Sources). Opened without them it would tell the author the field refers to nothing.
	if (!ibChoiceHasLists(m_property)) {
		wxMessageBox(_("Editing the choice parameters is not available in the thin designer yet."),
			_("Choice parameter links"), wxOK | wxICON_INFORMATION, propgrid);
		return false;
	}

	ibVariantDataChoiceParameters* data = property_cast(m_value, ibVariantDataChoiceParameters);
	if (data == nullptr)
		return false;

	// ⭐ THE WINDOW EDITS A COPY, and the value is posted only if it says something changed — as the link's above, and
	// for the same reasons; the clone is held by a variant from the moment it exists.
	ibVariantDataChoiceParameters* clone = data->Clone();
	const wxVariant held(clone);

	if (!ibDialogChoiceParameters::ShowChoiceParametersDialog(m_property, clone->GetParametersDesc()))
		return false;

	propgrid->EditorsValueWasModified();
	SetValueInEvent(held);
	return true;
}
