#include "advpropSource.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the binding's node
#include "core/description/sourceDescription.h"                                  // ibSourceDescriptionMemory — Parent

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataSourceProperty, wxPGProperty, TextCtrlAndButton)

namespace {

ibSourcePicker& SourcePicker()
{
	static ibSourcePicker picker;
	return picker;
}

}

void ibSetSourcePicker(const ibSourcePicker& picker)
{
	SourcePicker() = picker;
}

namespace {

// The binding a value holds, as the server stores it.
ibSourceDescription ReadSource(const wxVariant& value)
{
	ibSourceDescription desc;
	ibSourceDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), desc);
	return desc;
}

// …and the value of another one: the same property's, the words the server's to say.
wxVariant WriteSource(const wxVariant& value, const ibSourceDescription& desc)
{
	ibDataValue node;
	ibSourceDescriptionMemory::WriteNode(node, desc);
	return wxVariant(new ibVariantDataSerializable(value.GetType(), node, wxEmptyString));
}

}

// register frontend property — ibPropertySource; the binding as the server stores it (ibSourceDescriptionMemory: the
// hop path), read as it wrote it (the row's Presentation), and back as one.
class ibPropertySourceLoader
{
public:
	ibPropertySourceLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySource"), [](ibBackendProperty* property) -> wxPGProperty* {
			// …what it may be bound to, as the row says (an older server's says nothing: an attribute).
			using ibSourceDataType = ibProtocol::ibSchema::ibProperties::ibSourceDataType;
			ibSourceDataType filter = ibSourceDataType::Attribute;
			ibSourceDescription table;
			ibDataNode row;
			if (property->GetProtocolValue(row)) {
				if (row.FindField(ibProtocol::ibSchema::ibProperties::SourceDataType) != nullptr)
					filter = static_cast<ibSourceDataType>(row.GetValue<s32>(ibProtocol::ibSchema::ibProperties::SourceDataType));
				if (const ibDataValue* const parent = row.FindProperty(ibProtocol::ibSchema::ibProperties::Parent))
					ibSourceDescriptionMemory::ReadNode(*parent, table);
			}
			return new ibPGDataSourceProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue(), filter, table);
		});
	}
}g_sourceLoader;

// (The desktop's builds the Type row here, over the owner's source factory — see the header.)
ibPGDataSourceProperty::ibPGDataSourceProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& strName, const wxVariant& value, ibProtocol::ibSchema::ibProperties::ibSourceDataType filter,
	const ibSourceDescription& table) :
	wxPGProperty(label, strName), m_owner(property), m_filterSourceDataType(filter), m_table(table)
{
	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGDataSourceProperty::ValueToString(wxVariant& variant,
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	// …as the server wrote it; nothing bound, or bound and not yet named by the server — not selected.
	const wxString words = variant.GetString();
	return ReadSource(variant).IsOk() && !words.IsEmpty() ? words : wxString(_("<not selected>"));
}

bool ibPGDataSourceProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	// An empty text unbinds.
	if (!text.IsEmpty())
		return false;
	ibSourceDescription desc = ReadSource(variant);
	desc.ClearSource();
	variant = WriteSource(variant, desc);
	return true;
}

wxPGEditorDialogAdapter* ibPGDataSourceProperty::GetEditorDialog() const
{
	class ibPGEditorDataSourceDialogAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override
		{
			ibPGDataSourceProperty* const cell = wxDynamicCast(prop, ibPGDataSourceProperty);
			// No form holds the control (no picker set) — said, as before.
			if (cell == nullptr || !SourcePicker()) {
				wxMessageBox(_("Choosing a data source is not available in the thin designer yet."),
					_("Choice source"), wxOK | wxICON_INFORMATION, pg);
				return false;
			}
			const wxVariant value = cell->GetValue();
			const ibSourceDescription current = ReadSource(value);
			ibSourceDescription binding = current;
			if (!SourcePicker()(pg, cell->GetOwner(), cell->GetFilterSourceDataType(), cell->GetTable(), binding))
				return false;
			// …a binding the same as it was changes nothing.
			if (binding.GetPath() == current.GetPath())
				return false;
			SetValue(WriteSource(value, binding));
			return true;
		}
	};

	return new ibPGEditorDataSourceDialogAdapter();
}
