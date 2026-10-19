#include "advpropCommandSource.h"

#include "frmclient/backend/propertyManager/property/variant/variantCommandSource.h"    // ibVariantDataCommandSource (the cell value)

#include "frmclient/propertyManager/property/private/prop.h"                    // wxPGPropertyFlags_*, property_cast
#include "frmclient/propertyManager/property/private/propertyRegistry.h"        // ibPropertyRegistry::Register
#include "frmclient/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGCommandSourceProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGCommandSourceProperty, wxPGProperty, TextCtrlAndButton)

// register the frontend editor for the server's command-source property — the binding as it stores it
// (ibCommandDescriptionMemory), read as it wrote it (the row's Presentation).
class ibPropertyCommandSourceLoader
{
public:
	ibPropertyCommandSourceLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyCommandSource"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibCommandDescription commandDesc;
			ibCommandDescriptionMemory::ReadNode(property->GetNodeValue(), commandDesc);
			return new ibPGCommandSourceProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataCommandSource(commandDesc, property->GetValue().GetString())));
		});
	}
} g_commandSourceLoader;

ibPGCommandSourceProperty::ibPGCommandSourceProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_propertyObject(property)
{
	m_flags |= wxPGPropertyFlags_ActiveButton;   // the "…" button is always live
	SetValue(value);
}

wxString ibPGCommandSourceProperty::ValueToString(wxVariant& variant, wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	// Just the variant's own string — the binding's name as the server wrote it.
	ibVariantDataCommandSource* data = property_cast(variant, ibVariantDataCommandSource);
	wxString s;
	if (data != nullptr)
		data->Write(s);
	return s;
}

void ibPGCommandSourceProperty::OnSetValue()
{
	// No bound command's picture: the desktop resolves it through the owner's command door
	// (ibFrontendCommandReceiver::ResolveCommand) over the form's commands, which are the server's — text only.
	m_valueBitmapBundle = wxBitmapBundle();
}

bool ibPGCommandSourceProperty::StringToValue(wxVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;   // read-only cell — the value is set only through the dialog
}

wxPGEditorDialogAdapter* ibPGCommandSourceProperty::GetEditorDialog() const
{
	class ibPGEditorCommandSourceDialogAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override
		{
			// ⚠ SAID, NOT DROPPED: the command sources the dialog lists — the form's own commands, its main
			// attribute's, each table's, the general ones, and "New command..." making one with its handler — are
			// gathered over the server's form; the thin designer does not have them yet.
			wxMessageBox(_("Choosing a command is not available in the thin designer yet."),
				_("Choice command"), wxOK | wxICON_INFORMATION, pg);
			return false;
		}
	};
	return new ibPGEditorCommandSourceDialogAdapter();
}
