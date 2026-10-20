#include "advpropChoiceLink.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the description's node
#include "core/description/choiceLinkDescription.h"                                   // ibChoiceLinkDescriptionMemory

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

namespace {

// The description a value holds, as the server stores it…
template <typename typeDescription>
typeDescription ReadDescription(const wxVariant& value)
{
	typeDescription desc;
	ibChoiceLinkDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), desc);
	return desc;
}

// …and the value of another one: the same property's, read as the server named the fields until its rows come back
// (none: nothing).
template <typename typeDescription>
wxVariant WriteDescription(const wxVariant& value, const typeDescription& desc)
{
	ibDataValue node;
	ibChoiceLinkDescriptionMemory::WriteNode(node, desc);
	return wxVariant(new ibVariantDataSerializable(value.GetType(), node, desc.IsOk() ? value.GetString() : wxString()));
}

}

// register frontend property — the two descriptions as the server stores them (ibChoiceLinkDescriptionMemory), named
// as the server named them (the row's Presentation), and back as one.
class ibPropertyChoiceLinkLoader
{
public:
	ibPropertyChoiceLinkLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyChoiceLink"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGChoiceLinkProperty(property, property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyChoiceParameters"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGChoiceParametersProperty(property, property->GetLabel(), property->GetName(), property->GetValue());
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

	// ⭐ THE WINDOW EDITS A COPY, and the value is posted only if it says something changed — the same
	// arrangement as its companion below, for the same two reasons: Cancel stays a real cancel all the
	// way up to the property, and an OK that changed nothing does not mark the configuration modified.
	ibChoiceTypeLinkDescription linkDesc = ReadDescription<ibChoiceTypeLinkDescription>(m_value);
	if (!ibDialogChoiceLink::ShowChoiceLinkDialog(m_property, linkDesc))
		return false;

	propgrid->EditorsValueWasModified();
	SetValueInEvent(WriteDescription(m_value, linkDesc));
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

	// ⭐ THE WINDOW EDITS A COPY, and the value is posted only if it says something changed — as the link's above, and
	// for the same reasons.
	ibChoiceParametersDescription paramsDesc = ReadDescription<ibChoiceParametersDescription>(m_value);
	if (!ibDialogChoiceParameters::ShowChoiceParametersDialog(m_property, paramsDesc))
		return false;

	propgrid->EditorsValueWasModified();
	SetValueInEvent(WriteDescription(m_value, paramsDesc));
	return true;
}
