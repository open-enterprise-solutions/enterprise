/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardPreviewPage — Page 2 implementation. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardPreview.h"

#include <wx/sizer.h>
#include <wx/notebook.h>
#include <wx/treectrl.h>
#include <wx/listctrl.h>
#include <wx/listbox.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/log.h>
#include <wx/translation.h>

#include <unordered_map>
#include <string>
#include <vector>

#include "3rdparty/nlohmann/json.hpp"
#include "templateWizardPayload.h"

ibTemplateWizardPreviewPage::ibTemplateWizardPreviewPage(wxWindow* parent,
                                                          BackCallback     onBack,
                                                          CustomizeCallback onCustomize,
                                                          ApplyCallback    onApply)
	: wxPanel(parent, wxID_ANY)
{
	auto* vbox = new wxBoxSizer(wxVERTICAL);

	// Header — "<TemplateName> v<Version>"
	m_header = new wxStaticText(this, wxID_ANY, _("Шаблон"));
	{
		wxFont f = m_header->GetFont();
		f.MakeBold();
		f.SetPointSize(f.GetPointSize() + 2);
		m_header->SetFont(f);
	}
	vbox->Add(m_header, 0, wxALL, 10);

	// Notebook with three tabs.
	m_notebook = new wxNotebook(this, wxID_ANY);

	m_structureTree = new wxTreeCtrl(m_notebook, wxID_ANY,
	                                   wxDefaultPosition, wxDefaultSize,
	                                   wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT);
	m_notebook->AddPage(m_structureTree, _("Структура"));

	m_demoList = new wxListCtrl(m_notebook, wxID_ANY,
	                              wxDefaultPosition, wxDefaultSize,
	                              wxLC_REPORT | wxLC_SINGLE_SEL);
	m_demoList->AppendColumn(_("Объект"),  wxLIST_FORMAT_LEFT, 240);
	m_demoList->AppendColumn(_("Строк"),  wxLIST_FORMAT_LEFT, 100);
	m_demoList->AppendColumn(_("Образец"),wxLIST_FORMAT_LEFT, 320);
	m_notebook->AddPage(m_demoList, _("Demo data"));

	m_modulesList = new wxListBox(m_notebook, wxID_ANY);
	m_notebook->AddPage(m_modulesList, _("Модули"));

	vbox->Add(m_notebook, 1, wxALL | wxEXPAND, 10);

	// Footer — checkbox + buttons.
	auto* footer = new wxBoxSizer(wxHORIZONTAL);
	m_includeDataCheckBox = new wxCheckBox(this, wxID_ANY,
	                                         _("Включить демо-данные"));
	m_includeDataCheckBox->SetValue(true);
	footer->Add(m_includeDataCheckBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
	footer->AddStretchSpacer(1);

	auto* btnBack = new wxButton(this, wxID_ANY, _("Назад"));
	auto* btnCustomize = new wxButton(this, wxID_ANY, _("Настроить"));
	auto* btnApply = new wxButton(this, wxID_ANY, _("Применить как есть"));
	footer->Add(btnBack, 0, wxALL, 5);
	footer->Add(btnCustomize, 0, wxALL, 5);
	footer->Add(btnApply, 0, wxALL, 5);

	vbox->Add(footer, 0, wxEXPAND | wxBOTTOM, 8);

	SetSizer(vbox);

	btnBack->Bind(wxEVT_BUTTON, [onBack](wxCommandEvent&) { if (onBack) onBack(); });
	btnCustomize->Bind(wxEVT_BUTTON, [onCustomize](wxCommandEvent&) {
		if (onCustomize) onCustomize();
	});
	btnApply->Bind(wxEVT_BUTTON, [this, onApply](wxCommandEvent&) {
		if (onApply) onApply(IncludeDataChecked());
	});
}

bool ibTemplateWizardPreviewPage::IncludeDataChecked() const
{
	return m_includeDataCheckBox != nullptr && m_includeDataCheckBox->GetValue();
}

void ibTemplateWizardPreviewPage::LoadFrom(const wxString& responseJson,
                                             const wxString& templateName,
                                             const wxString& version)
{
	m_header->SetLabel(wxString::Format(wxT("%s   v%s"),
	                                       templateName,
	                                       version.IsEmpty()
	                                           ? wxString(wxT("?"))
	                                           : version));

	m_structureTree->DeleteAllItems();
	wxTreeItemId root = m_structureTree->AddRoot(wxT(""));

	m_demoList->DeleteAllItems();
	m_modulesList->Clear();

	// Parse the JSON response — tolerate both "structure" and "mutations"
	// keys at the top level because provider contracts may evolve.
	auto parsed = nlohmann::json::parse(std::string(responseJson.utf8_str()),
	                                       nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		m_structureTree->AppendItem(root, _("(Сервер вернул нераспознанный ответ)"));
		return;
	}

	// Unwrap result.* if present (MCP-style providers often wrap payload).
	const nlohmann::json* payload = &parsed;
	if (parsed.contains("result") && parsed["result"].is_object()) {
		payload = &parsed["result"];
	} else if (parsed.contains("structuredContent") &&
	           parsed["structuredContent"].is_object()) {
		payload = &parsed["structuredContent"];
	}

	const nlohmann::json* mutations =
	    ibTemplateWizardPayload::PickMutations(*payload);

	if (mutations == nullptr || mutations->empty()) {
		m_structureTree->AppendItem(root, _("(Шаблон не содержит объектов)"));
	} else {
		// Group by kind for nicer reading.
		std::unordered_map<std::string, wxTreeItemId> kindBuckets;
		for (const auto& m : *mutations) {
			if (!m.is_object()) continue;
			std::string fullName =
			    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path");
			std::string kind =
			    ibTemplateWizardPayload::StringField(m, "kind", "type");
			if (kind.empty()) {
				kind = ibTemplateWizardPayload::InferKindFromFullName(fullName);
			}
			if (kind.empty() && fullName.empty()) continue;
			if (kind.empty()) kind = "Object";
			auto it = kindBuckets.find(kind);
			if (it == kindBuckets.end()) {
				wxTreeItemId bucket = m_structureTree->AppendItem(
				    root, wxString::FromUTF8(kind.c_str()));
				it = kindBuckets.emplace(kind, bucket).first;
			}
			if (fullName.empty()) fullName = kind;
			m_structureTree->AppendItem(it->second,
			                              wxString::FromUTF8(fullName.c_str()));
		}
		m_structureTree->ExpandAll();
	}

	// demoData[] — populate the second tab. Provider shape per spec:
	// [{kind, fullName, rows:[{...},{...}], postAfterInsert?}]
	if (payload->contains("demoData") && (*payload)["demoData"].is_array()) {
		long row = 0;
		for (const auto& di : (*payload)["demoData"]) {
			if (!di.is_object()) continue;
			const std::string fullName = di.value("fullName", std::string());
			int rowCount = 0;
			std::string sample;
			if (di.contains("rows") && di["rows"].is_array()) {
				rowCount = static_cast<int>(di["rows"].size());
				if (!di["rows"].empty() && di["rows"][0].is_object()) {
					// Build a one-line sample: "key1=val1, key2=val2"
					int n = 0;
					for (auto& [k, v] : di["rows"][0].items()) {
						if (n++ > 0) sample += ", ";
						sample += k + "=";
						sample += v.is_string() ? v.get<std::string>() : v.dump();
						if (n >= 3) { sample += " …"; break; }
					}
				}
			}
			const long inserted = m_demoList->InsertItem(row,
			                                                wxString::FromUTF8(fullName.c_str()));
			m_demoList->SetItem(inserted, 1,
			                     wxString::Format(wxT("%d"), rowCount));
			m_demoList->SetItem(inserted, 2, wxString::FromUTF8(sample.c_str()));
			++row;
		}
	}

	// previewModules[] — tolerate both shapes (flat string[] OR
	// {name, source} object[]).
	if (payload->contains("previewModules") &&
	    (*payload)["previewModules"].is_array()) {
		for (const auto& pm : (*payload)["previewModules"]) {
			wxString label;
			if (pm.is_string()) {
				label = wxString::FromUTF8(pm.get<std::string>().c_str());
			} else if (pm.is_object()) {
				const std::string name = pm.value("name", std::string());
				label = wxString::FromUTF8(name.c_str());
			}
			if (!label.empty()) m_modulesList->Append(label);
		}
	}
}
