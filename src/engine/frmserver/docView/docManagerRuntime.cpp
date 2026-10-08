#include "docManagerRuntime.h"

#include "frmserver/schema/runtime/allFunctions.h"
#include "frmserver/schema/runtime/sections.h"

//files
#include "frmserver/docView/templates/docViewDataProcessorFile.h"
#include "frmserver/docView/templates/docViewDataReportFile.h"
#include "backend/fileKind.h"   // extensions live in one table, not at each call site

wxIMPLEMENT_DYNAMIC_CLASS(ibDocManagerRuntime, ibDocManager);

ibDocManagerRuntime::ibDocManagerRuntime()
	: ibDocManager()
{
	// The external data processor and report, as the desktop's enterprise opened them from a file — a client's file,
	// handed over through the session's temporary storage (ibMetaData*::LoadFromTempFile).
	AddDocTemplate(g_metaExternalDataProcessorCLSID, _("External data processor"), ibFileMask(ibFileKind::Tool), ibFileExtension(ibFileKind::Tool), _("Data processor Doc"), _("Data processor View"), CLASSINFO(ibDataProcessorFileDocument), CLASSINFO(ibDataProcessorEditView), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);
	AddDocTemplate(g_metaExternalReportCLSID, _("External report"), ibFileMask(ibFileKind::Report), ibFileExtension(ibFileKind::Report), _("Report Doc"), _("Report View"), CLASSINFO(ibReportFileDocument), CLASSINFO(ibReportEditView), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);

	// All functions — it opens what the runtime runs.
	RegisterSchema(ibProtocolSchema::AllFunctions, std::make_unique<ibSchemaAllFunctions>());
	// …and the section panel, from which the same runtime opens what a section offers.
	RegisterSchema(ibProtocolSchema::Sections, std::make_unique<ibSchemaSections>());
}
