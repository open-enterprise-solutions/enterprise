#ifndef __CHOICE_TEMPLATE_H__
#define __CHOICE_TEMPLATE_H__

#include <wx/listctrl.h>
#include <wx/statline.h>

struct ibChoiceTemplateItem
{
	ibDocTemplate* m_template;
	wxString m_description;
	wxIcon m_icon;
};

class ibDialogChoiceTemplate : public wxDialog {

	struct ibDialogChoiceTemplateItem {

		long m_index;

		ibDocTemplate* m_template;
		wxString m_description;
		wxIcon m_icon;
	};

public:

	ibDocTemplate* GetSelectionData() const
	{
		const long selected = m_listChoice->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (selected != -1) {

			auto iterator = std::find_if(m_choiceData.begin(), m_choiceData.end(),
				[selected](const ibDialogChoiceTemplateItem& i) { return i.m_index == selected; });

			if (iterator != m_choiceData.end())
				return iterator->m_template;
		}

		return nullptr;
	}

	ibDialogChoiceTemplate(const wxVector<ibChoiceTemplateItem>& choices) :
		wxDialog(NULL, wxID_ANY, _("Select document type"), wxDefaultPosition, wxSize(250, 225), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

		m_staticTextTemplate = new wxStaticText(this, wxID_ANY, _("Templates:"));
		m_staticTextTemplate->Wrap(-1);
		mainSizer->Add(m_staticTextTemplate, 0, wxALL, 5);

		m_listChoice = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_NO_HEADER | wxLC_REPORT | wxLC_SINGLE_SEL);
		m_listChoice->AppendColumn(wxT("template"), wxLIST_FORMAT_LEFT, 250);
		m_listChoice->SetImageList(m_imageList, wxIMAGE_LIST_SMALL);

		m_listChoice->Bind(wxEVT_COMMAND_LIST_ITEM_ACTIVATED, &ibDialogChoiceTemplate::OnitemActivated, this);
		m_listChoice->Bind(wxEVT_SIZE, &ibDialogChoiceTemplate::OnSize, this);

		for (const auto& choice : choices)
			AppendTemplate(choice.m_template, choice.m_description, choice.m_icon);

		mainSizer->Add(m_listChoice, 1, wxALL | wxEXPAND, 5);

		m_staticline = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
		mainSizer->Add(m_staticline, 0, wxEXPAND | wxALL, 5);

		m_sdbSizer = new wxStdDialogButtonSizer();
		m_sdbSizerOK = new wxButton(this, wxID_OK);
		m_sdbSizer->AddButton(m_sdbSizerOK);
		m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
		m_sdbSizer->AddButton(m_sdbSizerCancel);
		m_sdbSizer->Realize();

		mainSizer->Add(m_sdbSizer, 0, wxEXPAND, 5);

		wxDialog::SetSizer(mainSizer);
		wxDialog::Layout();
		wxDialog::Centre(wxBOTH);

		m_listChoice->SetFocus();
	}

	virtual ~ibDialogChoiceTemplate() { wxDELETE(m_imageList); }

private:

	void OnitemActivated(wxListEvent& event) {
		wxDialog::EndModal(wxID_OK);
		event.Skip();
	}

	void OnSize(wxSizeEvent& event) {
		m_listChoice->SetColumnWidth(0, event.m_size.x - 4);
		event.Skip();
	}

	void AppendTemplate(ibDocTemplate* docTemplate, const wxString& description, const wxIcon& icon) {

		const int index = m_listChoice->InsertItem(m_listChoice->GetItemCount(), description, m_imageList->Add(icon));

		ibDialogChoiceTemplateItem entry;
		entry.m_index = index;
		entry.m_template = docTemplate;
		entry.m_description = description;
		entry.m_icon = icon;
		m_choiceData.push_back(entry);

		if (m_choiceData.size() == 1)
			m_listChoice->SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}

	wxImageList* m_imageList = new wxImageList(16, 16);

	wxStaticText* m_staticTextTemplate;
	wxListCtrl* m_listChoice;
	wxStaticLine* m_staticline;
	wxStdDialogButtonSizer* m_sdbSizer;
	wxButton* m_sdbSizerOK;
	wxButton* m_sdbSizerCancel;

	wxVector<ibDialogChoiceTemplateItem> m_choiceData;
};

#endif 
