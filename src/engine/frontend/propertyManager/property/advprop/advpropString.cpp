#include "advpropString.h"

#include "backend/propertyManager/property/propertyString.h"
#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/aiPropertyHelper.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include <wx/button.h>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/timer.h>

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

		// Workmate-parity "AI" button — visible for the synonym / comment /
		// tooltip / title family. Clicking it dispatches the helper which
		// calls the active AI provider's CompleteCodeAsync. While the
		// request is in flight the button shows "…" and is disabled. The
		// dialog stays open so the user can inspect the proposed text
		// before pressing OK.
		ibProperty* aiBackingProperty = m_ownerProperty != nullptr
		    ? m_ownerProperty->GetProperty(GetBaseName())
		    : nullptr;
		if (!HasFlag(wxPGFlags::ReadOnly) && aiBackingProperty != nullptr &&
		    ibAIPropertyHelper::IsAIEligible(GetBaseName())) {
			wxButton* aiButton = new wxButton(dlg, wxID_ANY, wxT("AI"));
			aiButton->SetToolTip(_("Сгенерировать через AI"));
			buttonSizer->Insert(0, aiButton, wxSizerFlags(0).Border(wxRIGHT, spacing));

			// Stash a self-owning poll timer on the button so its label /
			// enabled state and the visible language editors stay in
			// sync as the async LLM call completes. wxTimer fires on the
			// UI thread; once IsBusy flips to false we refresh and stop.
			auto* pollTimer = new wxTimer();
			pollTimer->SetOwner(aiButton);
			ibProperty* propCapture = aiBackingProperty;
			auto* locArrayPtr = &locArray;
			const wxString priorLabel = aiButton->GetLabel();

			aiButton->Bind(wxEVT_TIMER,
			    [aiButton, propCapture, locArrayPtr, priorLabel, pollTimer]
			    (wxTimerEvent&) {
				if (ibAIPropertyHelper::IsBusy()) return; // still waiting
				pollTimer->Stop();
				aiButton->Enable(true);
				aiButton->SetLabel(priorLabel);
				if (auto* sp = dynamic_cast<ibPropertyStringBase*>(propCapture)) {
					ibBackendLocalizationEntryArray arr;
					ibBackendLocalization::CreateLocalizationArray(sp->GetValueAsString(), arr);
					for (auto& pair : *locArrayPtr) {
						const wxString lang = pair.first->GetLangCode();
						auto found = std::find_if(arr.begin(), arr.end(),
						    [&](const ibBackendLocalizationEntry& e) {
							return stringUtils::CompareString(e.m_code, lang);
						});
						if (found != arr.end()) pair.second->SetValue(found->m_data);
					}
				}
			});

			aiButton->Bind(wxEVT_BUTTON,
			    [aiButton, propCapture, pollTimer]
			    (wxCommandEvent&) {
				// Spec: clicking again while pending is a no-op.
				if (ibAIPropertyHelper::IsBusy() || !aiButton->IsEnabled()) return;
				aiButton->Enable(false);
				aiButton->SetLabel(wxT("…"));
				ibAIPropertyHelper::RunGenerate(
				    aiButton, /*pgManager=*/nullptr, /*pgProperty=*/nullptr,
				    propCapture, propCapture->GetPropertyObject());
				// 100 ms keeps the dialog responsive without burning
				// CPU. The provider round-trip is dominated by the
				// model call, not the polling interval.
				pollTimer->Start(100);
			});

			aiButton->Bind(wxEVT_DESTROY,
			    [pollTimer](wxWindowDestroyEvent& evt) {
				pollTimer->Stop();
				delete pollTimer;
				evt.Skip();
			});
		}

		topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

		dlg->SetSizer(topsizer);
		topsizer->SetSizeHints(dlg);

		if (!wxPropertyGrid::IsSmallScreen()) {
			dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
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

	// AI button — wxMStringProperty backs the "Comment" cell (and other
	// long-string fields). When the wxPGProperty's name matches one of
	// the AI-eligible identifiers, surface the same generator flow we
	// expose on wxTStringProperty. The owning ibPropertyObject is
	// reachable via the propGrid's current property selection: the
	// grid stores a wxClientData-style backpointer on each wxPGProperty
	// through the ibObjectInspector m_propMap. We fish out the
	// inspector to dereference it.
	ibProperty* aiBackingProperty = nullptr;
	ibPropertyObject* aiOwner = nullptr;
	if (ibObjectInspector* inspector = ibObjectInspector::GetObjectInspector()) {
		aiOwner = inspector->GetSelectedObject();
		if (aiOwner != nullptr)
			aiBackingProperty = aiOwner->GetProperty(GetBaseName());
	}
	if (!HasFlag(wxPGFlags::ReadOnly) && aiBackingProperty != nullptr &&
	    ibAIPropertyHelper::IsAIEligible(GetBaseName())) {
		wxButton* aiButton = new wxButton(dlg, wxID_ANY, wxT("AI"));
		aiButton->SetToolTip(_("Сгенерировать через AI"));
		buttonSizer->Insert(0, aiButton, wxSizerFlags(0).Border(wxRIGHT, spacing));

		auto* pollTimer = new wxTimer();
		pollTimer->SetOwner(aiButton);
		ibProperty* propCapture = aiBackingProperty;
		ibPropertyObject* ownerCapture = aiOwner;
		const wxString priorLabel = aiButton->GetLabel();

		aiButton->Bind(wxEVT_TIMER,
		    [aiButton, propCapture, ed, priorLabel, pollTimer]
		    (wxTimerEvent&) {
			if (ibAIPropertyHelper::IsBusy()) return;
			pollTimer->Stop();
			aiButton->Enable(true);
			aiButton->SetLabel(priorLabel);
			if (auto* sp = dynamic_cast<ibPropertyStringBase*>(propCapture)) {
				ed->SetValue(sp->GetValueAsString());
			}
		});

		aiButton->Bind(wxEVT_BUTTON,
		    [aiButton, propCapture, ownerCapture, pollTimer]
		    (wxCommandEvent&) {
			if (ibAIPropertyHelper::IsBusy() || !aiButton->IsEnabled()) return;
			aiButton->Enable(false);
			aiButton->SetLabel(wxT("…"));
			ibAIPropertyHelper::RunGenerate(
			    aiButton, /*pgManager=*/nullptr, /*pgProperty=*/nullptr,
			    propCapture, ownerCapture);
			pollTimer->Start(100);
		});

		aiButton->Bind(wxEVT_DESTROY,
		    [pollTimer](wxWindowDestroyEvent& evt) {
			pollTimer->Stop();
			delete pollTimer;
			evt.Skip();
		});
	}

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
