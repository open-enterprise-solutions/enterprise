#include "advpropString.h"

#include "frmclient/backend/propertyManager/propertyObject.h"                     // ibPropertyObject
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the string's node
#include "frmclient/propertyManager/property/private/prop.h"                 // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The text a value holds, as the server stores it (a String node) — a translated string's every language, raw…
wxString ReadText(const wxVariant& value)
{
	return ibVariantDataSerializable::GetNodeValue(value).AsString();
}

// …and the value of another: the same property's, read as the words given.
wxVariant WriteText(const wxVariant& value, const wxString& text, const wxString& words)
{
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::String(text), words));
}

} // namespace

// register frontend property
class ibPropertyStringLoader
{
public:
	ibPropertyStringLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyString"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyUEString"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibUEStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyUString"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibUStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyFormat"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxFormatStringProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyTString"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxTStringProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyMString"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxMStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_stringLoader;

// -----------------------------------------------------------------------
// ibStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibStringProperty, wxStringProperty, TextCtrl)

ibStringProperty::ibStringProperty(const wxString& label, const wxString& name, const wxVariant& value)
	: wxStringProperty(label, name)
{
	SetValue(value);
}

wxString ibStringProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	wxVariant held(ReadText(value));
	return wxStringProperty::ValueToString(held, flags);
}

bool ibStringProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	wxVariant held(ReadText(variant));
	if (!wxStringProperty::StringToValue(held, text, flags))
		return false;
	variant = WriteText(variant, held.GetString(), held.GetString());
	return true;
}

// -----------------------------------------------------------------------
// ibUStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibUStringProperty, ibStringProperty, TextCtrl)

bool ibUStringProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) >= 0) {
		wxMessageBox(_("You can enter only numbers, letters and the symbol \"_\""), _("Error entering value"));
		return false;
	}

	if (!ibStringProperty::StringToValue(variant, text, flags))
		return false;

	if (m_strFlags == allow_empty)
		return true;

	return text.length() > 0;
}

// -----------------------------------------------------------------------
// ibUEStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibUEStringProperty, ibUStringProperty, TextCtrl)

// -----------------------------------------------------------------------
// wxTStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxTStringProperty, wxLongStringProperty, TextCtrlAndButton)

wxTStringProperty::wxTStringProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const wxVariant& value)
	: wxLongStringProperty(label, name), m_ownerProperty(property)
{
	SetValue(value);
}

namespace {

// The languages a value holds — its text, raw…
ibTranslateString ReadTranslate(const wxVariant& value)
{
	return ibTranslateString(ReadText(value));
}

// …and the value of others: the same property's, read as the language in force.
wxVariant WriteTranslate(const wxVariant& value, const ibTranslateString& translate)
{
	return WriteText(value, translate.GetRawText(), translate.GetString());
}

} // namespace

wxString wxTStringProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return ReadTranslate(value).GetString();
}

bool wxTStringProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	const ibTranslateString held = ReadTranslate(variant);
	if (held.GetString() == text)
		return false;

	// ⚠ A NEW CELL, never an edit in place — the command processor caches these variants and swaps
	// them back, which is what makes undo a snapshot (property-system.md §3.2). Typed in the grid
	// means the language in force, so only that one cell differs from the cached value.
	ibTranslateString edited = held;
	edited.SetTranslate(text);

	variant = WriteTranslate(variant, edited);
	return true;
}

#include "frmclient/win/dlgs/translateConstructor/translateConstructor.h"

bool wxTStringProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	// THE SAME WINDOW the code editor opens on a string literal — see translateConstructor.h for what
	// OK does to the text, and the three rules the window this one replaced broke quietly. Its languages are those of
	// the configuration the property's owner holds (a metaobject: the one it was opened from).
	ibDialogTranslateConstructor dlg(pg->GetPanel(), m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
		ReadTranslate(value), m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr,
		HasFlag(wxPGFlags::ReadOnly), m_maxLen,
		GetBoxEditor());
	if (!wxPropertyGrid::IsSmallScreen())
		dlg.Move(pg->GetGoodEditorDialogPosition(this, dlg.GetSize()));

	if (dlg.ShowModal() != wxID_OK)
		return false;

	// ⚠ A NEW CELL, never an edit in place — see StringToValue.
	value = WriteTranslate(value, dlg.GetTranslate());
	return true;
}

// -----------------------------------------------------------------------
// wxFormatStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxFormatStringProperty, wxTStringProperty, TextCtrlAndButton)

#include "frmclient/win/dlgs/formatConstructor/formatConstructor.h"

ibDialogTranslateConstructor::ibBoxEditor wxFormatStringProperty::GetBoxEditor() const
{
	return ibFormatBoxEditor(HasFlag(wxPGFlags::ReadOnly));
}

// -----------------------------------------------------------------------
// wxMStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxMStringProperty, wxLongStringProperty, TextCtrlAndButton)

wxMStringProperty::wxMStringProperty(const wxString& label, const wxString& name, const wxVariant& value)
	: wxLongStringProperty(label, name)
{
	SetValue(value);
}

wxString wxMStringProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	wxVariant held(ReadText(value));
	return wxLongStringProperty::ValueToString(held, flags);
}

bool wxMStringProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	wxVariant held(ReadText(variant));
	if (!wxLongStringProperty::StringToValue(held, text, flags))
		return false;
	variant = WriteText(variant, held.GetString(), held.GetString());
	return true;
}

bool wxMStringProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	// launch editor dialog
	wxDialog* dlg = new wxDialog(pg->GetPanel(), wxID_ANY,
		m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
		wxDefaultPosition, wxDefaultSize, m_dlgStyle);

	dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

	// Multi-line text editor dialog.
	const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
	wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);
	long edStyle = wxTE_MULTILINE;
	if (HasFlag(wxPGFlags::ReadOnly))
		edStyle |= wxTE_READONLY;
	wxTextCtrl* ed = new wxTextCtrl(dlg, wxID_ANY, ReadText(value),
		wxDefaultPosition, wxDefaultSize, edStyle);
	if (m_maxLen > 0)
		ed->SetMaxLength(m_maxLen);

	rowsizer->Add(ed, wxSizerFlags(1).Expand().Border(wxALL, spacing));
	topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

	long btnSizerFlags = wxCANCEL;
	if (!HasFlag(wxPGFlags::ReadOnly))
		btnSizerFlags |= wxOK;
	wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(btnSizerFlags);
	topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

	dlg->SetSizer(topsizer);
	topsizer->SetSizeHints(dlg);

	if (!wxPropertyGrid::IsSmallScreen())
	{
		dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
		dlg->Move(pg->GetGoodEditorDialogPosition(this, dlg->GetSize()));
	}

	int res = dlg->ShowModal();

	if (res == wxID_OK)
	{
		value = WriteText(value, ed->GetValue(), ed->GetValue());
		dlg->Destroy();
		return true;
	}

	dlg->Destroy();
	return false;
}
