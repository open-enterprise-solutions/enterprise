#include "advpropDataComposition.h"

#include "core/description/compositionDescription.h"                         // the composition, as the row holds it
#include "core/serialize/dataBuilder.h"

#include "frmclient/backend/metaData.h"                                       // the owner's configuration — its tab
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the composition's node
#include "frmclient/backend/query/queryConstructorModel.h"                   // ibQueryFieldsForget — the window's fields
#include "frmclient/propertyManager/property/private/prop.h"                 // wxPGPropertyFlags_* — the grid flags
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/dlgs/settings/composer/composerSettings.h"           // the window it opens
#include "frmclient/win/dlgs/settings/list/listSettings.h"                   // …and a dynamic list's

// -----------------------------------------------------------------------
// ibPGDataCompositionProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataCompositionProperty, wxPGProperty, HyperLink)

namespace {

// The composition a value holds, as the server stores it…
ibCompositionDescription ReadComposition(const wxVariant& value)
{
	ibCompositionDescription desc;
	ibCompositionDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), desc);
	return desc;
}

// …and the value of another one: the same property's, read as its query — what a composition is for.
wxVariant WriteComposition(const wxVariant& value, const ibCompositionDescription& desc)
{
	ibDataValue node;
	ibCompositionDescriptionMemory::WriteNode(node, desc);
	return wxVariant(new ibVariantDataSerializable(value.GetType(), node, desc.m_query));
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
				[door](ibBackendProperty* property) -> wxPGProperty* {
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
	const wxString& name, const wxVariant& value, ibSettingsDoor door)
	: wxPGProperty(label, name), m_property(property), m_door(std::move(door))
{
	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	// The cell's value is the variant it was given — the composition the row brought.
	wxPGProperty::SetValue(value);
}

ibPGDataCompositionProperty::~ibPGDataCompositionProperty()
{
}

wxString ibPGDataCompositionProperty::ValueToString(wxVariant& WXUNUSED(value), wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return _("Setup...");
}

bool ibPGDataCompositionProperty::StringToValue(wxVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;
}

void ibPGDataCompositionProperty::OnSetValue()
{
	// THE NAME IS THE CLICK (propertyEditor.cpp) — taken, so ended: a name left standing turns one click into every
	// later refresh.
	if (wxT("hyperLink_clicked") != m_value.GetName())
		return;
	m_value.SetName(wxEmptyString);

	// HERE, NOT DEFERRED — inside the editor's event is where a pending value is collected (the desktop's, see there).
	ibCompositionDescription edited = ReadComposition(m_value);

	const ibMetaData* const metaData = m_property != nullptr ? m_property->GetMetaData() : nullptr;
	const bool accepted = m_door && m_door(edited, metaData);

	// The fields the window asked of the owner's tab were the window's — the next one asks again.
	if (metaData != nullptr && metaData->GetTabId() != 0)
		ibQueryFieldsForget(metaData->GetTabId());

	if (!accepted)
		return;

	// Posted as the pending value: the grid validates and announces it, the inspector sends it — the row's node.
	SetValueInEvent(WriteComposition(m_value, edited));
}

void ibPGDataCompositionProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
