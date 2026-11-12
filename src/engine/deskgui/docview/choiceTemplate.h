#ifndef _DESKGUI_CHOICE_TEMPLATE_H__
#define _DESKGUI_CHOICE_TEMPLATE_H__

// THE CHOICE OF A DOCUMENT'S TEMPLATE — File → New, the old desktop's:the templates
// with their pictures, one picked. On the thin client the templates are the server's (the frame's Templates).

#include <algorithm>

#include "deskgui/univ/ctrl/button.h"
#include <wx/dialog.h>
#include <wx/imaglist.h>
#include "deskgui/univ/ctrl/listCtrl.h"
#include <wx/sizer.h>
#include "deskgui/univ/ctrl/staticLine.h"
#include "deskgui/univ/ctrl/staticText.h"

#include "deskgui/docview/docManager.h"
#include "deskgui/univ/dialog.h"

struct ibChoiceTemplateItem
{
	ibFrontendDocTemplate* m_template;
	wxString m_description;
	wxIcon m_icon;
};

class ibDialogChoiceTemplate : public ibDialog {

	struct ibDialogChoiceTemplateItem {

		long m_index;

		ibFrontendDocTemplate* m_template;
		wxString m_description;
		wxIcon m_icon;
	};

public:

	ibFrontendDocTemplate* GetSelectionData() const
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
		ibDialog(NULL, wxID_ANY, _("Select document type"), wxDefaultPosition, wxSize(250, 225), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

		m_staticTextTemplate = new ibStaticText(this, wxID_ANY, _("Templates:"));
		m_staticTextTemplate->Wrap(-1);
		mainSizer->Add(m_staticTextTemplate, 0, wxALL, 5);

		m_listChoice = new ibListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_NO_HEADER | wxLC_REPORT | wxLC_SINGLE_SEL);
		m_listChoice->AppendColumn(wxT("template"), wxLIST_FORMAT_LEFT, 250);
		m_listChoice->SetImageList(m_imageList, wxIMAGE_LIST_SMALL);

		m_listChoice->Bind(wxEVT_COMMAND_LIST_ITEM_ACTIVATED, &ibDialogChoiceTemplate::OnitemActivated, this);
		m_listChoice->Bind(wxEVT_SIZE, &ibDialogChoiceTemplate::OnSize, this);

		for (const auto& choice : choices)
			AppendTemplate(choice.m_template, choice.m_description, choice.m_icon);

		mainSizer->Add(m_listChoice, 1, wxALL | wxEXPAND, 5);

		m_staticline = new ibStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
		mainSizer->Add(m_staticline, 0, wxEXPAND | wxALL, 5);

		m_sdbSizer = new ibStdDialogButtonSizer();
		m_sdbSizerOK = new ibButton(this, wxID_OK);
		m_sdbSizer->AddButton(m_sdbSizerOK);
		m_sdbSizerCancel = new ibButton(this, wxID_CANCEL);
		m_sdbSizer->AddButton(m_sdbSizerCancel);
		m_sdbSizer->Realize();

		mainSizer->Add(m_sdbSizer, 0, wxEXPAND, 5);

		ibDialog::SetSizer(mainSizer);
		ibDialog::Layout();
		ibDialog::Centre(wxBOTH);

		m_listChoice->SetFocus();
	}

	virtual ~ibDialogChoiceTemplate() { wxDELETE(m_imageList); }

private:

	void OnitemActivated(wxListEvent& event) {
		ibDialog::EndModal(wxID_OK);
		event.Skip();
	}

	void OnSize(wxSizeEvent& event) {
		m_listChoice->SetColumnWidth(0, event.m_size.x - 4);
		event.Skip();
	}

	void AppendTemplate(ibFrontendDocTemplate* docTemplate, const wxString& description, const wxIcon& icon) {

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

	ibStaticText* m_staticTextTemplate;
	ibListCtrl* m_listChoice;
	ibStaticLine* m_staticline;
	ibStdDialogButtonSizer* m_sdbSizer;
	ibButton* m_sdbSizerOK;
	ibButton* m_sdbSizerCancel;

	wxVector<ibDialogChoiceTemplateItem> m_choiceData;
};

#endif
