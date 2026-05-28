////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report manager 
////////////////////////////////////////////////////////////////////////////

#include "docManager.h"

//common templates 
#include "frontend/docView/templates/docViewSpreadsheet.h"

#include "templates/docViewModuleEditor.h"
#include "templates/docViewFormEditor.h"
#include "templates/docViewInterface.h"
#include "templates/docViewRole.h"
#include "templates/docViewConfigCompare.h"

//files
#include "templates/docViewdataProcessorFile.h"
#include "templates/docViewDataReportFile.h"
#include "templates/docViewMetaFile.h"

wxBEGIN_EVENT_TABLE(ibMetaDocManagerDesigner, ibMetaDocManager)
EVT_UPDATE_UI(wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE, ibMetaDocManagerDesigner::OnUpdateSaveMetadata)
EVT_UPDATE_UI(wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE, ibMetaDocManagerDesigner::OnUpdateSaveMetadata)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibMetaDocManagerDesigner, ibMetaDocManager);

ibMetaDocManagerDesigner::ibMetaDocManagerDesigner()
	: ibMetaDocManager()
{
	AddDocTemplate(g_metaExternalDataProcessorCLSID, _("External data processor"), wxT("*.edp"), wxT("edp"), _("Data processor Doc"), _("Data processor View"), CLASSINFO(ibDataProcessorFilibDocument), CLASSINFO(ibDataProcessorEditView), wxTEMPLATE_VISIBLE);
	AddDocTemplate(g_metaExternalReportCLSID, _("External report"), wxT("*.erp"), wxT("erp"), _("Report Doc"), _("Report View"), CLASSINFO(ibReportFilibDocument), CLASSINFO(ibReportEditView), wxTEMPLATE_VISIBLE);
	AddDocTemplate(g_metaCommonMetadataCLSID, _("Configuration"), wxT("*.mcf"), wxT("mcf"), _("Configuration Doc"), _("Configuration View"), CLASSINFO(ibMetadataFilibDocument), CLASSINFO(ibMetadataEditView), wxTEMPLATE_VISIBLE | wxTEMPLATE_ONLY_OPEN);

	//common objects 
	AddDocTemplate(g_metaCommonModuleCLSID, CLASSINFO(ibModuleEditDocument), CLASSINFO(ibModuleEditView));
	AddDocTemplate(g_metaCommonFormCLSID, CLASSINFO(ibFormEditDocument), CLASSINFO(ibFormEditView));
	AddDocTemplate(g_metaCommonTemplateCLSID, _("Spreadsheet document"), wxT("*.oxl"), wxT("oxl"), CLASSINFO(ibSpreadsheetEditDocument), CLASSINFO(ibSpreadsheetEditView));

	AddDocTemplate(g_metaInterfaceCLSID, CLASSINFO(ibInterfaceEditDocument), CLASSINFO(ibInterfaceEditView));
	AddDocTemplate(g_metaRoleCLSID, CLASSINFO(ibRoleEditDocument), CLASSINFO(ibRoleEditView));

	// Tools — invisible template, opened through CreateDocument<T>().
	AddDocTemplate(g_toolConfigCompareCLSID,
		CLASSINFO(ibConfigCompareDocument), CLASSINFO(ibConfigCompareView));

	//advanced object
	AddDocTemplate(g_metaModuleCLSID, CLASSINFO(ibModuleEditDocument), CLASSINFO(ibModuleEditView));
	AddDocTemplate(g_metaManagerCLSID, CLASSINFO(ibModuleEditDocument), CLASSINFO(ibModuleEditView));
	AddDocTemplate(g_metaFormCLSID, CLASSINFO(ibFormEditDocument), CLASSINFO(ibFormEditView));
	AddDocTemplate(g_metaTemplateCLSID, _("Spreadsheet document"), wxT("*.oxl"), wxT("oxl"), CLASSINFO(ibSpreadsheetEditDocument), CLASSINFO(ibSpreadsheetEditView));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaDocManagerDesigner::OnUpdateSaveMetadata(wxUpdateUIEvent& event)
{
	event.Enable(activeMetaData != nullptr && activeMetaData->IsModified());
}
