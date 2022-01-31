////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "metadata/metadata.h"
#include "metadata/metaObjectsDefines.h"
#include "metadata/objects/baseObject.h"
#include "frontend/visualView/controls/form.h"

#include <wx/dialog.h>
#include <wx/treectrl.h>

extern wxImageList *GetImageList();

void CMainFrameEnterprise::OnClickAllOperaions(wxCommandEvent &event)
{
	class CDialogOperations : public wxDialog {
		class CMetaDataItem : public wxTreeItemData {
			IMetaObject *m_metaObject;
		public:
			CMetaDataItem(IMetaObject *metaObject) : m_metaObject(metaObject) {}
			IMetaObject *GetMetaObject() { return m_metaObject; }
		};
		wxTreeCtrl* m_treeCtrlElements;
	public:

		CDialogOperations(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("All operations"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(480, 300), long style = wxDEFAULT_DIALOG_STYLE)
			: wxDialog(parent, id, title, pos, size, style)
		{
			this->SetSizeHints(wxDefaultSize, wxDefaultSize);

			wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);

			//m_buttonOpen = new wxButton(this, wxID_ANY, wxT("Open"), wxDefaultPosition, wxDefaultSize, 0);
			//bSizer->Add(m_buttonOpen, 0, wxALL | wxEXPAND, 5);

			m_treeCtrlElements = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_SINGLE | wxTR_HIDE_ROOT);
			bSizer->Add(m_treeCtrlElements, 1, wxALL | wxEXPAND, 5);

			// Connect Events
			m_treeCtrlElements->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(CDialogOperations::OnTreeCtrlElementsOnLeftDClick), NULL, this);

			//set image list
			m_treeCtrlElements->SetImageList(::GetImageList());

			wxDialog::SetSizer(bSizer);
			wxDialog::Layout();

			wxDialog::Centre(wxBOTH);
		}

		void BuildOperations()
		{
			wxTreeItemId root = m_treeCtrlElements->AddRoot(wxEmptyString);

			wxTreeItemId catalogs = m_treeCtrlElements->AppendItem(root, _("Catalogs"), 85, 85);
			for (auto catalog : metadata->GetMetaObjects(g_metaCatalogCLSID)) {
				m_treeCtrlElements->AppendItem(catalogs, catalog->GetSynonym(), 226, 226, new CMetaDataItem(catalog));
			}
			wxTreeItemId documents = m_treeCtrlElements->AppendItem(root, _("Documents"), 171, 171);
			for (auto document : metadata->GetMetaObjects(g_metaDocumentCLSID)) {
				m_treeCtrlElements->AppendItem(documents, document->GetSynonym(), 216, 216, new CMetaDataItem(document));
			}
			wxTreeItemId dataProcessors = m_treeCtrlElements->AppendItem(root, _("Data processors"), 88, 88);
			for (auto dataProcessor : metadata->GetMetaObjects(g_metaDataProcessorCLSID)) {
				m_treeCtrlElements->AppendItem(dataProcessors, dataProcessor->GetSynonym(), 600, 600, new CMetaDataItem(dataProcessor));
			}
			wxTreeItemId reports = m_treeCtrlElements->AppendItem(root, _("Reports"), 87, 87);
			for (auto report : metadata->GetMetaObjects(g_metaReportCLSID)) {
				m_treeCtrlElements->AppendItem(reports, report->GetSynonym(), 598, 598, new CMetaDataItem(report));
			}

			m_treeCtrlElements->ExpandAll();
		}

		virtual void OnTreeCtrlElementsOnLeftDClick(wxMouseEvent& event)
		{
			wxTreeItemId selItem = m_treeCtrlElements->GetSelection();

			if (!selItem.IsOk())
				return;

			CMetaDataItem *itemData = dynamic_cast<CMetaDataItem *>(m_treeCtrlElements->GetItemData(selItem));

			if (itemData) {
				IMetaObjectValue *metaObject =
					wxStaticCast(itemData->GetMetaObject(), IMetaObjectValue
					);

				if (metaObject->IsRefObject()) {
					IMetaObjectRefValue *metaRefObj = wxStaticCast(metaObject, IMetaObjectRefValue);
					wxASSERT(metaRefObj);
					CValueForm *valueForm = metaRefObj->GetListForm();
					valueForm->ShowForm(); Close();
				}
				else {
					wxASSERT(metaObject);
					CValueForm *valueForm = metaObject->GetObjectForm();
					valueForm->ShowForm(); Close();
				}
			}

			event.Skip();
		}

		virtual ~CDialogOperations() {
			m_treeCtrlElements->Disconnect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(CDialogOperations::OnTreeCtrlElementsOnLeftDClick), NULL, this);
		}
	};

	CDialogOperations *dialogOperations = 
		new CDialogOperations(this, wxID_ANY);

	dialogOperations->BuildOperations();
	dialogOperations->Show();

	event.Skip();
}

#include "frontend/windows/optionsWnd.h"

void CMainFrameEnterprise::OnToolsSettings(wxCommandEvent &event)
{
	COptionsWnd *optionsDlg = new COptionsWnd(this, wxID_ANY);
	optionsDlg->ShowModal();
}

#include "frontend/windows/activeUsersWnd.h"

void CMainFrameEnterprise::OnActiveUsers(wxCommandEvent & event)
{
	CActiveUsersWnd *activeUsers = new CActiveUsersWnd(this, wxID_ANY);
	activeUsers->Show();
}

#include "frontend/windows/aboutWnd.h"

void CMainFrameEnterprise::OnAbout(wxCommandEvent &event)
{
	CAboutDialogWnd *aboutDlg = new CAboutDialogWnd(this, wxID_ANY);
	aboutDlg->ShowModal();
}