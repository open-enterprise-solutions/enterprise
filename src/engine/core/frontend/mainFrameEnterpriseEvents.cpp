////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "metadata/metadata.h"
#include "metadata/metaObjectsDefines.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "metadata/metaObjects/objects/constant.h"
#include "frontend/visualView/controls/form.h"

#include <wx/dialog.h>
#include <wx/treectrl.h>

extern wxImageList* GetImageList();

void CMainFrameEnterprise::OnClickAllOperation(wxCommandEvent& event)
{
	class CDialogOperationWnd : public wxDialog {
		class CMetaDataItem : public wxTreeItemData {
			IMetaObject* m_metaObject;
		public:
			CMetaDataItem(IMetaObject* metaObject) : m_metaObject(metaObject) {}
			IMetaObject* GetMetaObject() const { return m_metaObject; }
		};
		wxTreeCtrl* m_treeCtrlElements;
	public:

		virtual bool Show(bool show = true) override {
			if (show) {
				CDialogOperationWnd::BuildOperation();
			}
			return wxDialog::Show(show);
		}

		// show the dialog modally and return the value passed to EndModal()
		virtual int ShowModal() override {
			CDialogOperationWnd::BuildOperation();
			return wxDialog::ShowModal();
		}

		CDialogOperationWnd(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("All operations"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(300, 400), long style = wxDEFAULT_DIALOG_STYLE)
			: wxDialog(parent, id, title, pos, size, style)
		{
			this->SetSizeHints(wxDefaultSize, wxDefaultSize);

			wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);

			//m_buttonOpen = new wxButton(this, wxID_ANY, wxT("Open"), wxDefaultPosition, wxDefaultSize, 0);
			//bSizer->Add(m_buttonOpen, 0, wxALL | wxEXPAND, 5);

			m_treeCtrlElements = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_SINGLE | wxTR_HIDE_ROOT);
			m_treeCtrlElements->SetDoubleBuffered(true);

			bSizer->Add(m_treeCtrlElements, 1, wxALL | wxEXPAND, 5);

			// Connect Events
			m_treeCtrlElements->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(CDialogOperationWnd::OnTreeCtrlElementsOnLeftDClick), NULL, this);

			//set image list
			m_treeCtrlElements->SetImageList(::GetImageList());

			wxDialog::SetSizer(bSizer);
			wxDialog::Layout();

			wxDialog::Centre(wxBOTH);
		}

		void BuildOperation()
		{
			wxTreeItemId root = m_treeCtrlElements->AddRoot(wxEmptyString);

			wxTreeItemId constants = m_treeCtrlElements->AppendItem(root, _("Constants"), 86, 86);
			for (auto constant : metadata->GetMetaObjects(g_metaConstantCLSID)) {
				m_treeCtrlElements->AppendItem(constants, constant->GetSynonym(), 219, 219, new CMetaDataItem(constant));
			}

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
			wxTreeItemId informationRegisters = m_treeCtrlElements->AppendItem(root, _("Information registers"), 209, 209);
			for (auto informationRegister : metadata->GetMetaObjects(g_metaInformationRegisterCLSID)) {
				m_treeCtrlElements->AppendItem(informationRegisters, informationRegister->GetSynonym(), 210, 210, new CMetaDataItem(informationRegister));
			}
			wxTreeItemId accumulationRegisters = m_treeCtrlElements->AppendItem(root, _("Accumulation registers"), 209, 209);
			for (auto accumulationRegister : metadata->GetMetaObjects(g_metaAccumulationRegisterCLSID)) {
				m_treeCtrlElements->AppendItem(accumulationRegisters, accumulationRegister->GetSynonym(), 210, 210, new CMetaDataItem(accumulationRegister));
			}

			m_treeCtrlElements->ExpandAll();
		}

		virtual void OnTreeCtrlElementsOnLeftDClick(wxMouseEvent& event)
		{
			wxTreeItemId selItem = m_treeCtrlElements->GetSelection();

			if (!selItem.IsOk())
				return;

			CMetaDataItem* itemData = dynamic_cast<CMetaDataItem*>(
				m_treeCtrlElements->GetItemData(selItem)
				);

			if (itemData != NULL) {

				IMetaCommandData* metaObject =
					dynamic_cast<IMetaCommandData*>(itemData->GetMetaObject());

				if (metaObject != NULL) {
					CValueForm* valueForm = metaObject->GetDefaultCommandForm();
					wxASSERT(valueForm);
					valueForm->ShowForm(); Close(true);
				}
			}

			event.Skip();
		}

		virtual ~CDialogOperationWnd() {
			m_treeCtrlElements->Disconnect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(CDialogOperationWnd::OnTreeCtrlElementsOnLeftDClick), NULL, this);
		}
	};

	CDialogOperationWnd* dialogOperations
		= new CDialogOperationWnd(this, wxID_ANY);

	dialogOperations->Show();
	event.Skip();
}

#include "frontend/windows/optionsWnd.h"

void CMainFrameEnterprise::OnToolsSettings(wxCommandEvent& event)
{
	COptionsWnd optionsDlg(this, wxID_ANY);
	optionsDlg.ShowModal();
}

#include "frontend/windows/activeUsersWnd.h"

void CMainFrameEnterprise::OnActiveUsers(wxCommandEvent& event)
{
	CActiveUsersWnd* activeUsers = new CActiveUsersWnd(this, wxID_ANY);
	activeUsers->Show();
}

#include "frontend/windows/aboutWnd.h"

void CMainFrameEnterprise::OnAbout(wxCommandEvent& event)
{
	CAboutDialogWnd aboutDlg(this, wxID_ANY);
	aboutDlg.ShowModal();
}