////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report manager 
////////////////////////////////////////////////////////////////////////////

#include "docManager.h"

//common templates 
#include "frontend/docView/templates/template.h"

#include "templates/moduleEditor.h"
#include "templates/formEditor.h"
#include "templates/interface.h"
#include "templates/role.h"

//files
#include "templates/dataProcessorFile.h"
#include "templates/dataReportFile.h"
#include "templates/metaFile.h"

wxBEGIN_EVENT_TABLE(CDesignerDocManager, CMetaDocManager)

EVT_UPDATE_UI(wxID_DESIGNER_CONFIGURATION_RETURN_DATABASE, CDesignerDocManager::OnUpdateSaveMetadata)
EVT_UPDATE_UI(wxID_DESIGNER_UPDATE_METADATA, CDesignerDocManager::OnUpdateSaveMetadata)

wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(CDesignerDocManager, CMetaDocManager);

CDesignerDocManager::CDesignerDocManager()
	: CMetaDocManager()
{
	AddDocTemplate("External data processor", "*.edp", "", "edp", "Data processor Doc", "Data processor View", CLASSINFO(CDataProcessorEditDocument), CLASSINFO(CDataProcessorEditView), wxTEMPLATE_VISIBLE);
	AddDocTemplate("External report", "*.erp", "", "erp", "Report Doc", "Report View", CLASSINFO(CReportEditDocument), CLASSINFO(CReportEditView), wxTEMPLATE_VISIBLE);
	AddDocTemplate("Configuration", "*.conf", "", "conf", "Configuration Doc", "Configuration View", CLASSINFO(CMetataEditDocument), CLASSINFO(CMetadataView), wxTEMPLATE_ONLY_OPEN);

	//common objects 
	AddDocTemplate(g_metaCommonModuleCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleEditView));
	AddDocTemplate(g_metaCommonFormCLSID, CLASSINFO(CFormEditDocument), CLASSINFO(CFormEditView));
	AddDocTemplate(g_metaCommonTemplateCLSID, CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView));

	AddDocTemplate(g_metaInterfaceCLSID, CLASSINFO(CInterfaceEditDocument), CLASSINFO(CInterfaceEditView));
	AddDocTemplate(g_metaRoleCLSID, CLASSINFO(CRoleEditDocument), CLASSINFO(CRoleEditView));

	//advanced object
	AddDocTemplate(g_metaModuleCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleEditView));
	AddDocTemplate(g_metaManagerCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleEditView));
	AddDocTemplate(g_metaFormCLSID, CLASSINFO(CFormEditDocument), CLASSINFO(CFormEditView));
	AddDocTemplate(g_metaTemplateCLSID, CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CDesignerDocManager::OnUpdateSaveMetadata(wxUpdateUIEvent& event)
{
	event.Enable(commonMetaData->IsModified());
}
