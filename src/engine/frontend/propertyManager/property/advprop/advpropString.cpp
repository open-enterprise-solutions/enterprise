#include "advpropString.h"

#include "backend/propertyManager/property/propertyString.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertyStringLoader
{
public:
	ibPropertyStringLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(wxStringProperty, ibPropertyStringBase::ms_propertyString);
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibUStringProperty, ibPropertyStringBase::ms_propertyUString);
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibUEStringProperty, ibPropertyStringBase::ms_propertyUEString);
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(wxTStringProperty, ibPropertyStringBase::ms_propertyTString);
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(wxMStringProperty, ibPropertyStringBase::ms_propertyMString);
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

wxString wxTStringProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return ibBackendLocalization::GetTranslateGetRawLocText(value.GetString());
}

bool wxTStringProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	ibBackendLocalizationEntryArray array;

	if (ibBackendLocalization::CreateLocalizationArray(variant.GetString(), array)) {
		const wxString& strLangCode = ibBackendLocalization::GetUserLanguage();
		const auto iterator = std::find_if(array.begin(), array.end(),
			[strLangCode](const ibBackendLocalizationEntry& entry) {
				return stringUtils::CompareString(entry.m_code, strLangCode); });
		if (iterator == array.end()) {
			ibBackendLocalizationEntry entry;
			entry.m_code = strLangCode;
			entry.m_data = text;
			array.emplace_back(entry);
		}
		else {
			iterator->m_data = text;
		}
		variant = ibBackendLocalization::GetRawLocText(array);
		return true;
	}

	ibBackendLocalizationEntry entry;
	entry.m_code = ibBackendLocalization::GetUserLanguage();
	entry.m_data = text;
	array.emplace_back(entry);

	variant = ibBackendLocalization::GetRawLocText(array);
	return true;
}

#include "backend/metadata.h"
#include "backend/metaCollection/metaLanguageObject.h"

bool wxTStringProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	class wxTranslateTextCtrl : public wxTextCtrl {
	public:
		wxTranslateTextCtrl() : wxTextCtrl() {}
		wxTranslateTextCtrl(wxWindow* parent, wxWindowID id,
			const wxString& value = wxEmptyString,
			const wxPoint& pos = wxDefaultPosition,
			const wxSize& size = wxDefaultSize,
			long style = 0,
			const wxValidator& validator = wxDefaultValidator,
			const wxString& name = wxASCII_STR(wxTextCtrlNameStr))
			:
			wxTextCtrl(parent, id, value, pos, size, style, validator, name)
		{
			InvalidateBestSize();
		}
	protected:
		virtual wxSize DoGetBestSize() const override {
			wxSize best_size = wxTextCtrl::DoGetBestSize();
			if (best_size.y > 38)
				best_size.y = 38;
			return best_size;
		}
	};

	wxASSERT_MSG(value.IsType(wxS("string")), "Function called for incompatible property");

	if (m_ownerProperty != nullptr) {

		std::map<ibValueMetaObjectLanguage*, wxTextCtrl*> locArray;

		// launch editor dialog
		wxDialog* dlg = new wxDialog(pg->GetPanel(), wxID_ANY,
			m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
			wxDefaultPosition, wxDefaultSize, m_dlgStyle);

		dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

		// Multi-line text editor dialog.
		const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
		wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
		long edStyle = wxTE_MULTILINE;
		if (HasFlag(wxPGFlags::ReadOnly))
			edStyle |= wxTE_READONLY;

		ibMetaData* metaData = m_ownerProperty->GetMetaData();
		if (metaData != nullptr) {

			wxBoxSizer* rowsizer = new wxBoxSizer(wxVERTICAL);

			ibBackendLocalizationEntryArray array;
			ibBackendLocalization::CreateLocalizationArray(
				m_value.GetString(), array);

			ibMetaData* owner = nullptr;
			metaData->GetOwner(owner);
			if (owner == nullptr) { owner = metaData; }

			auto arrayLanguage = owner->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID);
			for (const auto language : arrayLanguage) {

				auto iterator = std::find_if(array.begin(), array.end(),
					[language](const ibBackendLocalizationEntry& entry) {
						return stringUtils::CompareString(entry.m_code, language->GetLangCode()); });

				const wxString& strTranslate =
					iterator != array.end() ? wxString(iterator->m_data) : wxString();

				if (arrayLanguage.size() > 1) {

					wxStaticText* ss = new wxStaticText(dlg, wxID_ANY, language->GetSynonym(),
						wxDefaultPosition, wxDefaultSize);

					wxTextCtrl* ed = new wxTranslateTextCtrl(dlg, language->GetMetaID(), strTranslate,
						wxDefaultPosition, wxDefaultSize, edStyle);

					if (m_maxLen > 0)
						ed->SetMaxLength(m_maxLen);

					rowsizer->Add(ss, wxSizerFlags(0).Border(wxLEFT | wxRIGHT, spacing));
					rowsizer->Add(ed, wxSizerFlags(1).Expand().Border(wxALL, spacing));

					locArray.emplace(language, ed);
				}
				else {
					wxTextCtrl* ed = new wxTranslateTextCtrl(dlg, language->GetMetaID(), strTranslate,
						wxDefaultPosition, wxDefaultSize, edStyle);

					if (m_maxLen > 0)
						ed->SetMaxLength(m_maxLen);

					rowsizer->Add(ed, wxSizerFlags(1).Expand().Border(wxALL, spacing));
					locArray.emplace(language, ed);
				}
			}

			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());
		}

		long btnSizerFlags = wxCANCEL;
		if (!HasFlag(wxPGFlags::ReadOnly))
			btnSizerFlags |= wxOK;
		wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(btnSizerFlags);
		topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

		dlg->SetSizer(topsizer);
		topsizer->SetSizeHints(dlg);

		if (!wxPropertyGrid::IsSmallScreen()) {
			dlg->SetSize(400, 300);
			dlg->Move(pg->GetGoodEditorDialogPosition(this, dlg->GetSize()));
		}

		const int res = dlg->ShowModal();

		if (res == wxID_OK) {
			wxString strLocalization;
			for (const auto pair : locArray) {
				const auto ml = pair.first;	const auto ed = pair.second;
				strLocalization += wxString::Format(wxT("%s = '%s';"),
					ml->GetLangCode(),
					ed->GetValue()
				);
			}
			value = strLocalization;

			dlg->Destroy();
			return true;
		}

		dlg->Destroy();
		return false;
	}

	return false;
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
		dlg->SetSize(400, 300);
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
