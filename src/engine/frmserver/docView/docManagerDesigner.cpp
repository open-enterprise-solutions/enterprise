#include "docManagerDesigner.h"

#include "frmserver/schema/designer/assistant.h"
#include "frmserver/schema/designer/database.h"
#include "frmserver/schema/designer/debugger.h"
#include "frmserver/schema/designer/mcpServer.h"
#include "frmserver/schema/designer/metaData.h"
#include "frmserver/schema/designer/homePage.h"
#include "frmserver/schema/designer/objectText.h"
#include "frmserver/schema/designer/predefined.h"
#include "frmserver/schema/designer/properties.h"
#include "frmserver/schema/designer/userList.h"

//files
#include "frmserver/docView/templates/designer/docViewDataProcessorEdit.h"
#include "frmserver/docView/templates/designer/docViewDataReportEdit.h"

//objects
#include "frmserver/docView/templates/designer/docViewCommonAttributeEdit.h"
#include "frmserver/docView/templates/designer/docViewFormEdit.h"
#include "frmserver/docView/templates/designer/docViewFunctionalOptionEdit.h"
#include "frmserver/docView/templates/designer/docViewInterfaceEdit.h"
#include "frmserver/docView/templates/designer/docViewModuleEdit.h"
#include "frmserver/docView/templates/designer/docViewRoleEdit.h"
#include "frmserver/docView/templates/designer/docViewSpreadsheetEdit.h"

//tools
#include "frmserver/docView/templates/designer/docViewAssistant.h"

#include "core/fileKind.h"   // extensions live in one table, not at each call site
#include "core/picturePredefined.h"   // the assistant's picture — its tool template's key

wxIMPLEMENT_DYNAMIC_CLASS(ibDocManagerDesigner, ibDocManager);

ibDocManagerDesigner::ibDocManagerDesigner()
	: ibDocManager()
{
	// The external data processor and report, as the desktop's designer edited them — made anew or opened from a client's
	// file (the session's temporary storage), kept open, their trees drawn for the client and saved back to a file.
	AddDocTemplate(g_metaExternalDataProcessorCLSID, _("External data processor"), ibFileMask(ibFileKind::Tool), ibFileExtension(ibFileKind::Tool), _("Data processor Doc"), _("Data processor View"), CLASSINFO(ibDataProcessorEditDocument), CLASSINFO(ibDataProcessorTreeView), ibTEMPLATE_VISIBLE);
	AddDocTemplate(g_metaExternalReportCLSID, _("External report"), ibFileMask(ibFileKind::Report), ibFileExtension(ibFileKind::Report), _("Report Doc"), _("Report View"), CLASSINFO(ibReportEditDocument), CLASSINFO(ibReportTreeView), ibTEMPLATE_VISIBLE);

	// A module's text — a common module, an object's and a manager's, as the desktop's designer registered them.
	AddDocTemplate(g_metaCommonModuleCLSID, CLASSINFO(ibModuleTextDocument), CLASSINFO(ibModuleTextView));
	AddDocTemplate(g_metaModuleCLSID, CLASSINFO(ibModuleTextDocument), CLASSINFO(ibModuleTextView));
	AddDocTemplate(g_metaManagerCLSID, CLASSINFO(ibModuleTextDocument), CLASSINFO(ibModuleTextView));
	// …and a form — a common form and an object's: its module's text and the form itself, edited by the client.
	AddDocTemplate(g_metaCommonFormCLSID, CLASSINFO(ibFormDesignDocument), CLASSINFO(ibFormDesignView));
	AddDocTemplate(g_metaFormCLSID, CLASSINFO(ibFormDesignDocument), CLASSINFO(ibFormDesignView));
	// …and a template's sheet — a common template and an object's.
	AddDocTemplate(g_metaCommonTemplateCLSID, CLASSINFO(ibSpreadsheetTemplateDocument), CLASSINFO(ibSpreadsheetTemplateView));
	AddDocTemplate(g_metaTemplateCLSID, CLASSINFO(ibSpreadsheetTemplateDocument), CLASSINFO(ibSpreadsheetTemplateView));
	// …and the checkable trees over a metaobject — a section's objects; a common attribute opens on its composition and
	// a functional option on its members, the same gesture; a role on its rights.
	AddDocTemplate(g_metaSectionCLSID, CLASSINFO(ibInterfaceCompositionDocument), CLASSINFO(ibInterfaceCompositionView));
	AddDocTemplate(g_metaCommonAttributeCLSID, CLASSINFO(ibCommonAttributeCompositionDocument), CLASSINFO(ibCommonAttributeCompositionView));
	AddDocTemplate(g_metaFunctionalOptionCLSID, CLASSINFO(ibFunctionalOptionMembersDocument), CLASSINFO(ibFunctionalOptionMembersView));
	AddDocTemplate(g_metaRoleCLSID, CLASSINFO(ibRoleRightsDocument), CLASSINFO(ibRoleRightsView));

	// The assistant window — no metaobject, the registration journal's shape: an invisible template reached through
	// CreateDocument<ibAssistantTranscriptDocument>() from its schema's Open.
	AddDocTemplate(g_picAssistantCLSID,
		_("Assistant"), wxEmptyString, wxEmptyString,
		_("Assistant Doc"), _("Assistant View"),
		CLASSINFO(ibAssistantTranscriptDocument), CLASSINFO(ibAssistantTranscriptView),
		ibTEMPLATE_INVISIBLE);

	// The configuration tree — its doors; the rows are the frame's (ibClientFrameDesigner).
	RegisterSchema(ibProtocolSchema::MetaData, std::make_unique<ibSchemaMetaData>());
	// …and its predefined values editor — what an object declares, and its edits.
	RegisterSchema(ibProtocolSchema::Predefined, std::make_unique<ibSchemaPredefined>());
	// …and its texts — an object's help and technical information.
	RegisterSchema(ibProtocolSchema::ObjectText, std::make_unique<ibSchemaObjectText>());
	// …and the configuration's home page workspace.
	RegisterSchema(ibProtocolSchema::HomePage, std::make_unique<ibSchemaHomePage>());
	// The debugger's store — the configuration's modules and their breakpoints; the debugger is the client's.
	RegisterSchema(ibProtocolSchema::Debugger, std::make_unique<ibSchemaDebugger>());
	// The inspector's edit — the object it shows, a form's control or a metaobject alike, its property given a value.
	RegisterSchema(ibProtocolSchema::Properties, std::make_unique<ibSchemaProperties>());
	// The designer's Administration → Users — the base's users.
	RegisterSchema(ibProtocolSchema::UserList, std::make_unique<ibSchemaUserList>());
	// …and Restore, Dump and Clear database — the base as a whole.
	RegisterSchema(ibProtocolSchema::Database, std::make_unique<ibSchemaDatabase>());
	// The designer's options — the person's assistant access (its MCP page).
	RegisterSchema(ibProtocolSchema::McpServer, std::make_unique<ibSchemaMcpServer>());
	// …and the window onto it — Administration → Assistant window.
	RegisterSchema(ibProtocolSchema::Assistant, std::make_unique<ibSchemaAssistant>());
}
