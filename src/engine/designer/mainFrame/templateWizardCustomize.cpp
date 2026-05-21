/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardCustomizePage — Page 3 implementation. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardCustomize.h"

#include <wx/sizer.h>
#include <wx/radiobut.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/log.h>
#include <wx/translation.h>

#include "3rdparty/nlohmann/json.hpp"

ibTemplateWizardCustomizePage::ibTemplateWizardCustomizePage(wxWindow* parent,
                                                                BackCallback  onBack,
                                                                ApplyCallback onApply)
	: wxPanel(parent, wxID_ANY)
{
	auto* vbox = new wxBoxSizer(wxVERTICAL);

	auto* header = new wxStaticText(this, wxID_ANY, _("Настройка шаблона"));
	{
		wxFont f = header->GetFont();
		f.MakeBold();
		f.SetPointSize(f.GetPointSize() + 2);
		header->SetFont(f);
	}
	vbox->Add(header, 0, wxALL, 10);

	// Radio cluster — three modes.
	m_radioNone = new wxRadioButton(this, wxID_ANY,
	                                  _("Без изменений (применить как есть)"),
	                                  wxDefaultPosition, wxDefaultSize,
	                                  wxRB_GROUP);
	m_radioManual = new wxRadioButton(this, wxID_ANY,
	                                    _("Ручная настройка"));
	m_radioAi = new wxRadioButton(this, wxID_ANY,
	                                _("Tweak with AI"));

	vbox->Add(m_radioNone, 0, wxLEFT | wxRIGHT | wxTOP, 8);
	vbox->Add(m_radioManual, 0, wxLEFT | wxRIGHT | wxTOP, 4);
	vbox->Add(m_radioAi, 0, wxLEFT | wxRIGHT | wxTOP, 4);

	m_radioNone->SetValue(true);

	// Manual mode — list with three columns.
	m_renameList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
	                                wxSize(-1, 220),
	                                wxLC_REPORT | wxLC_SINGLE_SEL |
	                                wxLC_EDIT_LABELS);
	m_renameList->AppendColumn(_("Объект"),      wxLIST_FORMAT_LEFT, 280);
	m_renameList->AppendColumn(_("Новое имя"),  wxLIST_FORMAT_LEFT, 240);
	m_renameList->AppendColumn(_("Исключить"),   wxLIST_FORMAT_CENTER, 100);
	m_renameList->Enable(false);  // gated by radio choice
	vbox->Add(m_renameList, 0, wxALL | wxEXPAND, 10);

	auto* manualHint = new wxStaticText(this, wxID_ANY,
	    _("Двойной клик в колонке «Новое имя» — изменить. "
	      "В колонке «Исключить» — поставить 'X' для пропуска объекта."));
	manualHint->SetForegroundColour(wxColour(96, 110, 132));
	vbox->Add(manualHint, 0, wxLEFT | wxRIGHT, 10);

	// AI mode — large prompt textarea.
	auto* aiHint = new wxStaticText(this, wxID_ANY,
	    _("Что изменить в шаблоне? (например: «переименуй Контрагенты "
	      "в Клиенты и убери РКО»)"));
	aiHint->SetForegroundColour(wxColour(96, 110, 132));
	vbox->Add(aiHint, 0, wxLEFT | wxRIGHT | wxTOP, 10);

	m_aiPromptCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
	                                  wxDefaultPosition, wxSize(-1, 140),
	                                  wxTE_MULTILINE);
	m_aiPromptCtrl->Enable(false);
	vbox->Add(m_aiPromptCtrl, 0, wxALL | wxEXPAND, 10);

	// Footer.
	auto* footer = new wxBoxSizer(wxHORIZONTAL);
	footer->AddStretchSpacer(1);
	auto* btnBack = new wxButton(this, wxID_ANY, _("Назад"));
	auto* btnApply = new wxButton(this, wxID_ANY, _("Применить"));
	footer->Add(btnBack, 0, wxALL, 5);
	footer->Add(btnApply, 0, wxALL, 5);
	vbox->Add(footer, 0, wxEXPAND | wxBOTTOM, 8);

	SetSizer(vbox);

	m_radioNone->Bind(wxEVT_RADIOBUTTON,
	                    &ibTemplateWizardCustomizePage::OnModeChanged, this);
	m_radioManual->Bind(wxEVT_RADIOBUTTON,
	                      &ibTemplateWizardCustomizePage::OnModeChanged, this);
	m_radioAi->Bind(wxEVT_RADIOBUTTON,
	                  &ibTemplateWizardCustomizePage::OnModeChanged, this);

	btnBack->Bind(wxEVT_BUTTON,
	                [onBack](wxCommandEvent&) { if (onBack) onBack(); });

	btnApply->Bind(wxEVT_BUTTON, [this, onApply](wxCommandEvent&) {
		if (onApply) onApply(BuildPayload());
	});
}

void ibTemplateWizardCustomizePage::OnModeChanged(wxCommandEvent&)
{
	if (m_renameList   != nullptr) m_renameList->Enable(m_radioManual->GetValue());
	if (m_aiPromptCtrl != nullptr) m_aiPromptCtrl->Enable(m_radioAi->GetValue());
}

void ibTemplateWizardCustomizePage::LoadObjectsFrom(
    const wxString& templateGetResponseJson)
{
	m_renameList->DeleteAllItems();
	m_objectFullNames.clear();

	auto parsed = nlohmann::json::parse(
	    std::string(templateGetResponseJson.utf8_str()),
	    nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) return;

	const nlohmann::json* payload = &parsed;
	if (parsed.contains("result") && parsed["result"].is_object()) {
		payload = &parsed["result"];
	} else if (parsed.contains("structuredContent") &&
	           parsed["structuredContent"].is_object()) {
		payload = &parsed["structuredContent"];
	}

	const nlohmann::json* mutations = nullptr;
	if (payload->contains("structure") && (*payload)["structure"].is_array()) {
		mutations = &(*payload)["structure"];
	} else if (payload->contains("mutations") && (*payload)["mutations"].is_array()) {
		mutations = &(*payload)["mutations"];
	}
	if (mutations == nullptr) return;

	long row = 0;
	for (const auto& m : *mutations) {
		if (!m.is_object()) continue;
		const std::string fullName = m.value("fullName", std::string());
		if (fullName.empty()) continue;
		const wxString wxFullName = wxString::FromUTF8(fullName.c_str());
		m_objectFullNames.push_back(wxFullName);
		const long inserted = m_renameList->InsertItem(row, wxFullName);
		m_renameList->SetItem(inserted, 1, wxFullName);   // default = unchanged
		m_renameList->SetItem(inserted, 2, wxT(""));      // empty = not excluded
		++row;
	}
}

ibTemplateWizardCustomizePage::Payload
ibTemplateWizardCustomizePage::BuildPayload() const
{
	Payload p;
	if (m_radioManual != nullptr && m_radioManual->GetValue()) {
		p.mode = Mode::Manual;
		nlohmann::json mods = nlohmann::json::object();
		mods["renames"]  = nlohmann::json::object();
		mods["excludes"] = nlohmann::json::array();
		const long n = m_renameList->GetItemCount();
		for (long i = 0; i < n; ++i) {
			const wxString orig = m_renameList->GetItemText(i, 0);
			const wxString rename = m_renameList->GetItemText(i, 1);
			const wxString exclude = m_renameList->GetItemText(i, 2);
			if (!exclude.IsEmpty()) {
				mods["excludes"].push_back(std::string(orig.utf8_str()));
				continue;
			}
			if (rename != orig && !rename.IsEmpty()) {
				mods["renames"][std::string(orig.utf8_str())]
				    = std::string(rename.utf8_str());
			}
		}
		p.modificationsJson = wxString::FromUTF8(mods.dump().c_str());
	} else if (m_radioAi != nullptr && m_radioAi->GetValue()) {
		p.mode = Mode::AiTweak;
		p.userPrompt = m_aiPromptCtrl->GetValue();
	} else {
		p.mode = Mode::None;
	}
	return p;
}
