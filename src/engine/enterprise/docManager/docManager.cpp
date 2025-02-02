////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report manager 
////////////////////////////////////////////////////////////////////////////

#include "docManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(CEnterpriseDocManager, CMetaDocManager);

//files
#include "templates/dataProcessorFile.h"
#include "templates/dataReportFile.h"

CEnterpriseDocManager::CEnterpriseDocManager()
	: CMetaDocManager()
{
	AddDocTemplate("External data processor", "*.edp", "", "edp", "Data processor Doc", "Data processor View", CLASSINFO(CDataProcessorDocument), CLASSINFO(CDataProcessorView), wxTEMPLATE_ONLY_OPEN);
	AddDocTemplate("External report", "*.erp", "", "erp", "Report Doc", "Report View", CLASSINFO(CReportDocument), CLASSINFO(CReportView), wxTEMPLATE_ONLY_OPEN);
}
