#include "docManagerDesigner.h"

#include "frmserver/schema/designer/configCompare.h"
#include "frmserver/schema/designer/database.h"
#include "frmserver/schema/designer/debugger.h"
#include "frmserver/schema/designer/metaData.h"
#include "frmserver/schema/designer/homePage.h"
#include "frmserver/schema/designer/objectText.h"
#include "frmserver/schema/designer/predefined.h"
#include "frmserver/schema/designer/properties.h"
#include "frmserver/schema/designer/types.h"
#include "frmserver/schema/designer/userList.h"

//files
#include "frmserver/docView/templates/designer/docViewDataProcessorEdit.h"
#include "frmserver/docView/templates/designer/docViewDataReportEdit.h"
#include "frmserver/docView/templates/designer/docViewMetaFile.h"

//objects
#include "frmserver/docView/templates/designer/docViewCommonAttributeEdit.h"
#include "frmserver/docView/templates/designer/docViewFormEdit.h"
#include "frmserver/docView/templates/designer/docViewFunctionalOptionEdit.h"
#include "frmserver/docView/templates/designer/docViewInterfaceEdit.h"
#include "frmserver/docView/templates/designer/docViewModuleEdit.h"
#include "frmserver/docView/templates/designer/docViewRoleEdit.h"
#include "frmserver/docView/templates/designer/docViewSpreadsheetEdit.h"

#include "core/fileKind.h"   // extensions live in one table, not at each call site

ibDocManagerDesigner::ibDocManagerDesigner()
	: ibDocManager()
{
	// The external data processor and report, as the desktop's designer edited them — made anew or opened from a client's
	// file (the session's temporary storage), kept open, their trees drawn for the client and saved back to a file.
	AddDocTemplate<ibDataProcessorEditDocument, ibDataProcessorTreeView>(g_metaExternalDataProcessorCLSID, _("External data processor"), ibFileMask(ibFileKind::Tool), ibFileExtension(ibFileKind::Tool), _("Data processor Doc"), _("Data processor View"), ibTEMPLATE_VISIBLE);
	AddDocTemplate<ibReportEditDocument, ibReportTreeView>(g_metaExternalReportCLSID, _("External report"), ibFileMask(ibFileKind::Report), ibFileExtension(ibFileKind::Report), _("Report Doc"), _("Report View"), ibTEMPLATE_VISIBLE);
	AddDocTemplate<ibMetadataFileDocument, ibMetadataTreeView>(g_metaCommonMetadataCLSID, _("Configuration"), ibFileMask(ibFileKind::Application), ibFileExtension(ibFileKind::Application), _("Configuration Doc"), _("Configuration View"), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);

	// A module's text — a common module, an object's and a manager's, as the desktop's designer registered them.
	AddDocTemplate<ibModuleTextDocument, ibModuleTextView>(g_metaCommonModuleCLSID);
	AddDocTemplate<ibModuleTextDocument, ibModuleTextView>(g_metaModuleCLSID);
	AddDocTemplate<ibModuleTextDocument, ibModuleTextView>(g_metaManagerCLSID);
	// …and a form — a common form and an object's: its module's text and the form itself, edited by the client.
	AddDocTemplate<ibFormDesignDocument, ibFormDesignView>(g_metaCommonFormCLSID);
	AddDocTemplate<ibFormDesignDocument, ibFormDesignView>(g_metaFormCLSID);
	// …and a template — a common template and an object's: its sheet, or a report's composer's composition panel over its
	// description, as the desktop's designer opened it (ibTemplateDocument makes its kind's view).
	AddDocTemplate<ibTemplateDocument, ibSpreadsheetTemplateView>(g_metaCommonTemplateCLSID);
	AddDocTemplate<ibTemplateDocument, ibSpreadsheetTemplateView>(g_metaTemplateCLSID);
	// …and the checkable trees over a metaobject — a section's objects; a common attribute opens on its composition and
	// a functional option on its members, the same gesture; a role on its rights.
	AddDocTemplate<ibInterfaceCompositionDocument, ibInterfaceCompositionView>(g_metaSectionCLSID);
	AddDocTemplate<ibCommonAttributeCompositionDocument, ibCommonAttributeCompositionView>(g_metaCommonAttributeCLSID);
	AddDocTemplate<ibFunctionalOptionMembersDocument, ibFunctionalOptionMembersView>(g_metaFunctionalOptionCLSID);
	AddDocTemplate<ibRoleRightsDocument, ibRoleRightsView>(g_metaRoleCLSID);

	// The configuration tree — its doors; the rows are the frame's (ibClientFrameDesigner).
	RegisterSchema(ibProtocol::ibSchema::ibKind::MetaData, std::make_unique<ibSchemaMetaData>());
	// …and its predefined values editor — what an object declares, and its edits.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Predefined, std::make_unique<ibSchemaPredefined>());
	// …and its texts — an object's help and technical information.
	RegisterSchema(ibProtocol::ibSchema::ibKind::ObjectText, std::make_unique<ibSchemaObjectText>());
	// …and the configuration's home page workspace.
	RegisterSchema(ibProtocol::ibSchema::ibKind::HomePage, std::make_unique<ibSchemaHomePage>());
	// The debugger's store — the configuration's modules and their breakpoints; the debugger is the client's.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Debugger, std::make_unique<ibSchemaDebugger>());
	// The inspector's edit — the object it shows, a form's control or a metaobject alike, its property given a value.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Properties, std::make_unique<ibSchemaProperties>());
	// The designer's Administration → Users — the base's users.
	RegisterSchema(ibProtocol::ibSchema::ibKind::UserList, std::make_unique<ibSchemaUserList>());
	// …and Restore, Dump and Clear database — the base as a whole.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Database, std::make_unique<ibSchemaDatabase>());
	RegisterSchema(ibProtocol::ibSchema::ibKind::ConfigCompare, std::make_unique<ibSchemaConfigCompare>());
	// The syntax helper's names that have no article.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Types, std::make_unique<ibSchemaTypes>());
}
