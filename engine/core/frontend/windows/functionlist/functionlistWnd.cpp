////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : function list window
////////////////////////////////////////////////////////////////////////////

#include "functionlistWnd.h"
#include "common/docInfo.h"

#include "frontend/codeEditor/codeEditorParser.h"
#include "metadata/metaObjectsDefines.h"

wxBEGIN_EVENT_TABLE(CFunctionList, wxDialog)
wxEND_EVENT_TABLE()

wxImageList *GetImageList();

CFunctionList::CFunctionList(CDocument *moduleDoc, CCodeEditorCtrl* parent)
	: wxDialog(parent, wxID_ANY, _("Procedures and functions")), m_docModule(moduleDoc), m_codeEditor(parent)
{
	m_OK = new wxButton(this, wxID_ANY, _("OK"));
	m_OK->Connect(wxEVT_BUTTON, wxCommandEventHandler(CFunctionList::OnButtonOk), NULL, this);
	m_Cancel = new wxButton(this, wxID_ANY, _("Cancel"));
	m_Cancel->Connect(wxEVT_BUTTON, wxCommandEventHandler(CFunctionList::OnButtonCancel), NULL, this);

	m_Sort = new wxCheckBox(this, wxID_ANY, _("Sort"));
	m_Sort->Connect(wxEVT_CHECKBOX, wxCommandEventHandler(CFunctionList::OnCheckBoxSort), NULL, this);

	wxBoxSizer *boxsizerList = new wxBoxSizer(wxHORIZONTAL);

	m_listProcedures = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_NO_HEADER);
	m_listProcedures->AppendColumn(_("Procedures and functions"), wxLIST_FORMAT_LEFT, GetSize().x - 10);

	m_listProcedures->Connect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(CFunctionList::OnItemSelected), NULL, this);

	m_listProcedures->SetImageList(::GetImageList(), wxIMAGE_LIST_SMALL);

	CMetaModuleObject *metaModule = dynamic_cast<CMetaModuleObject *>(moduleDoc->GetMetaObject());
	wxASSERT(metaModule);

	CParserModule moduleParser; std::vector<wxString> arrayProcedures; int maxLine = 0;

	if (moduleParser.ParseModule(metaModule->GetModuleText())) {
		for (auto content : moduleParser.GetAllContent()) {

			if (content.eType == eContentType::eExportFunction ||
				content.eType == eContentType::eFunction ||
				content.eType == eContentType::eExportProcedure ||
				content.eType == eContentType::eProcedure)
			{
				int item_id = m_listProcedures->GetItemCount();

				wxListItem info;
				info.m_image = content.nImage;
				info.m_text = content.sName;
				info.m_mask = wxLIST_MASK_TEXT | wxLIST_MASK_IMAGE | wxLIST_MASK_DATA;
				info.m_itemId = item_id;
				info.m_col = 0;

				long item = m_listProcedures->InsertItem(info);
				m_listProcedures->SetItemData(item, content.nLineStart + 1);

				m_aOffsets.insert_or_assign(item, offset_proc_t{ content.nLineStart + 1,  content.nLineEnd - content.nLineStart });

				maxLine = content.nLineEnd;
				arrayProcedures.push_back(content.sName);
			}
		}
	}

	//Get default proc 
	for (unsigned int idx = 0; idx < metaModule->GetDefaultProcedureCount(); idx++)
	{
		wxString procedureName = metaModule->GetDefaultProcedureName(idx);
		auto itFounded = std::find_if(arrayProcedures.begin(), arrayProcedures.end(),
			[&procedureName](const wxString& proc) { return proc.CompareTo(procedureName, wxString::caseCompare::ignoreCase) == 0; });

		if (itFounded != arrayProcedures.end())
			continue;

		int item_id = m_listProcedures->GetItemCount();

		wxListItem info;
		info.m_image = 0;//content.nImage;
		info.m_text = procedureName;
		info.m_mask = wxLIST_MASK_TEXT | wxLIST_MASK_IMAGE | wxLIST_MASK_DATA;
		info.m_itemId = item_id;
		info.m_col = 0;

		long item = m_listProcedures->InsertItem(info);
		m_listProcedures->SetItemData(item, 0);

		m_aOffsets.insert_or_assign(item, offset_proc_t{ maxLine,  wxNOT_FOUND });
	}

	wxBoxSizer *boxsizerButton = new wxBoxSizer(wxVERTICAL);
	boxsizerButton->Add(m_OK, 0, wxEXPAND);
	boxsizerButton->Add(m_Cancel, 0, wxEXPAND);
	boxsizerButton->AddSpacer(10);
	boxsizerButton->Add(m_Sort, 0, wxEXPAND);

	boxsizerList->Add(m_listProcedures, 1, wxEXPAND);
	boxsizerList->Add(boxsizerButton, 0, wxEXPAND);

	SetSizer(boxsizerList);
}

#include "metadata/metadata.h"

void CFunctionList::OnButtonOk(wxCommandEvent &event)
{
	long lSelectedItem = m_listProcedures->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	m_codeEditor->Raise();
	m_codeEditor->SetFocus();

	m_codeEditor->SetSTCFocus(true);

	CMetaModuleObject *metaModule = wxDynamicCast(
		m_docModule->GetMetaObject(), CMetaModuleObject
	);

	wxASSERT(metaModule);

	if (lSelectedItem != wxNOT_FOUND) {

		wxUIntPtr lineId =
			m_listProcedures->GetItemData(lSelectedItem);

		auto foundedIt = m_aOffsets.find(lSelectedItem);
		offset_proc_t line = foundedIt->second;

		if (line.m_offset != wxNOT_FOUND) {
			m_codeEditor->GotoLine(line.m_line - 1);
		}
		else {
			wxString procName = m_listProcedures->GetItemText(lSelectedItem);
			std::vector<wxString> procArgs;
			for (unsigned int idx = 0; idx < metaModule->GetDefaultProcedureCount(); idx++)
			{
				wxString procedureName = metaModule->GetDefaultProcedureName(idx);
				if (procName.CompareTo(procedureName, wxString::caseCompare::ignoreCase) == 0) {
					procArgs = metaModule->GetDefaultProcedureArgs(idx);
					break;
				}
			}

			wxString prcArgs = "";

			for (auto args : procArgs)
			{
				if (!prcArgs.IsEmpty()) {
					prcArgs += ", ";
				}
				prcArgs += args;
			}

			int endPos = m_codeEditor->GetLineEndPosition(line.m_line);

			wxString offset = endPos > 0 ? 
				"\r\n\r\n" : ""; 

			m_codeEditor->Replace(endPos, endPos,
				offset +
				"procedure " + m_listProcedures->GetItemText(lSelectedItem) + "(" + prcArgs + ")\r\n"
				"\t\r\n"
				"endProcedure"
			);

			m_codeEditor->GotoLine(line.m_line + (endPos > 0 ? 2 : 0) );
		}
	}

	EndModal(0); event.Skip();
}

void CFunctionList::OnButtonCancel(wxCommandEvent &event)
{
	m_codeEditor->Raise();
	m_codeEditor->SetFocus();

	m_codeEditor->SetSTCFocus(true);

	EndModal(1); event.Skip();
}

struct sortInfo_t
{
	wxListCtrl *m_listCtrl;
	bool m_sortOrder;
};

int wxCALLBACK CompareFunction(wxIntPtr item1, wxIntPtr item2, wxIntPtr sortData)
{
	sortInfo_t *sortInfo = (sortInfo_t *)sortData;
	wxListCtrl *listCtrl = sortInfo->m_listCtrl;

	long index1 = listCtrl->FindItem(-1, item1); // gets index of the first item
	long index2 = listCtrl->FindItem(-1, item2); // gets index of the second item

	if (sortInfo->m_sortOrder) {

		wxString string1 = listCtrl->GetItemText(index1);
		wxString string2 = listCtrl->GetItemText(index2);

		if (string1.Cmp(string2) < 0)
		{
			return -1;
		}
		else if (string1.Cmp(string2) > 0)
		{
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

void CFunctionList::OnCheckBoxSort(wxCommandEvent &event)
{
	m_sortInfo.m_listCtrl = m_listProcedures;
	m_sortInfo.m_sortOrder = m_Sort->GetValue();

	m_listProcedures->SortItems(CompareFunction, (wxIntPtr)&m_sortInfo);
	event.Skip();
}

void CFunctionList::OnItemSelected(wxListEvent &event)
{
	/*long lSelectedItem = m_listProcedures->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (lSelectedItem != wxNOT_FOUND) {
		wxUIntPtr line = m_listProcedures->GetItemData(lSelectedItem);
		m_codeEditor->GotoLine(line);
	}

	m_codeEditor->SetFocus();
	EndModal(0);*/ event.Skip();
}
