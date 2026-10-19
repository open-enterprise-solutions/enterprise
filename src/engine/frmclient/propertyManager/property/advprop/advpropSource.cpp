#include "advpropSource.h"

#include "frmclient/backend/propertyManager/property/variant/variantSource.h"   // ibVariantDataSource

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGDataSourceProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataSourceProperty, wxPGProperty, TextCtrlAndButton)

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
			return new ibPGDataSourceProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataSource(sourceDesc, property->GetValue().GetString())));
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
ibPGDataSourceProperty::ibPGDataSourceProperty(const ibPropertyObject* WXUNUSED(property), const wxString& label,
	const wxString& strName, const wxVariant& value) : wxPGProperty(label, strName)
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
			// ⚠ SAID, NOT DROPPED: the attributes and columns to bind to are walked over the server's form; the thin
			// designer does not have them yet.
			wxMessageBox(_("Choosing a data source is not available in the thin designer yet."),
				_("Choice source"), wxOK | wxICON_INFORMATION, pg);
			return false;
		}
	};

	return new ibPGEditorDataSourceDialogAdapter();
}
