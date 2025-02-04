#include "roleEditor.h"

#define ICON_SIZE 16

CRoleEditor::CRoleEditor(wxWindow* parent,
	wxWindowID winid, IMetaObject* metaObject) :
	wxSplitterWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE), m_metaRole(metaObject)
{
	m_roleCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_SINGLE | wxTR_TWIST_BUTTONS);
	m_roleCtrl->SetDoubleBuffered(true);
	m_roleCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &CRoleEditor::OnSelectedItem, this);

	//set image list
	m_roleCtrl->SetImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_checkCtrl = new wxCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER);
	m_checkCtrl->SetDoubleBuffered(true);
	m_checkCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &CRoleEditor::OnCheckItem, this);

	InitRole();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* treeSizer = new wxBoxSizer(wxHORIZONTAL);
	treeSizer->Add(m_roleCtrl, 2, wxEXPAND);
	treeSizer->Add(m_checkCtrl, 1, wxEXPAND);
	mainSizer->Add(treeSizer, 1, wxEXPAND, 1);

	wxSplitterWindow::SplitVertically(m_roleCtrl, m_checkCtrl, -300);

	wxSplitterWindow::SetSizer(mainSizer);
	wxSplitterWindow::Layout();
}

CRoleEditor::~CRoleEditor() {
	m_roleCtrl->Unbind(wxEVT_TREE_SEL_CHANGED, &CRoleEditor::OnSelectedItem, this);
	m_checkCtrl->Unbind(wxEVT_CHECKTREE_CHOICE, &CRoleEditor::OnCheckItem, this);
}

void CRoleEditor::OnCheckItem(wxTreeEvent& event)
{
	wxTreeItemRoleData* data = dynamic_cast<wxTreeItemRoleData*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	if (data != nullptr) {
		Role* role = data->GetRole();
		wxASSERT(role);
		IMetaObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		metaObject->SetRight(role, m_metaRole->GetMetaID(), event.GetExtraLong());
	}

	event.Skip();
}

void CRoleEditor::OnSelectedItem(wxTreeEvent& event) {
	wxTreeItemMetaData* data = dynamic_cast<wxTreeItemMetaData*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	m_checkCtrl->Freeze();
	m_checkCtrl->DeleteAllItems();
	if (data != nullptr) {
		IMetaObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		wxTreeItemId root = m_checkCtrl->AddRoot(wxEmptyString);
		for (unsigned int idx = 0; idx < metaObject->GetRoleCount(); idx++) {
			Role* role = metaObject->GetRole(idx);
			wxASSERT(role);
			wxTreeItemId newItem = m_checkCtrl->AppendItem(root, role->GetLabel(), wxNOT_FOUND, wxNOT_FOUND, new wxTreeItemRoleData(role));
			m_checkCtrl->SetItemState(newItem, wxCheckTree::UNCHECKED);
			m_checkCtrl->Check(newItem, metaObject->AccessRight(role, m_metaRole->GetMetaID()));
		}
	}
	m_checkCtrl->Thaw();
	event.Skip();
}

#include "frontend/artProvider/artProvider.h"

#define metadataName _("metaData")

#define commonName _("common")
#define commonFormsName _("common forms")

#define constantsName _("constants")

#define catalogsName _("catalogs")
#define documentsName _("documents")
#define dataProcessorName _("data processors")
#define reportsName _("reports")
#define informationRegisterName _("information Registers")
#define accumulationRegisterName _("accumulation Registers")

void CRoleEditor::InitRole()
{
	IAbstractTypeCtor* typeCtor = CValue::GetAvailableCtor(g_metaCommonMetadataCLSID);
	wxASSERT(typeCtor);

	wxImageList* imageList = m_roleCtrl->GetImageList();
	int imageIndex = imageList->Add(typeCtor->GetClassIcon());
	m_treeMETADATA = m_roleCtrl->AddRoot(_("configuration"), imageIndex, imageIndex, new wxTreeItemMetaData(commonMetaData->GetCommonMetaObject()));

	m_roleCtrl->SelectItem(m_treeMETADATA);

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	int imageCommonIndex = imageList->Add(wxArtProvider::GetIcon(wxART_COMMON_FOLDER, wxART_METATREE));
	m_treeCOMMON = m_roleCtrl->AppendItem(m_treeMETADATA, commonName, imageCommonIndex, imageCommonIndex);

	///////////////////////////////////////////////////////////////////////////////////////////////////////

	m_treeFORMS = AppendGroupItem(m_treeCOMMON, g_metaCommonFormCLSID, commonFormsName);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	m_treeCONSTANTS = AppendGroupItem(m_treeMETADATA, g_metaConstantCLSID, constantsName);
	m_treeCATALOGS = AppendGroupItem(m_treeMETADATA, g_metaCatalogCLSID, catalogsName);
	m_treeDOCUMENTS = AppendGroupItem(m_treeMETADATA, g_metaDocumentCLSID, documentsName);

	m_treeDATAPROCESSORS = AppendGroupItem(m_treeMETADATA, g_metaDataProcessorCLSID, dataProcessorName);
	m_treeREPORTS = AppendGroupItem(m_treeMETADATA, g_metaReportCLSID, reportsName);

	m_treeINFORMATION_REGISTERS = AppendGroupItem(m_treeMETADATA, g_metaInformationRegisterCLSID, informationRegisterName);
	m_treeACCUMULATION_REGISTERS = AppendGroupItem(m_treeMETADATA, g_metaAccumulationRegisterCLSID, accumulationRegisterName);

	//Set item bold and name
	m_roleCtrl->SetItemText(m_treeMETADATA, _("configuration"));
	m_roleCtrl->SetItemBold(m_treeMETADATA);

	m_roleCtrl->ExpandAll();
}

void CRoleEditor::ClearRole() {

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	if (m_treeFORMS.IsOk()) m_roleCtrl->DeleteChildren(m_treeFORMS);

	if (m_treeCONSTANTS.IsOk()) m_roleCtrl->DeleteChildren(m_treeCONSTANTS);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	if (m_treeCATALOGS.IsOk()) m_roleCtrl->DeleteChildren(m_treeCATALOGS);
	if (m_treeDOCUMENTS.IsOk()) m_roleCtrl->DeleteChildren(m_treeDOCUMENTS);

	if (m_treeDATAPROCESSORS.IsOk()) m_roleCtrl->DeleteChildren(m_treeDATAPROCESSORS);
	if (m_treeREPORTS.IsOk()) m_roleCtrl->DeleteChildren(m_treeREPORTS);
	if (m_treeINFORMATION_REGISTERS.IsOk()) m_roleCtrl->DeleteChildren(m_treeINFORMATION_REGISTERS);
	if (m_treeACCUMULATION_REGISTERS.IsOk()) m_roleCtrl->DeleteChildren(m_treeACCUMULATION_REGISTERS);

	//delete all items
	m_roleCtrl->DeleteAllItems();

	//Initialize tree
	InitRole();
}

void CRoleEditor::FillData()
{
	IMetaData* metaData = m_metaRole->GetMetaData();
	wxASSERT(metaData);
	IMetaObject *commonObject = commonMetaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	m_roleCtrl->SetItemText(m_treeMETADATA, commonObject->GetName());

	//****************************************************************
	//*                          CommonForms                         *
	//****************************************************************
	for (auto commonForm : commonMetaData->GetMetaObject(g_metaCommonFormCLSID)) {
		if (commonForm->IsDeleted())
			continue;
		AppendItem(m_treeFORMS, commonForm);
	}

	//****************************************************************
	//*                          Constants                           *
	//****************************************************************
	for (auto constant : commonMetaData->GetMetaObject(g_metaConstantCLSID)) {
		if (constant->IsDeleted())
			continue;
		AppendItem(m_treeCONSTANTS, constant);
	}

	//****************************************************************
	//*                        Catalogs                              *
	//****************************************************************
	for (auto catalog : commonMetaData->GetMetaObject(g_metaCatalogCLSID)) {
		if (catalog->IsDeleted())
			continue;
		AppendItem(m_treeCATALOGS, catalog);
	}

	//****************************************************************
	//*                        Documents                             *
	//****************************************************************
	for (auto document : commonMetaData->GetMetaObject(g_metaDocumentCLSID)) {
		if (document->IsDeleted())
			continue;
		AppendItem(m_treeDOCUMENTS, document);
	}

	//****************************************************************
	//*                          Data processor                      *
	//****************************************************************
	for (auto dataProcessor : commonMetaData->GetMetaObject(g_metaDataProcessorCLSID)) {
		if (dataProcessor->IsDeleted())
			continue;
		AppendItem(m_treeDATAPROCESSORS, dataProcessor);
	}

	//****************************************************************
	//*                          Report			                     *
	//****************************************************************
	for (auto report : commonMetaData->GetMetaObject(g_metaReportCLSID)) {
		if (report->IsDeleted())
			continue;
		AppendItem(m_treeREPORTS, report);
	}

	//****************************************************************
	//*                          Information register			     *
	//****************************************************************
	for (auto informationRegister : commonMetaData->GetMetaObject(g_metaInformationRegisterCLSID)) {
		if (informationRegister->IsDeleted())
			continue;
		AppendItem(m_treeINFORMATION_REGISTERS, informationRegister);
	}

	//****************************************************************
	//*                          Accumulation register			     *
	//****************************************************************
	for (auto accumulationRegister : commonMetaData->GetMetaObject(g_metaAccumulationRegisterCLSID)) {
		if (accumulationRegister->IsDeleted())
			continue;
		AppendItem(m_treeACCUMULATION_REGISTERS, accumulationRegister);
	}

	m_checkCtrl->Enable(m_metaRole->IsEnabled());
}
