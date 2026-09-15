#include "advpropString.h"

#include "backend/propertyManager/property/propertyString.h"
#include "backend/propertyManager/property/variant/variantTranslate.h"
#include "frontend/propertyManager/property/private/prop.h"                 // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyStringLoader
{
public:
	ibPropertyStringLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyString* prop) -> wxPGProperty* {
			return new wxStringProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsString());
		});
		// ibPropertyUEString DERIVES from ibPropertyUString, so the base must be tried after
		// it — otherwise the UString maker would swallow every UEString.
		ibPropertyRegistry::Register([](ibPropertyUEString* prop) -> wxPGProperty* {
			return new ibUEStringProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsString());
		});
		ibPropertyRegistry::Register([](ibPropertyUString* prop) -> wxPGProperty* {
			return new ibUStringProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsString());
		}, ibPropertyRegistry::Priority_Base);
		ibPropertyRegistry::Register([](ibPropertyTString* prop) -> wxPGProperty* {
			return new wxTStringProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(),
				prop->GetValueAsTranslate());
		});
		ibPropertyRegistry::Register([](ibPropertyMString* prop) -> wxPGProperty* {
			return new wxMStringProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsString());
		});
	}
}g_stringLoader;

// -----------------------------------------------------------------------
// ibUStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibUStringProperty, wxStringProperty, TextCtrl)

wxString ibUStringProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	wxString s = value.GetString();

	if (HasAnyChild() && HasFlag(wxPGFlags::ComposedValue))
	{
		// Value stored in m_value is non-editable, non-full value
		if (!!(flags & wxPGPropValFormatFlags::FullValue) ||
			!!(flags & wxPGPropValFormatFlags::EditableValue) ||
			s.empty())
		{
			// Calling this under incorrect conditions will fail
			wxASSERT_MSG(!!(flags & wxPGPropValFormatFlags::ValueIsCurrent),
				wxS("Sorry, currently default wxPGProperty::ValueToString() ")
				wxS("implementation only works if value is m_value."));

			DoGenerateComposedValue(s, flags);
		}

		return s;
	}

	// If string is password and value is for visual purposes,
	// then return asterisks instead the actual string.
	if (!!(m_flags & wxPGPropertyFlags_Password) && !(flags & (wxPGPropValFormatFlags::FullValue | wxPGPropValFormatFlags::EditableValue)))
		return wxString(wxS('*'), s.length());

	return s;
}

bool ibUStringProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) >= 0) {
		wxMessageBox(_("You can enter only numbers, letters and the symbol \"_\""), _("Error entering value"));
		return false;
	}

	if (GetChildCount() && HasFlag(wxPGFlags::ComposedValue))
		return wxPGProperty::StringToValue(variant, text, flags);

	if (variant != text) {

		variant = text;

		if (m_strFlags == allow_empty)
			return true;

		return text.length() > 0;
	}

	return false;
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
	const wxString& name, const ibTranslateString& value)
	: wxLongStringProperty(label, name), m_ownerProperty(property)
{
	SetValue(
		new ibVariantDataTranslate(value)
	);
}

wxString wxTStringProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	ibVariantDataTranslate* translateVariant = property_cast(value, ibVariantDataTranslate);
	wxASSERT(translateVariant);
	return translateVariant->GetTranslate();
}

bool wxTStringProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	ibVariantDataTranslate* translateVariant = property_cast(variant, ibVariantDataTranslate);
	wxASSERT(translateVariant);

	const ibTranslateString& held = translateVariant->GetTranslate();
	if (held.GetString() == text)
		return false;

	// ⚠ A NEW CELL, never an edit in place — the command processor caches these variants and swaps
	// them back, which is what makes undo a snapshot (property-system.md §3.2). Typed in the grid
	// means the language in force, so only that one cell differs from the cached value.
	ibTranslateString edited = held;
	edited.SetTranslate(text);

	variant = new ibVariantDataTranslate(edited);
	return true;
}

#include "frontend/win/dlgs/translateConstructor/translateConstructor.h"

bool wxTStringProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	ibVariantDataTranslate* translateVariant = property_cast(value, ibVariantDataTranslate);
	wxASSERT_MSG(translateVariant, "Function called for incompatible property");

	if (m_ownerProperty == nullptr)
		return false;

	// THE SAME WINDOW the code editor opens on a string literal — see translateConstructor.h for what
	// OK does to the text, and the three rules the window this one replaced broke quietly.
	ibDialogTranslateConstructor dlg(pg->GetPanel(), m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
		translateVariant->GetTranslate(), m_ownerProperty->GetMetaData(), HasFlag(wxPGFlags::ReadOnly), m_maxLen);
	if (!wxPropertyGrid::IsSmallScreen())
		dlg.Move(pg->GetGoodEditorDialogPosition(this, dlg.GetSize()));

	if (dlg.ShowModal() != wxID_OK)
		return false;

	// ⚠ A NEW CELL, never an edit in place — see StringToValue.
	value = new ibVariantDataTranslate(dlg.GetTranslate());
	return true;
}

// -----------------------------------------------------------------------
// wxMStringProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxMStringProperty, wxLongStringProperty, TextCtrlAndButton)

bool wxMStringProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	wxASSERT_MSG(value.IsType(wxS("string")), "Function called for incompatible property");

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
	wxTextCtrl* ed = new wxTextCtrl(dlg, wxID_ANY, value.GetString(),
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
		value = ed->GetValue();
		dlg->Destroy();
		return true;
	}

	dlg->Destroy();
	return false;
}
