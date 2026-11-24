#include "advpropChoices.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the description's node
#include "core/description/choicesDescription.h"                                      // ibChoicesDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/dlgs/choices/choices.h"   // where the items live

// register frontend property — the items as the server stores them (ibChoicesDescriptionMemory), named as the server
// named them (the row's Presentation), and back as one.
class ibPropertyChoicesLoader
{
public:
	ibPropertyChoicesLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyChoices"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGChoicesProperty(property, property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
} g_choicesLoader;

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGChoicesProperty, ibPGProperty, TextCtrlAndButton)

ibPGChoicesProperty::ibPGChoicesProperty(ibBackendProperty* property, const wxString& label,
	const wxString& strName, const ibVariant& value)
	: ibPGProperty(label, strName), m_property(property)
{
	SetValue(value);
}

bool ibPGChoicesProperty::OnEvent(ibPropertyGrid* propgrid, wxWindow* WXUNUSED(primary), wxEvent& event)
{
	if (!propgrid->IsMainButtonEvent(event) || m_property == nullptr)
		return false;

	// ⭐ THE WINDOW EDITS A COPY, and the value is posted only if it says something changed — as the choice parameters'
	// row does (advpropChoiceLink.cpp), and for the same reasons.
	ibChoicesDescription choicesDesc;
	ibChoicesDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(m_value), choicesDesc);
	if (!ibDialogChoices::ShowChoicesDialog(m_property, choicesDesc))
		return false;

	// …named as the server named them until its row comes back (none: nothing).
	ibDataValue node;
	ibChoicesDescriptionMemory::WriteNode(node, choicesDesc);
	propgrid->EditorsValueWasModified();
	SetValueInEvent(ibVariant(new ibVariantSerializable(node, choicesDesc.IsOk() ? m_value.GetString() : ibString())));
	return true;
}
