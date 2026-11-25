#include "docManagerStudio.h"

#include "frmserver/schema/studio/applicationCompare.h"
#include "frmserver/schema/studio/database.h"
#include "frmserver/schema/studio/debugger.h"
#include "frmserver/schema/studio/metaData.h"
#include "frmserver/schema/studio/homePage.h"
#include "frmserver/schema/studio/objectText.h"
#include "frmserver/schema/studio/predefined.h"
#include "frmserver/schema/studio/properties.h"
#include "frmserver/schema/studio/types.h"
#include "frmserver/schema/studio/userList.h"
#include "frmserver/schema/studio/webAddress.h"

//files
#include "frmserver/docView/templates/studio/docViewDataToolEdit.h"
#include "frmserver/docView/templates/studio/docViewDataReportEdit.h"
#include "frmserver/docView/templates/studio/docViewMetaFile.h"

//objects
#include "frmserver/docView/templates/studio/docViewSharedAttributeEdit.h"
#include "frmserver/docView/templates/studio/docViewFormEdit.h"
#include "frmserver/docView/templates/studio/docViewFeatureOptionEdit.h"
#include "frmserver/docView/templates/studio/docViewInterfaceEdit.h"
#include "frmserver/docView/templates/studio/docViewModuleEdit.h"
#include "frmserver/docView/templates/studio/docViewProfile.h"
#include "frmserver/docView/templates/studio/docViewRoleEdit.h"
#include "frmserver/docView/templates/studio/docViewSpreadsheetEdit.h"

#include "core/fileKind.h"   // extensions live in one table, not at each call site

ibDocManagerStudio::ibDocManagerStudio()
	: ibDocManager()
{
	// The external data tool and report, as the desktop's designer edited them — made anew or opened from a client's
	// file (the session's temporary storage), kept open, their trees drawn for the client and saved back to a file.
	AddDocTemplate<ibDataToolEditDocument, ibDataToolTreeView>(g_metaExternalDataToolCLSID, _("External data tool"), ibFileMask(ibFileKind::Tool), ibFileExtension(ibFileKind::Tool), _("Data tool Doc"), _("Data tool View"), ibTEMPLATE_VISIBLE);
	AddDocTemplate<ibReportEditDocument, ibReportTreeView>(g_metaExternalReportCLSID, _("External report"), ibFileMask(ibFileKind::Report), ibFileExtension(ibFileKind::Report), _("Report Doc"), _("Report View"), ibTEMPLATE_VISIBLE);
	AddDocTemplate<ibMetadataFileDocument, ibMetadataTreeView>(g_metaApplicationMetadataCLSID, _("Application"), ibFileMask(ibFileKind::Application), ibFileExtension(ibFileKind::Application), _("Application Doc"), _("Application View"), ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN);

	// A module's text — a shared module, an object's and a manager's, as the desktop's designer registered them.
	AddDocTemplate<ibModuleTextDocument, ibModuleTextView>(g_metaSharedModuleCLSID);
	AddDocTemplate<ibModuleTextDocument, ibModuleTextView>(g_metaModuleCLSID);
	AddDocTemplate<ibModuleTextDocument, ibModuleTextView>(g_metaManagerCLSID);
	// …and a form — a shared form and an object's: its module's text and the form itself, edited by the client.
	AddDocTemplate<ibFormDesignDocument, ibFormDesignView>(g_metaSharedFormCLSID);
	AddDocTemplate<ibFormDesignDocument, ibFormDesignView>(g_metaFormCLSID);
	// …and a template — a shared template and an object's: its sheet, or a report's composer's composition panel over its
	// description, as the desktop's designer opened it (ibTemplateDocument makes its kind's view).
	AddDocTemplate<ibTemplateDocument, ibSpreadsheetTemplateView>(g_metaSharedTemplateCLSID);
	AddDocTemplate<ibTemplateDocument, ibSpreadsheetTemplateView>(g_metaTemplateCLSID);
	// …and the checkable trees over a metaobject — a section's objects; a shared attribute opens on its composition and
	// a feature option on its members, the same gesture; a role on its rights.
	AddDocTemplate<ibInterfaceCompositionDocument, ibInterfaceCompositionView>(g_metaSectionCLSID);
	AddDocTemplate<ibSharedAttributeCompositionDocument, ibSharedAttributeCompositionView>(g_metaSharedAttributeCLSID);
	AddDocTemplate<ibFeatureOptionMembersDocument, ibFeatureOptionMembersView>(g_metaFeatureOptionCLSID);
	AddDocTemplate<ibRoleRightsDocument, ibRoleRightsView>(g_metaRoleCLSID);

	// What a measurement measured — no metaobject, the registration journal's shape: an invisible template its end makes
	// (ibClientDebugger::OnProfile).
	AddDocTemplate<ibProfileDocument, ibProfileView>(g_metaModuleCLSID,
		_("Performance measurement"), ibEmptyString, ibEmptyString,
		_("Profile Doc"), _("Profile View"),
		ibTEMPLATE_INVISIBLE);

	// The application tree — its doors; the rows are the frame's (ibClientFrameStudio).
	RegisterSchema(ibProtocol::ibSchema::ibKind::MetaData, std::make_unique<ibSchemaMetaData>());
	// …and its predefined values editor — what an object declares, and its edits.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Predefined, std::make_unique<ibSchemaPredefined>());
	// …and its texts — an object's help and technical information.
	RegisterSchema(ibProtocol::ibSchema::ibKind::ObjectText, std::make_unique<ibSchemaObjectText>());
	// …and the application's home page workspace.
	RegisterSchema(ibProtocol::ibSchema::ibKind::HomePage, std::make_unique<ibSchemaHomePage>());
	// The debugger's store — the application's modules and their breakpoints; the debugger is the client's.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Debugger, std::make_unique<ibSchemaDebugger>());
	// The inspector's edit — the object it shows, a form's control or a metaobject alike, its property given a value.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Properties, std::make_unique<ibSchemaProperties>());
	// The studio's Administration → Users — the base's users.
	RegisterSchema(ibProtocol::ibSchema::ibKind::UserList, std::make_unique<ibSchemaUserList>());
	// …and Web addresses — what the base answers on a web server, beside its clients.
	RegisterSchema(ibProtocol::ibSchema::ibKind::WebAddress, std::make_unique<ibSchemaWebAddress>());
	// …and Restore, Dump and Clear database — the base as a whole.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Database, std::make_unique<ibSchemaDatabase>());
	RegisterSchema(ibProtocol::ibSchema::ibKind::ApplicationCompare, std::make_unique<ibSchemaApplicationCompare>());
	// The syntax helper's names that have no article.
	RegisterSchema(ibProtocol::ibSchema::ibKind::Types, std::make_unique<ibSchemaTypes>());
}
