#include "advpropDataComposition.h"

#include "core/description/compositionDescription.h"                         // the composition, as the row holds it
#include "core/serialize/dataBuilder.h"

#include "frmclient/backend/metaData.h"                                       // the owner's configuration — its tab
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the composition's node
#include "frmclient/backend/query/queryConstructorModel.h"                   // ibQueryFieldsForget — the window's fields
#include "frmclient/propertyManager/property/private/prop.h"                 // ibPGPropertyFlags_* — the grid flags
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/dlgs/settings/composer/composerSettings.h"           // the window it opens
#include "frmclient/win/dlgs/settings/list/listSettings.h"                   // …and a dynamic list's

// -----------------------------------------------------------------------
// ibPGDataCompositionProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataCompositionProperty, ibPGProperty, HyperLink)

namespace {

// The composition a value holds, as the server stores it…
ibCompositionDescription ReadComposition(const ibVariant& value)
{
	ibCompositionDescription desc;
	ibCompositionDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(value), desc);
	return desc;
}

// …and the value of another one: the same property's, read as its query — what a composition is for.
ibVariant WriteComposition(const ibCompositionDescription& desc)
{
	ibDataValue node;
	ibCompositionDescriptionMemory::WriteNode(node, desc);
	return ibVariant(new ibVariantSerializable(node, desc.m_query));
}

} // namespace

// register frontend property — by the class the server names the row with
class ibPropertyDataCompositionLoader
{
public:
	ibPropertyDataCompositionLoader()
	{
		// A COMPOSITION on a form attribute opens the composer's window — outputs, resources, parameters, the query —
		// on the author's road: the description itself is edited and written back. A DYNAMIC LIST opens the list's —
		// filter, sort, grouping and its query — over the same description; its fields, the main table's and the
		// query's, are asked of the form's tab (Kind Source).
		auto registerCell = [](const wxChar* className, ibPGDataCompositionProperty::ibSettingsDoor door) {
			ibPropertyRegistry::Register(className,
				[door](ibBackendProperty* property) -> ibPGProperty* {
					return new ibPGDataCompositionProperty(property->GetPropertyObject(), property->GetLabel(),
						property->GetName(), property->GetValue(), door);
				});
		};
		registerCell(wxT("ibPropertyDataComposition"), [](ibCompositionDescription& edited, const ibMetaData* metaData) {
			return ibDialogComposerSettings::ShowComposerSettings(nullptr, edited, metaData);
		});
		registerCell(wxT("ibPropertyDynamicList"), [](ibCompositionDescription& edited, const ibMetaData* metaData) {
			return ibDialogListSettings::ShowListSettings(edited, metaData);
		});
	}
} g_dataCompositionLoader;

ibPGDataCompositionProperty::ibPGDataCompositionProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const ibVariant& value, ibSettingsDoor door)
	: ibPGProperty(label, name), m_property(property), m_door(std::move(door))
{
	ibPGProperty::SetFlagRecursively(ibPGFlags::ReadOnly, true);
	ibPGProperty::SetFlagRecursively(ibPGFlags::NoEditor, true);

	// The cell's value is the variant it was given — the composition the row brought.
	ibPGProperty::SetValue(value);
}

ibPGDataCompositionProperty::~ibPGDataCompositionProperty()
{
}

wxString ibPGDataCompositionProperty::ValueToString(ibVariant& WXUNUSED(value), ibPGPropValFormatFlags WXUNUSED(flags)) const
{
	return _("Setup...");
}

bool ibPGDataCompositionProperty::StringToValue(ibVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	ibPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;
}

bool ibPGDataCompositionProperty::OnEvent(ibPropertyGrid* WXUNUSED(propgrid), wxWindow* WXUNUSED(wnd_primary),
	wxEvent& event)
{
	// THE CLICK (the hyperlink's editor, propertyEditor.cpp).
	if (event.GetEventType() != wxEVT_LEFT_DOWN)
		return false;

	// HERE, NOT DEFERRED — inside the editor's event is where a pending value is collected (the desktop's, see there).
	ibCompositionDescription edited = ReadComposition(m_value);

	const ibMetaData* const metaData = m_property != nullptr ? m_property->GetMetaData() : nullptr;
	const bool accepted = m_door && m_door(edited, metaData);

	// The fields the window asked of the owner's tab were the window's — the next one asks again.
	if (metaData != nullptr && metaData->GetTabId() != 0)
		ibQueryFieldsForget(metaData->GetTabId());

	if (!accepted)
		return false;

	// Posted as the pending value: the grid validates and announces it, the inspector sends it — the row's node.
	SetValueInEvent(WriteComposition(edited));
	return true;
}

void ibPGDataCompositionProperty::RefreshChildren()
{
	ibPGProperty::Enable(true);
}
