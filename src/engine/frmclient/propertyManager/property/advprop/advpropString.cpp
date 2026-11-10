#include "advpropString.h"

#include "frmclient/backend/propertyManager/propertyObject.h"                     // ibPropertyObject
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the string's node
#include "frmclient/propertyManager/property/private/prop.h"                 // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "deskgui/univ/ctrl/textCtrl.h"
#include "deskgui/univ/dialog.h"
#include "deskgui/univ/messageDialog.h"

namespace {

// The text a value holds, as the server stores it (a String node) — a translated string's every language, raw…
wxString ReadText(const ibVariant& value)
{
	return ibVariantSerializable::GetNodeValue(value).AsString();
}

// …and the value of another: the same property's, read as the words given.
ibVariant WriteText(const wxString& text, const wxString& words)
{
	return ibVariant(new ibVariantSerializable(ibDataValue::String(text), words));
}

} // namespace

// register frontend property
class ibPropertyStringLoader
{
public:
	ibPropertyStringLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyString"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyUEString"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibUEStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyUString"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibUStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyFormat"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibFormatStringProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyTString"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibTStringProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyMString"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibMStringProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_stringLoader;

// -----------------------------------------------------------------------
// ibPGStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGStringProperty, ibStringProperty, TextCtrl)

ibPGStringProperty::ibPGStringProperty(const wxString& label, const wxString& name, const ibVariant& value)
	: ibStringProperty(label, name)
{
	SetValue(value);
}

wxString ibPGStringProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	ibVariant held(ReadText(value));
	return ibStringProperty::ValueToString(held, flags);
}

bool ibPGStringProperty::StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const
{
	ibVariant held(ReadText(variant));
	if (!ibStringProperty::StringToValue(held, text, flags))
		return false;
	variant = WriteText(held.GetString(), held.GetString());
	return true;
}

// -----------------------------------------------------------------------
// ibUStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibUStringProperty, ibPGStringProperty, TextCtrl)

bool ibUStringProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) >= 0) {
		ibMessageBox(_("You can enter only numbers, letters and the symbol \"_\""), _("Error entering value"));
		return false;
	}

	if (!ibPGStringProperty::StringToValue(variant, text, flags))
		return false;

	if (m_strFlags == allow_empty)
		return true;

	return text.length() > 0;
}

// -----------------------------------------------------------------------
// ibUEStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibUEStringProperty, ibUStringProperty, TextCtrl)

// -----------------------------------------------------------------------
// ibTStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibTStringProperty, ibLongStringProperty, TextCtrlAndButton)

ibTStringProperty::ibTStringProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const ibVariant& value)
	: ibLongStringProperty(label, name), m_ownerProperty(property)
{
	SetValue(value);
}

namespace {

// The languages a value holds — its text, raw…
ibTranslateString ReadTranslate(const ibVariant& value)
{
	return ibTranslateString(ibString(ReadText(value)));
}

// …and the value of others: the same property's, read as the language in force.
ibVariant WriteTranslate(const ibVariant& value, const ibTranslateString& translate)
{
	return WriteText(translate.GetRawText(), translate.GetString());
}

} // namespace

wxString ibTStringProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	return ReadTranslate(value).GetString();
}

bool ibTStringProperty::StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const
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

bool ibTStringProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
	// THE SAME WINDOW the code editor opens on a string literal — see translateConstructor.h for what
	// OK does to the text, and the three rules the window this one replaced broke quietly. Its languages are those of
	// the configuration the property's owner holds (a metaobject: the one it was opened from).
	ibDialogTranslateConstructor dlg(pg->GetPanel(), m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
		ReadTranslate(value), m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr,
		HasFlag(ibPGFlags::ReadOnly), m_maxLen,
		GetBoxEditor());
	if (!ibPropertyGrid::IsSmallScreen())
		dlg.Move(pg->GetGoodEditorDialogPosition(this, dlg.GetSize()));

	if (dlg.ShowModal() != wxID_OK)
		return false;

	// ⚠ A NEW CELL, never an edit in place — see StringToValue.
	value = WriteTranslate(value, dlg.GetTranslate());
	return true;
}

// -----------------------------------------------------------------------
// ibFormatStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibFormatStringProperty, ibTStringProperty, TextCtrlAndButton)

#include "frmclient/win/dlgs/formatConstructor/formatConstructor.h"

ibDialogTranslateConstructor::ibBoxEditor ibFormatStringProperty::GetBoxEditor() const
{
	return ibFormatBoxEditor(HasFlag(ibPGFlags::ReadOnly));
}

// -----------------------------------------------------------------------
// ibMStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibMStringProperty, ibLongStringProperty, TextCtrlAndButton)

ibMStringProperty::ibMStringProperty(const wxString& label, const wxString& name, const ibVariant& value)
	: ibLongStringProperty(label, name)
{
	SetValue(value);
}

wxString ibMStringProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	ibVariant held(ReadText(value));
	return ibLongStringProperty::ValueToString(held, flags);
}

bool ibMStringProperty::StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const
{
	ibVariant held(ReadText(variant));
	if (!ibLongStringProperty::StringToValue(held, text, flags))
		return false;
	variant = WriteText(held.GetString(), held.GetString());
	return true;
}

bool ibMStringProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
	// launch editor dialog
	ibDialog* dlg = new ibDialog(pg->GetPanel(), wxID_ANY,
		m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
		wxDefaultPosition, wxDefaultSize, m_dlgStyle);

	dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

	// Multi-line text editor dialog.
	const int spacing = ibPropertyGrid::IsSmallScreen() ? 4 : 8;
	wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);
	long edStyle = wxTE_MULTILINE;
	if (HasFlag(ibPGFlags::ReadOnly))
		edStyle |= wxTE_READONLY;
	ibTextCtrl* ed = new ibTextCtrl(dlg, wxID_ANY, ReadText(value),
		wxDefaultPosition, wxDefaultSize, edStyle);
	if (m_maxLen > 0)
		ed->SetMaxLength(m_maxLen);

	rowsizer->Add(ed, wxSizerFlags(1).Expand().Border(wxALL, spacing));
	topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

	long btnSizerFlags = wxCANCEL;
	if (!HasFlag(ibPGFlags::ReadOnly))
		btnSizerFlags |= wxOK;
	ibStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(btnSizerFlags);
	topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

	dlg->SetSizer(topsizer);
	topsizer->SetSizeHints(dlg);

	if (!ibPropertyGrid::IsSmallScreen())
	{
		dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
		dlg->Move(pg->GetGoodEditorDialogPosition(this, dlg->GetSize()));
	}

	int res = dlg->ShowModal();

	if (res == wxID_OK)
	{
		value = WriteText(ed->GetValue(), ed->GetValue());
		dlg->Destroy();
		return true;
	}

	dlg->Destroy();
	return false;
}
