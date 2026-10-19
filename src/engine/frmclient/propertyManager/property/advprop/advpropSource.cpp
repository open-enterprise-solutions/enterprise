#include "advpropSource.h"

#include "frmclient/backend/propertyManager/property/variant/variantSource.h"   // ibVariantDataSource
#include "protocol/protocol.h"                                                   // ibProtocol::ibSchema::ibProperties
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

// register frontend property — ibPropertySource; the binding as the server stores it (ibSourceDescriptionMemory: the
// hop path), read as it wrote it (the row's Presentation), and back as one.
class ibPropertySourceLoader
{
public:
	ibPropertySourceLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySource"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibSourceDescription sourceDesc;
			ibSourceDescriptionMemory::ReadNode(property->GetNodeValue(), sourceDesc);
			// …and what it may be bound to, as the row says (an older server's says nothing: a field).
			int kind = 2;
			ibSourceDescription table;
			ibDataNode row;
			if (property->GetProtocolValue(row)) {
				if (row.FindField(ibProtocol::ibSchema::ibProperties::SourceKind) != nullptr)
					kind = row.GetValue<s32>(ibProtocol::ibSchema::ibProperties::SourceKind);
				if (const ibDataValue* const parent = row.FindProperty(ibProtocol::ibSchema::ibProperties::Parent))
					ibSourceDescriptionMemory::ReadNode(*parent, table);
			}
			return new ibPGDataSourceProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataSource(sourceDesc, property->GetValue().GetString())), kind, table);
		},
		[](const wxVariant& value) {
			ibDataValue node;
			if (const ibVariantDataSource* const dataSource = property_cast(value, ibVariantDataSource))
				ibSourceDescriptionMemory::WriteNode(node, dataSource->GetSourceDesc());
			return node;
		});
	}
}g_sourceLoader;

// (The desktop's builds the Type row here, over the owner's source factory — see the header.)
ibPGDataSourceProperty::ibPGDataSourceProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& strName, const wxVariant& value, int kind, const ibSourceDescription& table) :
	wxPGProperty(label, strName), m_owner(property), m_kind(kind), m_table(table)
{
	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGDataSourceProperty::ValueToString(wxVariant& variant,
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return variant.GetString();
}

bool ibPGDataSourceProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	if (text.IsEmpty()) {
		ibVariantDataSource* dataSource = property_cast(variant, ibVariantDataSource);
		if (dataSource != nullptr) {
			dataSource->SetSource(wxNOT_FOUND);
			return true;
		}
		return false;
	}
	return text.IsEmpty();
}

wxPGEditorDialogAdapter* ibPGDataSourceProperty::GetEditorDialog() const
{
	class ibPGEditorDataSourceDialogAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override
		{
			ibPGDataSourceProperty* const cell = wxDynamicCast(prop, ibPGDataSourceProperty);
			wxVariant value = cell != nullptr ? cell->GetValue() : wxVariant();
			const ibVariantDataSource* const current = cell != nullptr ? property_cast(value, ibVariantDataSource) : nullptr;
			// No form holds the control (no picker set) — said, as before.
			if (current == nullptr || !SourcePicker()) {
				wxMessageBox(_("Choosing a data source is not available in the thin designer yet."),
					_("Choice source"), wxOK | wxICON_INFORMATION, pg);
				return false;
			}
			ibSourceDescription binding = current->GetSourceDesc();
			if (!SourcePicker()(pg, cell->GetOwner(), cell->GetKind(), cell->GetTable(), binding))
				return false;
			// …a binding the same as it was changes nothing.
			if (binding.GetPath() == current->GetSourceDesc().GetPath())
				return false;
			SetValue(wxVariant(new ibVariantDataSource(binding)));
			return true;
		}
	};

	return new ibPGEditorDataSourceDialogAdapter();
}
