#include "advpropChoiceLink.h"

#include "frmclient/backend/propertyManager/property/variant/variantChoiceLink.h"

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/dlgs/choiceLink/choiceLink.h"               // where the governing field is chosen

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

	// ⚠ SAID, NOT DROPPED. The window is the desktop's, whole (frmclient/win/dlgs/choiceParameters), and its lists are
	// the server property's answers the row does not bring yet — the fields of what this one refers to, the fields
	// beside it, what each could fill (ibPropertyChoiceParameters::GetParameterList / GetSourceList). Opened empty it
	// would tell the author the field refers to nothing; so the button says what is true instead.
	wxMessageBox(_("Editing the choice parameters is not available in the thin designer yet."),
		_("Choice parameter links"), wxOK | wxICON_INFORMATION, propgrid);
	return false;
}
