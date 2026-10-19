#include "clientFrameRuntime.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"   // the configuration the start page is the metaobject's of
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"

#include "frmserver/docView/docManagerRuntime.h"
#include "frmserver/docView/templates/docViewHomePage.h"

ibClientFrameRuntime::ibClientFrameRuntime(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance)
{
	m_docManager = new ibDocManagerRuntime;
	InitializeDefaultMenu();
}

void ibClientFrameRuntime::InitializeDefaultMenu()
{
	// The runtime's menus a right closes are not there at all — the right is the schema's own.
	if (IsSchemaAllowed(ibProtocol::ibSchema::ibKind::AllFunctions)) {
		ibClientMenu operations;
		operations.Append(ibProtocol::ibSchema::ibKind::AllFunctions, _("All operations..."));
		m_menu.AppendSubMenu(operations, _("Operations"));
	}

	ibClientMenu tools;
	tools.Append(ibProtocol::ibSchema::ibKind::Debugging, _("Options..."));
	m_menu.AppendSubMenu(tools, _("Tools"));

	if (IsSchemaAllowed(ibProtocol::ibSchema::ibKind::ActiveUser)) {
		ibClientMenu administration;
		administration.Append(ibProtocol::ibSchema::ibKind::ActiveUser, _("Active users"));
		administration.AppendSeparator();
		administration.Append(ibProtocol::ibSchema::ibKind::AuditLog, _("Registration journal"));
		m_menu.AppendSubMenu(administration, _("Administration"));
	}

	ibClientMenu help;
	help.Append(ibProtocol::ibSchema::ibKind::About, _("About"));
	m_menu.AppendSubMenu(help, _("Help"));
}

bool ibClientFrameRuntime::AllowRun()
{
	ibValueModuleManagerRuntimeConfiguration* const manager = GetSession()->GetManagerModule();
	return manager != nullptr && manager->StartMainModule();
}

void ibClientFrameRuntime::CreateStartupPage()
{
	// The page is the configuration's: its metaobject is handed in from the configuration this client works in.
	if (const ibMetaDataConfigurationBase* const metaData = GetSession()->GetMetaData())
		ibHomePageDocument::ShowHomePage(metaData->GetCommonMetaObject());
}
