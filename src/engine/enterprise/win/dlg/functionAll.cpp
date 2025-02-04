#include "functionAll.h"
#include "backend/metadataConfiguration.h"
#include "frontend/visualView/ctrl/form.h"

#define ICON_SIZE 16

class CMetaDataItem : public wxTreeItemData {
	IMetaObject* m_metaObject;
public:
	CMetaDataItem(IMetaObject* metaObject) : m_metaObject(metaObject) {}
	IMetaObject* GetMetaObject() const { return m_metaObject; }
};

//////////////////////////////////////////////////////////////////////////

CDialogFunctionAll::CDialogFunctionAll(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
	: wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
	m_treeCtrlElements = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_SINGLE | wxTR_HIDE_ROOT);
	m_treeCtrlElements->SetDoubleBuffered(true);
	bSizer->Add(m_treeCtrlElements, 1, wxALL | wxEXPAND, 5);

	// Connect Events
	m_treeCtrlElements->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(CDialogFunctionAll::OnTreeCtrlElementsOnLeftDClick), nullptr, this);

	//set image list
	m_treeCtrlElements->SetImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	wxDialog::SetSizer(bSizer);
	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);
}

wxTreeItemId CDialogFunctionAll::AppendGroupItem(const wxTreeItemId& parent,
	const class_identifier_t& clsid, const wxString& name) const {
	IAbstractTypeCtor* typeCtor = CValue::GetAvailableCtor(clsid);
	wxASSERT(typeCtor);
	wxImageList* imageList = m_treeCtrlElements->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(typeCtor->GetClassIcon());
	return m_treeCtrlElements->AppendItem(parent, name.IsEmpty() ? typeCtor->GetClassName() : name, imageIndex, imageIndex);
}

void CDialogFunctionAll::BuildTree()
{
	wxImageList* imageList = m_treeCtrlElements->GetImageList();
	wxASSERT(imageList);
	wxTreeItemId root = m_treeCtrlElements->AddRoot(wxEmptyString);
	wxTreeItemId constants = AppendGroupItem(root, g_metaConstantCLSID, _("Constants"));
	for (auto constant : commonMetaData->GetMetaObject(g_metaConstantCLSID)) {
		int imageIndex = imageList->Add(constant->GetIcon());
		m_treeCtrlElements->AppendItem(constants, constant->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(constant));
	}
	wxTreeItemId catalogs = AppendGroupItem(root, g_metaCatalogCLSID, _("Catalogs"));
	for (auto catalog : commonMetaData->GetMetaObject(g_metaCatalogCLSID)) {
		int imageIndex = imageList->Add(catalog->GetIcon());
		m_treeCtrlElements->AppendItem(catalogs, catalog->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(catalog));
	}
	wxTreeItemId documents = AppendGroupItem(root, g_metaDocumentCLSID, _("Documents"));
	for (auto document : commonMetaData->GetMetaObject(g_metaDocumentCLSID)) {
		int imageIndex = imageList->Add(document->GetIcon());
		m_treeCtrlElements->AppendItem(documents, document->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(document));
	}
	wxTreeItemId dataProcessors = AppendGroupItem(root, g_metaDataProcessorCLSID, _("Data processors"));
	for (auto dataProcessor : commonMetaData->GetMetaObject(g_metaDataProcessorCLSID)) {
		int imageIndex = imageList->Add(dataProcessor->GetIcon());
		m_treeCtrlElements->AppendItem(dataProcessors, dataProcessor->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(dataProcessor));
	}
	wxTreeItemId reports = AppendGroupItem(root, g_metaReportCLSID, _("Reports"));
	for (auto report : commonMetaData->GetMetaObject(g_metaReportCLSID)) {
		int imageIndex = imageList->Add(report->GetIcon());
		m_treeCtrlElements->AppendItem(reports, report->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(report));
	}
	wxTreeItemId informationRegisters = AppendGroupItem(root, g_metaInformationRegisterCLSID, _("Information registers"));
	for (auto informationRegister : commonMetaData->GetMetaObject(g_metaInformationRegisterCLSID)) {
		int imageIndex = imageList->Add(informationRegister->GetIcon());
		m_treeCtrlElements->AppendItem(informationRegisters, informationRegister->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(informationRegister));
	}
	wxTreeItemId accumulationRegisters = AppendGroupItem(root, g_metaAccumulationRegisterCLSID, _("Accumulation registers"));
	for (auto accumulationRegister : commonMetaData->GetMetaObject(g_metaAccumulationRegisterCLSID)) {
		int imageIndex = imageList->Add(accumulationRegister->GetIcon());
		m_treeCtrlElements->AppendItem(accumulationRegisters, accumulationRegister->GetSynonym(), imageIndex, imageIndex, new CMetaDataItem(accumulationRegister));
	}

	m_treeCtrlElements->ExpandAll();
}


void CDialogFunctionAll::OnTreeCtrlElementsOnLeftDClick(wxMouseEvent& event)
{
	wxTreeItemId selItem = m_treeCtrlElements->GetSelection();
	if (!selItem.IsOk())
		return;

	CMetaDataItem* itemData = dynamic_cast<CMetaDataItem*>(m_treeCtrlElements->GetItemData(selItem));
	if (itemData != nullptr) {
		IMetaCommandData* metaObject = dynamic_cast<IMetaCommandData*>(itemData->GetMetaObject());
		if (metaObject != nullptr) {
			IBackendValueForm* valueForm = nullptr;
			try {
				valueForm = metaObject->GetDefaultCommandForm();
				wxASSERT(valueForm);
				valueForm->ShowForm(); Close(true);
			}
			catch (const CBackendException* err) {
				wxMessageBox(err->what());
				wxDELETE(valueForm);
			}
		}
	}

	event.Skip();
}