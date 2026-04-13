#include "roleEditor.h"

#define commonName _("Common")
#define commonFormsName _("Common forms")
#define interfacesName _("Interfaces")
#define constantsName _("Constants")

#define catalogsName _("Catalogs")
#define documentsName _("Documents")
#define dataProcessorName _("Data processors")
#define reportsName _("Reports")
#define informationRegisterName _("Information Registers")
#define accumulationRegisterName _("Accumulation Registers")
#define chartsOfCharacteristicTypesName _("Charts of characteristic types")
#define chartsOfAccountsName _("Charts of accounts")
#define accountingRegistersName _("Accounting registers")

#define ICON_SIZE 16

ibRoleEditor::ibRoleEditor(wxWindow* parent,
	wxWindowID winid, ibValueMetaObject* metaObject) :
	wxSplitterWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE), m_metaRole(metaObject)
{
	m_roleCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxTR_TWIST_BUTTONS);
	m_roleCtrl->SetDoubleBuffered(true);
	m_roleCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ibRoleEditor::OnSelectedItem, this);

	//set image list
	m_roleCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_checkCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);
	m_checkCtrl->SetDoubleBuffered(true);
	m_checkCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibRoleEditor::OnCheckItem, this);

	InitRole();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* treeSizer = new wxBoxSizer(wxHORIZONTAL);
	treeSizer->Add(m_roleCtrl, 2, wxEXPAND);
	treeSizer->Add(m_checkCtrl, 1, wxEXPAND);
	mainSizer->Add(treeSizer, 1, wxEXPAND, 1);

	wxSplitterWindow::SetSashGravity(0.5);
	wxSplitterWindow::SplitVertically(m_roleCtrl, m_checkCtrl,
		250);

	m_roleCtrl->SelectItem(m_treeMETADATA);

	wxSplitterWindow::SetSizer(mainSizer);
	wxSplitterWindow::Layout();
}

ibRoleEditor::~ibRoleEditor() {
	m_roleCtrl->Unbind(wxEVT_TREE_SEL_CHANGED, &ibRoleEditor::OnSelectedItem, this);
	m_checkCtrl->Unbind(wxEVT_CHECKTREE_CHOICE, &ibRoleEditor::OnCheckItem, this);
}

void ibRoleEditor::OnCheckItem(wxTreeEvent& event)
{
	wxTreeItemRoleData* data = dynamic_cast<wxTreeItemRoleData*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	if (data != nullptr) {
		ibRole* role = data->GetRole();
		wxASSERT(role);
		ibAccessObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		metaObject->SetRight(role, m_metaRole->GetMetaID(), event.GetExtraLong());
	}

	event.Skip();
}

void ibRoleEditor::OnSelectedItem(wxTreeEvent& event) {
	wxTreeItemMetaData* data = dynamic_cast<wxTreeItemMetaData*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	m_checkCtrl->Freeze();
	m_checkCtrl->DeleteAllItems();
	if (data != nullptr) {
		ibAccessObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		wxTreeItemId root = m_checkCtrl->AddRoot(wxEmptyString);
		for (unsigned int idx = 0; idx < metaObject->GetRoleCount(); idx++) {
			ibRole* role = metaObject->GetRole(idx);
			wxASSERT(role);
			wxTreeItemId newItem = m_checkCtrl->AppendItem(root, role->GetLabel(), wxNOT_FOUND, wxNOT_FOUND, new wxTreeItemRoleData(role));
			m_checkCtrl->SetItemState(newItem, m_metaRole->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
			m_checkCtrl->Check(newItem, metaObject->AccessRight(role, m_metaRole->GetMetaID()));
		}
	}
	m_checkCtrl->Thaw();
	event.Skip();
}

void ibRoleEditor::AddInterfaceItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectInterface* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectInterface>();
	wxASSERT(metaObject);

	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {

		if (commonInterface->IsDeleted())
			continue;

		AddInterfaceItem(commonInterface,
			AppendItem(hParentID, commonInterface));
	}
}

#include "frontend/artProvider/artProvider.h"

void ibRoleEditor::InitRole()
{
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(g_metaCommonMetadataCLSID);
	wxASSERT(typeCtor);

	wxImageList* imageList = m_roleCtrl->GetImageList();
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
	m_treeMETADATA = m_roleCtrl->AddRoot(_("Configuration"), imageIndex, imageIndex, new wxTreeItemMetaData(activeMetaData->GetCommonMetaObject()));

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	const int imageCommonIndex = imageList->Add(wxArtProvider::GetIcon(wxART_COMMON_FOLDER, wxART_METATREE));
	m_treeCOMMON = m_roleCtrl->AppendItem(m_treeMETADATA, commonName, imageCommonIndex, imageCommonIndex);

	///////////////////////////////////////////////////////////////////////////////////////////////////////

	m_treeFORMS = AppendGroupItem(m_treeCOMMON, g_metaCommonFormCLSID, commonFormsName);
	m_treeINTERFACES = AppendGroupItem(m_treeCOMMON, g_metaInterfaceCLSID, interfacesName);

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
	m_treeCHARTS_OF_CHARACTERISTIC_TYPES = AppendGroupItem(m_treeMETADATA, g_metaChartOfCharacteristicTypesCLSID, chartsOfCharacteristicTypesName);
	m_treeCHARTS_OF_ACCOUNTS = AppendGroupItem(m_treeMETADATA, g_metaChartOfAccountsCLSID, chartsOfAccountsName);
	m_treeACCOUNTING_REGISTERS = AppendGroupItem(m_treeMETADATA, g_metaAccountingRegisterCLSID, accountingRegistersName);

	//Set item bold and name
	m_roleCtrl->SetItemText(m_treeMETADATA, _("Configuration"));
	m_roleCtrl->SetItemBold(m_treeMETADATA);

	m_roleCtrl->ExpandAll();
}

void ibRoleEditor::ClearRole() {

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	if (m_treeFORMS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeFORMS);

	if (m_treeINTERFACES.IsOk())
		m_roleCtrl->DeleteChildren(m_treeINTERFACES);

	if (m_treeCONSTANTS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeCONSTANTS);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	if (m_treeCATALOGS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeCATALOGS);
	if (m_treeDOCUMENTS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeDOCUMENTS);

	if (m_treeDATAPROCESSORS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeDATAPROCESSORS);
	if (m_treeREPORTS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeREPORTS);
	if (m_treeINFORMATION_REGISTERS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeINFORMATION_REGISTERS);
	if (m_treeACCUMULATION_REGISTERS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeACCUMULATION_REGISTERS);
	if (m_treeCHARTS_OF_CHARACTERISTIC_TYPES.IsOk())
		m_roleCtrl->DeleteChildren(m_treeCHARTS_OF_CHARACTERISTIC_TYPES);
	if (m_treeCHARTS_OF_ACCOUNTS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeCHARTS_OF_ACCOUNTS);
	if (m_treeACCOUNTING_REGISTERS.IsOk())
		m_roleCtrl->DeleteChildren(m_treeACCOUNTING_REGISTERS);

	//delete all items
	m_roleCtrl->DeleteAllItems();

	//Initialize tree
	InitRole();
}

void ibRoleEditor::FillData()
{
	const ibMetaData* metaData = m_metaRole->GetMetaData();
	wxASSERT(metaData);
	const ibValueMetaObject* commonObject = metaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	m_roleCtrl->SetItemText(m_treeMETADATA, commonObject->GetName());

	//****************************************************************
	//*                          CommonForms                         *
	//****************************************************************
	for (auto commonForm : metaData->GetAnyArrayObject(g_metaCommonFormCLSID)) {
		if (commonForm->IsDeleted())
			continue;
		AppendItem(m_treeFORMS, commonForm);
	}

	//****************************************************************
	//*                          Interfaces							 *
	//****************************************************************
	for (auto commonInterface : metaData->GetAnyArrayObject(g_metaInterfaceCLSID)) {
		if (commonInterface->IsDeleted())
			continue;	
		AddInterfaceItem(commonInterface,
			AppendItem(m_treeINTERFACES, commonInterface));
	}

	//****************************************************************
	//*                          Constants                           *
	//****************************************************************
	for (auto constant : metaData->GetAnyArrayObject(g_metaConstantCLSID)) {
		if (constant->IsDeleted())
			continue;
		AppendItem(m_treeCONSTANTS, constant);
	}

	//****************************************************************
	//*                        Catalogs                              *
	//****************************************************************
	for (auto catalog : metaData->GetAnyArrayObject(g_metaCatalogCLSID)) {
		if (catalog->IsDeleted())
			continue;
		AppendItem(m_treeCATALOGS, catalog);
	}

	//****************************************************************
	//*                        Documents                             *
	//****************************************************************
	for (auto document : metaData->GetAnyArrayObject(g_metaDocumentCLSID)) {
		if (document->IsDeleted())
			continue;
		AppendItem(m_treeDOCUMENTS, document);
	}

	//****************************************************************
	//*                          Data processor                      *
	//****************************************************************
	for (auto dataProcessor : metaData->GetAnyArrayObject(g_metaDataProcessorCLSID)) {
		if (dataProcessor->IsDeleted())
			continue;
		AppendItem(m_treeDATAPROCESSORS, dataProcessor);
	}

	//****************************************************************
	//*                          Report			                     *
	//****************************************************************
	for (auto report : metaData->GetAnyArrayObject(g_metaReportCLSID)) {
		if (report->IsDeleted())
			continue;
		AppendItem(m_treeREPORTS, report);
	}

	//****************************************************************
	//*                          Information register			     *
	//****************************************************************
	for (auto informationRegister : metaData->GetAnyArrayObject(g_metaInformationRegisterCLSID)) {
		if (informationRegister->IsDeleted())
			continue;
		AppendItem(m_treeINFORMATION_REGISTERS, informationRegister);
	}

	//****************************************************************
	//*                          Accumulation register			     *
	//****************************************************************
	for (auto accumulationRegister : metaData->GetAnyArrayObject(g_metaAccumulationRegisterCLSID)) {
		if (accumulationRegister->IsDeleted())
			continue;
		AppendItem(m_treeACCUMULATION_REGISTERS, accumulationRegister);
	}

	//****************************************************************
	//*                          Charts of characteristic types      *
	//****************************************************************
	for (auto chartOfCharacteristicTypes : metaData->GetAnyArrayObject(g_metaChartOfCharacteristicTypesCLSID)) {
		if (chartOfCharacteristicTypes->IsDeleted())
			continue;
		AppendItem(m_treeCHARTS_OF_CHARACTERISTIC_TYPES, chartOfCharacteristicTypes);
	}

	//****************************************************************
	//*                          Charts of accounts                  *
	//****************************************************************
	for (auto chartOfAccounts : metaData->GetAnyArrayObject(g_metaChartOfAccountsCLSID)) {
		if (chartOfAccounts->IsDeleted())
			continue;
		AppendItem(m_treeCHARTS_OF_ACCOUNTS, chartOfAccounts);
	}

	//****************************************************************
	//*                          Accounting register                 *
	//****************************************************************
	for (auto accountingRegister : metaData->GetAnyArrayObject(g_metaAccountingRegisterCLSID)) {
		if (accountingRegister->IsDeleted())
			continue;
		AppendItem(m_treeACCOUNTING_REGISTERS, accountingRegister);
	}

	m_checkCtrl->Enable(m_metaRole->IsEnabled());
}
