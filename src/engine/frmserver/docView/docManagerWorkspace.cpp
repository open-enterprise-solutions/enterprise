#include "docManagerWorkspace.h"

#include "frmserver/schema/workspace/allFunctions.h"
#include "frmserver/schema/workspace/debugging.h"
#include "frmserver/schema/workspace/fullTextSearch.h"
#include "frmserver/schema/workspace/sections.h"

//files
#include "frmserver/docView/templates/docViewDataToolFile.h"
#include "frmserver/docView/templates/docViewDataReportFile.h"
#include "core/fileKind.h"   // extensions live in one table, not at each call site

ibDocManagerWorkspace::ibDocManagerWorkspace()
	: ibDocManager()
{
	// The external data tool and report, as the desktop's enterprise opened them from a file — a client's file,
	// handed over through the session's temporary storage (ibMetaData*::LoadFromTempFile).
	AddDocTemplate<ibDataToolFileDocument, ibDataToolEditView>(g_metaExternalDataToolCLSID, _("External data tool"), ibFileMask(ibFileKind::Tool), ibFileExtension(ibFileKind::Tool), _("Data tool Doc"), _("Data tool View"), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);
	AddDocTemplate<ibReportFileDocument, ibReportEditView>(g_metaExternalReportCLSID, _("External report"), ibFileMask(ibFileKind::Report), ibFileExtension(ibFileKind::Report), _("Report Doc"), _("Report View"), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);

	// All functions — it opens what the runtime runs.
	RegisterSchema(ibProtocol::ibSchema::ibKind::AllFunctions, std::make_unique<ibSchemaAllFunctions>());
	// …and the section panel, from which the same runtime opens what a section offers.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Sections, std::make_unique<ibSchemaSections>());
	// …and debugging it, switched in its Options.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Debugging, std::make_unique<ibSchemaDebugging>());
	// …and the base's full-text search: its administration, and Find over it.
	RegisterSchema(ibProtocol::ibSchema::ibKind::FullTextSearch, std::make_unique<ibSchemaFullTextSearch>());
	RegisterSchema(ibProtocol::ibSchema::ibKind::Search, std::make_unique<ibSchemaSearch>());
}
