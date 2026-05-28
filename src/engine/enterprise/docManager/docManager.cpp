////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : Enterprise-runtime doc manager — only registers the
//	              external-data-processor / external-report templates.
//	              Everything else lives in the collapsed ibDocManager base.
////////////////////////////////////////////////////////////////////////////

#include "docManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibDocManagerEnterprise, ibDocManager);

//files
#include "templates/docViewDataProcessorFile.h"
#include "templates/docViewDataReportFile.h"

ibDocManagerEnterprise::ibDocManagerEnterprise()
	: ibDocManager()
{
	AddDocTemplate(g_metaExternalDataProcessorCLSID, _("External data processor"), wxT("*.edp"), wxT("edp"), _("Data processor Doc"), _("Data processor View"), CLASSINFO(ibDataProcessorFileDocument), CLASSINFO(ibDataProcessorEditView), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);
	AddDocTemplate(g_metaExternalReportCLSID, _("External report"), wxT("*.erp"), wxT("erp"), _("Report Doc"), _("Report View"), CLASSINFO(ibReportFileDocument), CLASSINFO(ibReportEditView), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);
}
