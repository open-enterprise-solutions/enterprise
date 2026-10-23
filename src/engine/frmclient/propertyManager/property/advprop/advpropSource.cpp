#include "advpropSource.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the binding's node
#include "core/description/sourceDescription.h"                                  // ibSourceDescriptionMemory — the binding

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

// …and the type it holds, beside the path in the same value (SourceType: its node and its words) — the same property's,
// as the Type row reads it; unbound, or bound and not yet named by the server — none.
wxVariant ReadSourceType(const wxVariant& value)
{
	const ibDataValue node = ibVariantDataSerializable::GetNodeValue(value);
	const ibDataNode* const type = node.AsChild() != nullptr
		? node.AsChild()->FindChild(ibProtocol::ibSchema::ibProperties::SourceType) : nullptr;
	return wxVariant(new ibVariantDataSerializable(value.GetType(),
		type != nullptr ? type->GetProperty(ibProtocol::ibSchema::ibProperties::Value) : ibDataValue(),
		type != nullptr ? type->GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Presentation) : wxString()));
}

}

// register frontend property — ibPropertySource; the binding as the server stores it (ibSourceDescriptionMemory: the
// hop path) and the type beside it, read as it wrote them (the row's Presentation, SourceType's), and back as a path;
// its window's tree asked as the window opens (the picker's).
class ibPropertySourceLoader
{
public:
	ibPropertySourceLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySource"), [](ibBackendProperty* property) -> wxPGProperty* {
			// …what it may be bound to, as the row says (an older server's says nothing: an attribute).
			using ibSourceDataType = ibProtocol::ibSchema::ibProperties::ibSourceDataType;
			ibSourceDataType filter = ibSourceDataType::Attribute;
			std::vector<ibClassID> types;
			wxVariant value = property->GetValue();
			ibDataNode row;
			if (property->GetProtocolValue(row)) {
				if (row.FindField(ibProtocol::ibSchema::ibProperties::SourceDataType) != nullptr)
					filter = static_cast<ibSourceDataType>(row.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::SourceDataType));
				// …the classes its Type row offers, in the server's order (Classes);
				if (const ibDataNode* const classes = row.FindChild(ibProtocol::ibSchema::ibProperties::Classes)) {
					for (const ibDataNode& entry : classes->Children()) {
						unsigned long long clsid = 0;
						entry.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Type).ToULongLong(&clsid);
						types.push_back(static_cast<ibClassID>(clsid));
					}
				}
				// …and the value as the server wrote it, both halves (the path, SourceType beside it), while it is the
				// binding the form holds: the form's node, drawn anew, carries the path alone.
				const ibDataValue stated = row.GetProperty(ibProtocol::ibSchema::ibProperties::Value);
				ibSourceDescription written;
				ibSourceDescriptionMemory::ReadNode(stated, written);
				if (written.GetPath() == ReadSource(value).GetPath())
					value = wxVariant(new ibVariantDataSerializable(value.GetType(), stated, value.GetString()));
			}
			return new ibPGDataSourceProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				value, filter, types);
		});
	}
}g_sourceLoader;

ibPGDataSourceProperty::ibPGDataSourceProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& strName, const wxVariant& value, ibProtocol::ibSchema::ibProperties::ibSourceDataType filter,
	const std::vector<ibClassID>& types) :
	wxPGProperty(label, strName), m_owner(property), m_filterSourceDataType(filter)
{
	// The Type row — the desktop's, over the type the binding holds (RefreshChildren), read only: its window offers the
	// classes the row named (Classes — `types`), composite unless the control's is a table's shape (SourceType's Multiple).
	const ibDataValue node = ibVariantDataSerializable::GetNodeValue(value);
	const ibDataNode* const type = node.AsChild() != nullptr
		? node.AsChild()->FindChild(ibProtocol::ibSchema::ibProperties::SourceType) : nullptr;
	m_typeSelector = new ibPGTypeProperty(types, type != nullptr && type->GetValue<bool>(ibProtocol::ibSchema::ibProperties::Multiple),
		_("Type"), wxT("type"));
	AddPrivateChild(m_typeSelector);

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

void ibPGDataSourceProperty::RefreshChildren()
{
	// The desktop's: no binding — no Type at all (the control's type comes from the source); a bound one's — the type
	// beside its path, handed to the Type row, which shows what it holds (and hides the qualifiers it has none of), read
	// only.
	m_typeSelector->Hide(!ReadSource(m_value).IsOk());
	m_typeSelector->SetValue(ReadSourceType(m_value));
	m_typeSelector->SetFlagRecursively(wxPGFlags::ReadOnly, true);
	ibPGDataSourceProperty::SetExpanded(true);
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
			if (!SourcePicker()(pg, cell->GetOwner(), cell->GetName(), cell->GetFilterSourceDataType(), binding))
				return false;
			// …a binding the same as it was changes nothing.
			if (binding.GetPath() == current.GetPath())
				return false;
			// …its words and the type beneath are the server's, with the control's rows it answers the edit with.
			SetValue(WriteSource(value, binding));
			return true;
		}
	};

	return new ibPGEditorDataSourceDialogAdapter();
}
