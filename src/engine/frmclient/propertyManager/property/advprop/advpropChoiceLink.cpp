#include "advpropChoiceLink.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the description's node
#include "core/description/choiceLinkDescription.h"                                   // ibChoiceLinkDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/dlgs/choiceLink/choiceLink.h"               // where the governing field is chosen
#include "frmclient/win/dlgs/choiceParameters/choiceParameters.h"   // …and where the parameters' rows live
#include "frmclient/backend/propertyManager/property/propertyChoiceLink.h"   // ibChoiceHasLists — what the row brought
#include "deskgui/univ/messageDialog.h"

// -----------------------------------------------------------------------
// ibPGChoiceLinkProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGChoiceLinkProperty, ibPGProperty, TextCtrlAndButton)

namespace {

// The description a value holds, as the server stores it…
template <typename typeDescription>
typeDescription ReadDescription(const ibVariant& value)
{
	typeDescription desc;
	ibChoiceLinkDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(value), desc);
	return desc;
}

// …and the value of another one: the same property's, read as the server named the fields until its rows come back
// (none: nothing).
template <typename typeDescription>
ibVariant WriteDescription(const ibVariant& value, const typeDescription& desc)
{
	ibDataValue node;
	ibChoiceLinkDescriptionMemory::WriteNode(node, desc);
	return ibVariant(new ibVariantSerializable(node, desc.IsOk() ? value.GetString() : ibString()));
}

}

// register frontend property — the two descriptions as the server stores them (ibChoiceLinkDescriptionMemory), named
// as the server named them (the row's Presentation), and back as one.
class ibPropertyChoiceLinkLoader
{
public:
	ibPropertyChoiceLinkLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyChoiceLink"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGChoiceLinkProperty(property, property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyChoiceParameters"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGChoiceParametersProperty(property, property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
} g_choiceLinkLoader;

ibPGChoiceLinkProperty::ibPGChoiceLinkProperty(ibBackendProperty* property, const wxString& label,
	const wxString& strName, const ibVariant& value)
	: ibPGProperty(label, strName), m_property(property)
{
	SetValue(value);
}

bool ibPGChoiceLinkProperty::OnEvent(ibPropertyGrid* propgrid, wxWindow* WXUNUSED(primary), wxEvent& event)
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

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGChoiceParametersProperty, ibPGProperty, TextCtrlAndButton)

ibPGChoiceParametersProperty::ibPGChoiceParametersProperty(ibBackendProperty* property, const wxString& label,
	const wxString& strName, const ibVariant& value)
	: ibPGProperty(label, strName), m_property(property)
{
	SetValue(value);
}

bool ibPGChoiceParametersProperty::OnEvent(ibPropertyGrid* propgrid, wxWindow* WXUNUSED(primary), wxEvent& event)
{
	if (!propgrid->IsMainButtonEvent(event) || m_property == nullptr)
		return false;

	// ⚠ SAID, NOT DROPPED, WHERE THE ROW BROUGHT NO LISTS (an older server). The window's lists are the server
	// property's answers — the fields of what this one refers to, the fields beside it, what each could fill — written
	// on the row (its Targets and Sources). Opened without them it would tell the author the field refers to nothing.
	if (!ibChoiceHasLists(m_property)) {
		ibMessageBox(_("Editing the choice parameters is not available in the thin designer yet."),
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
