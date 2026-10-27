////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : function list window
////////////////////////////////////////////////////////////////////////////

#include "functionSearcher.h"

#include "codeRunner/artProvider/artProvider.h"

#include "codeRunner/win/editor/codeEditor/codeEditor.h"
#include "core/compiler/scriptParseCode.h"

#define ICON_SIZE 16

ibFunctionList::ibFunctionList(ibCodeEditor* parent)
	: wxDialog(parent, wxID_ANY, _("Procedures and functions")), m_codeEditor(parent)
{
	// The window wears the picture of the button that opens it.
	SetIcon(wxArtProvider::GetIcon(wxART_PROC_AND_FUNC, wxART_DOC_MODULE, FromDIP(wxSize(16, 16))));

	m_OK = new wxButton(this, wxID_ANY, _("OK"));
	m_OK->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibFunctionList::OnButtonOk), nullptr, this);
	m_Cancel = new wxButton(this, wxID_ANY, _("Cancel"));
	m_Cancel->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibFunctionList::OnButtonCancel), nullptr, this);

	m_Sort = new wxCheckBox(this, wxID_ANY, _("Sort"));
	m_Sort->Connect(wxEVT_CHECKBOX, wxCommandEventHandler(ibFunctionList::OnCheckBoxSort), nullptr, this);

	wxBoxSizer* boxsizerList = new wxBoxSizer(wxHORIZONTAL);

	m_listProcedures = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_NO_HEADER);
	m_listProcedures->AppendColumn(_("Procedures and functions"), wxLIST_FORMAT_LEFT, GetSize().x - 10);

	m_listProcedures->Connect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(ibFunctionList::OnItemSelected), nullptr, this);

	wxImageList* imageList = new wxImageList(ICON_SIZE, ICON_SIZE);
	const int procImage = imageList->Add(wxArtProvider::GetIcon(wxART_PROCEDURE, wxART_AUTOCOMPLETE));
	const int funcImage = imageList->Add(wxArtProvider::GetIcon(wxART_FUNCTION, wxART_AUTOCOMPLETE));
	m_listProcedures->AssignImageList(imageList, wxIMAGE_LIST_SMALL);

	// The editor's own text: there is no module behind it.
	const wxString moduleText = m_codeEditor != nullptr ? m_codeEditor->GetText() : wxString();

	ibParseCode moduleParser(ibCompileCode::GetCodeStyle()); std::vector<wxString> arrayProcedures;

	if (moduleParser.ParseModule(moduleText)) {
		for (auto content : moduleParser.GetAllContent()) {

			if (content.m_eType == ibContentType::eExportFunction ||
				content.m_eType == ibContentType::eFunction ||
				content.m_eType == ibContentType::eExportProcedure ||
				content.m_eType == ibContentType::eProcedure) {
				wxListItem info;
				info.m_text = content.m_name;
				info.m_mask = wxLIST_MASK_TEXT | wxLIST_MASK_IMAGE | wxLIST_MASK_DATA;
				info.m_itemId = m_listProcedures->GetItemCount();
				info.m_col = 0;

				if (content.m_eType == ibContentType::eExportFunction ||
					content.m_eType == ibContentType::eFunction)
					info.m_image = funcImage;
				else
					info.m_image = procImage;

				long item = m_listProcedures->InsertItem(info);
				m_listProcedures->SetItemData(item, content.m_lineStart + 1);

				m_aOffsets.insert_or_assign(item,
					offset_proc_t{
						content.m_lineStart + 1,
						content.m_lineEnd - content.m_lineStart
					}
				);

				arrayProcedures.push_back(content.m_name);
			}
		}
	}

	wxBoxSizer* boxsizerButton = new wxBoxSizer(wxVERTICAL);
	boxsizerButton->Add(m_OK, 0, wxEXPAND);
	boxsizerButton->Add(m_Cancel, 0, wxEXPAND);
	boxsizerButton->AddSpacer(10);
	boxsizerButton->Add(m_Sort, 0, wxEXPAND);

	boxsizerList->Add(m_listProcedures, 1, wxEXPAND);
	boxsizerList->Add(boxsizerButton, 0, wxEXPAND);

	SetSizer(boxsizerList);
}

void ibFunctionList::OnButtonOk(wxCommandEvent& event)
{
	long lSelectedItem = m_listProcedures->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	m_codeEditor->Raise();
	m_codeEditor->SetFocus();

	m_codeEditor->SetSTCFocus(true);

	if (lSelectedItem != wxNOT_FOUND) {
		auto it = m_aOffsets.find(lSelectedItem);
		m_codeEditor->GotoLine(it->second.m_line - 1);
	}

	EndModal(0); event.Skip();
}

void ibFunctionList::OnButtonCancel(wxCommandEvent& event)
{
	m_codeEditor->Raise();
	m_codeEditor->SetFocus();

	m_codeEditor->SetSTCFocus(true);

	EndModal(1); event.Skip();
}

struct sortInfo_t
{
	wxListCtrl* m_listCtrl;
	bool m_sortOrder;
};

int wxCALLBACK CompareFunction(wxIntPtr item1, wxIntPtr item2, wxIntPtr sortData)
{
	sortInfo_t* sortInfo = (sortInfo_t*)sortData;
	wxListCtrl* listCtrl = sortInfo->m_listCtrl;

	long index1 = listCtrl->FindItem(-1, item1); // gets index of the first item
	long index2 = listCtrl->FindItem(-1, item2); // gets index of the second item

	if (sortInfo->m_sortOrder) {

		wxString string1 = listCtrl->GetItemText(index1);
		wxString string2 = listCtrl->GetItemText(index2);

		if (string1.Cmp(string2) < 0) {
			return -1;
		}
		else if (string1.Cmp(string2) > 0) {
			return 1;
		}

	}
	else {

		wxIntPtr line1 = listCtrl->GetItemData(index1);
		wxIntPtr line2 = listCtrl->GetItemData(index2);

		if (line1 < line2)
			return -1;
		if (line1 > line2)
			return 1;
	}

	return 0;
}

static sortInfo_t m_sortInfo;

void ibFunctionList::OnCheckBoxSort(wxCommandEvent& event)
{
	m_sortInfo.m_listCtrl = m_listProcedures;
	m_sortInfo.m_sortOrder = m_Sort->GetValue();

	m_listProcedures->SortItems(CompareFunction, (wxIntPtr)&m_sortInfo);
	event.Skip();
}

void ibFunctionList::OnItemSelected(wxListEvent& event)
{
	/*long lSelectedItem = m_listProcedures->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (lSelectedItem != wxNOT_FOUND) {
		wxUIntPtr line = m_listProcedures->GetItemData(lSelectedItem);
		m_codeEditor->GotoLine(line);
	}

	m_codeEditor->SetFocus();
	EndModal(0);*/ event.Skip();
}
