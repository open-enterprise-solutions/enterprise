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
	if (IsSchemaAllowed(ibProtocolSchema::AllFunctions)) {
		ibClientMenu operations;
		operations.Append(ibProtocolSchema::AllFunctions, _("All operations..."));
		m_menu.AppendSubMenu(operations, _("Operations"));
	}

	if (IsSchemaAllowed(ibProtocolSchema::ActiveUser)) {
		ibClientMenu administration;
		administration.Append(ibProtocolSchema::ActiveUser, _("Active users"));
		m_menu.AppendSubMenu(administration, _("Administration"));
	}

	ibClientMenu help;
	help.Append(ibProtocolSchema::About, _("About"));
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
