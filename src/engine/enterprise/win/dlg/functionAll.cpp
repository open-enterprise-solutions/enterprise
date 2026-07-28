#include "functionAll.h"
#include "backend/metadataConfiguration.h"
#include "frontend/visualView/ctrl/form.h"

#define ICON_SIZE 16

class ibMetaDataItem : public wxTreeItemData {
	ibValueMetaObject* m_metaObject;
public:
	ibMetaDataItem(ibValueMetaObject* metaObject) : m_metaObject(metaObject) {}
	ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
};

//////////////////////////////////////////////////////////////////////////

ibDialogFunctionAll::ibDialogFunctionAll(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
	: wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
	m_treeCtrlElements = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_TWIST_BUTTONS);
	m_treeCtrlElements->SetDoubleBuffered(true);
	bSizer->Add(m_treeCtrlElements, 1, wxALL | wxEXPAND, FromDIP(5));

	// Connect Events
	m_treeCtrlElements->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(ibDialogFunctionAll::OnTreeCtrlElementsOnLeftDClick), nullptr, this);

	//set image list
	m_treeCtrlElements->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	wxDialog::SetSizer(bSizer);
	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picStructureCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

wxTreeItemId ibDialogFunctionAll::AppendGroupItem(const wxTreeItemId& parent,
	const ibClassID& clsid, const wxString& name) const {
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	wxASSERT(typeCtor);
	wxImageList* imageList = m_treeCtrlElements->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(typeCtor->GetClassIcon());
	return m_treeCtrlElements->AppendItem(parent, name.IsEmpty() ? typeCtor->GetClassName() : name, imageIndex, imageIndex);
}

void ibDialogFunctionAll::BuildTree()
{
	wxImageList* imageList = m_treeCtrlElements->GetImageList();
	wxASSERT(imageList);
	wxTreeItemId root = m_treeCtrlElements->AddRoot(wxEmptyString);
	wxTreeItemId constants = AppendGroupItem(root, g_metaConstantCLSID, _("Constants"));
	for (auto constant : activeMetaData->GetAnyArrayObject(g_metaConstantCLSID)) {
		const int imageIndex = imageList->Add(constant->GetIcon());
		m_treeCtrlElements->AppendItem(constants, constant->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(constant));
	}
	wxTreeItemId catalogs = AppendGroupItem(root, g_metaCatalogCLSID, _("Catalogs"));
	for (auto catalog : activeMetaData->GetAnyArrayObject(g_metaCatalogCLSID)) {
		const int imageIndex = imageList->Add(catalog->GetIcon());
		m_treeCtrlElements->AppendItem(catalogs, catalog->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(catalog));
	}
	wxTreeItemId documents = AppendGroupItem(root, g_metaDocumentCLSID, _("Documents"));
	for (auto document : activeMetaData->GetAnyArrayObject(g_metaDocumentCLSID)) {
		const int imageIndex = imageList->Add(document->GetIcon());
		m_treeCtrlElements->AppendItem(documents, document->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(document));
	}
	wxTreeItemId dataProcessors = AppendGroupItem(root, g_metaDataProcessorCLSID, _("Data processors"));
	for (auto dataProcessor : activeMetaData->GetAnyArrayObject(g_metaDataProcessorCLSID)) {
		const int imageIndex = imageList->Add(dataProcessor->GetIcon());
		m_treeCtrlElements->AppendItem(dataProcessors, dataProcessor->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(dataProcessor));
	}
	wxTreeItemId reports = AppendGroupItem(root, g_metaReportCLSID, _("Reports"));
	for (auto report : activeMetaData->GetAnyArrayObject(g_metaReportCLSID)) {
		const int imageIndex = imageList->Add(report->GetIcon());
		m_treeCtrlElements->AppendItem(reports, report->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(report));
	}
	wxTreeItemId informationRegisters = AppendGroupItem(root, g_metaInformationRegisterCLSID, _("Information registers"));
	for (auto informationRegister : activeMetaData->GetAnyArrayObject(g_metaInformationRegisterCLSID)) {
		const int imageIndex = imageList->Add(informationRegister->GetIcon());
		m_treeCtrlElements->AppendItem(informationRegisters, informationRegister->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(informationRegister));
	}
	wxTreeItemId accumulationRegisters = AppendGroupItem(root, g_metaAccumulationRegisterCLSID, _("Accumulation registers"));
	for (auto accumulationRegister : activeMetaData->GetAnyArrayObject(g_metaAccumulationRegisterCLSID)) {
		const int imageIndex = imageList->Add(accumulationRegister->GetIcon());
		m_treeCtrlElements->AppendItem(accumulationRegisters, accumulationRegister->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(accumulationRegister));
	}
	wxTreeItemId chartsOfCharacteristicTypes = AppendGroupItem(root, g_metaChartOfCharacteristicTypesCLSID, _("Charts of characteristic types"));
	for (auto chartOfCharacteristicTypes : activeMetaData->GetAnyArrayObject(g_metaChartOfCharacteristicTypesCLSID)) {
		const int imageIndex = imageList->Add(chartOfCharacteristicTypes->GetIcon());
		m_treeCtrlElements->AppendItem(chartsOfCharacteristicTypes, chartOfCharacteristicTypes->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(chartOfCharacteristicTypes));
	}
	wxTreeItemId chartsOfAccounts = AppendGroupItem(root, g_metaChartOfAccountsCLSID, _("Charts of accounts"));
	for (auto chartOfAccounts : activeMetaData->GetAnyArrayObject(g_metaChartOfAccountsCLSID)) {
		const int imageIndex = imageList->Add(chartOfAccounts->GetIcon());
		m_treeCtrlElements->AppendItem(chartsOfAccounts, chartOfAccounts->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(chartOfAccounts));
	}
	wxTreeItemId accountingRegisters = AppendGroupItem(root, g_metaAccountingRegisterCLSID, _("Accounting registers"));
	for (auto accountingRegister : activeMetaData->GetAnyArrayObject(g_metaAccountingRegisterCLSID)) {
		const int imageIndex = imageList->Add(accountingRegister->GetIcon());
		m_treeCtrlElements->AppendItem(accountingRegisters, accountingRegister->GetSynonym(), imageIndex, imageIndex, new ibMetaDataItem(accountingRegister));
	}

	m_treeCtrlElements->ExpandAll();
}

void ibDialogFunctionAll::OnTreeCtrlElementsOnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId& selItem = m_treeCtrlElements->GetSelection();
	if (!selItem.IsOk())
		return;

	ibMetaDataItem* itemData = dynamic_cast<ibMetaDataItem*>(m_treeCtrlElements->GetItemData(selItem));
	if (itemData != nullptr) {
		ibBackendCommandItem* metaObject = dynamic_cast<ibBackendCommandItem*>(itemData->GetMetaObject());
		if (metaObject != nullptr && metaObject->Execute())
			Close(true);
	}

	event.Skip();
}