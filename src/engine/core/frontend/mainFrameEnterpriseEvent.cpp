////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "core/metadata/metadata.h"
#include "core/metadata/metaObjectsDefines.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metaObjects/objects/constant.h"
#include "frontend/visualView/controls/form.h"

#include <wx/dialog.h>
#include <wx/treectrl.h>

#define ICON_SIZE 16

void wxAuiDocEnterpriseMDIFrame::OnClickAllOperation(wxCommandEvent& event)
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

		wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
			const CLASS_ID& clsid, const wxString& name = wxEmptyString) const {
			IObjectValueAbstract* singleObject = CValue::GetAvailableObject(clsid);
			wxASSERT(singleObject);
			wxImageList* imageList = m_treeCtrlElements->GetImageList();
			wxASSERT(imageList);
			int imageIndex = imageList->Add(singleObject->GetClassIcon());
			return m_treeCtrlElements->AppendItem(parent, name.IsEmpty() ? singleObject->GetClassName() : name, imageIndex, imageIndex);
		}

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
			m_treeCtrlElements = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_SINGLE | wxTR_HIDE_ROOT);
			m_treeCtrlElements->SetDoubleBuffered(true);
			bSizer->Add(m_treeCtrlElements, 1, wxALL | wxEXPAND, 5);

			// Connect Events
			m_treeCtrlElements->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(CDialogOperationWnd::OnTreeCtrlElementsOnLeftDClick), NULL, this);

			//set image list
			m_treeCtrlElements->SetImageList(
				new wxImageList(ICON_SIZE, ICON_SIZE)
			);

			wxDialog::SetSizer(bSizer);
			wxDialog::Layout();

			wxDialog::Centre(wxBOTH);
		}

		void BuildOperation()
		{
			wxImageList* imageList = m_treeCtrlElements->GetImageList();
			wxASSERT(imageList);
			wxTreeItemId root = m_treeCtrlElements->AddRoot(wxEmptyString);
			wxTreeItemId constants = AppendGroupItem(root, g_metaConstantCLSID, _("Constants"));
			for (auto constant : metadata->GetMetaObjects(g_metaConstantCLSID)) {
				int imageIndex = imageList->Add(constant->GetIcon());
				m_treeCtrlElements->AppendItem(constants, constant->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(constant));
			}
			wxTreeItemId catalogs = AppendGroupItem(root, g_metaCatalogCLSID, _("Catalogs"));
			for (auto catalog : metadata->GetMetaObjects(g_metaCatalogCLSID)) {
				int imageIndex = imageList->Add(catalog->GetIcon());
				m_treeCtrlElements->AppendItem(catalogs, catalog->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(catalog));
			}
			wxTreeItemId documents = AppendGroupItem(root, g_metaDocumentCLSID, _("Documents"));
			for (auto document : metadata->GetMetaObjects(g_metaDocumentCLSID)) {
				int imageIndex = imageList->Add(document->GetIcon());
				m_treeCtrlElements->AppendItem(documents, document->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(document));
			}
			wxTreeItemId dataProcessors = AppendGroupItem(root, g_metaDataProcessorCLSID, _("Data processors"));
			for (auto dataProcessor : metadata->GetMetaObjects(g_metaDataProcessorCLSID)) {
				int imageIndex = imageList->Add(dataProcessor->GetIcon());
				m_treeCtrlElements->AppendItem(dataProcessors, dataProcessor->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(dataProcessor));
			}
			wxTreeItemId reports = AppendGroupItem(root, g_metaReportCLSID, _("Reports"));
			for (auto report : metadata->GetMetaObjects(g_metaReportCLSID)) {
				int imageIndex = imageList->Add(report->GetIcon());
				m_treeCtrlElements->AppendItem(reports, report->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(report));
			}
			wxTreeItemId informationRegisters = AppendGroupItem(root, g_metaInformationRegisterCLSID, _("Information registers"));
			for (auto informationRegister : metadata->GetMetaObjects(g_metaInformationRegisterCLSID)) {
				int imageIndex = imageList->Add(informationRegister->GetIcon());
				m_treeCtrlElements->AppendItem(informationRegisters, informationRegister->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(informationRegister));
			}
			wxTreeItemId accumulationRegisters = AppendGroupItem(root, g_metaAccumulationRegisterCLSID, _("Accumulation registers"));
			for (auto accumulationRegister : metadata->GetMetaObjects(g_metaAccumulationRegisterCLSID)) {
				int imageIndex = imageList->Add(accumulationRegister->GetIcon());
				m_treeCtrlElements->AppendItem(accumulationRegisters, accumulationRegister->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(accumulationRegister));
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

void wxAuiDocEnterpriseMDIFrame::OnToolsSettings(wxCommandEvent& event)
{
	COptionsWnd optionsDlg(this, wxID_ANY);
	optionsDlg.ShowModal();
}

#include "frontend/windows/activeUsersWnd.h"

void wxAuiDocEnterpriseMDIFrame::OnActiveUsers(wxCommandEvent& event)
{
	CActiveUsersWnd* activeUsers = new CActiveUsersWnd(this, wxID_ANY);
	activeUsers->Show();
}

#include "frontend/windows/aboutWnd.h"

void wxAuiDocEnterpriseMDIFrame::OnAbout(wxCommandEvent& event)
{
	CAboutDialogWnd aboutDlg(this, wxID_ANY);
	aboutDlg.ShowModal();
}