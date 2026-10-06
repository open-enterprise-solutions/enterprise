#include "clientFrameRuntime.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"   // the configuration the start page is the metaobject's of
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"

#include "sfrontend/docView/docManagerRuntime.h"
#include "sfrontend/docView/templates/docViewHomePage.h"

ibClientFrameRuntime::ibClientFrameRuntime(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance)
{
	m_docManager = new ibDocManagerRuntime;
	InitializeDefaultMenu();
}

void ibClientFrameRuntime::InitializeDefaultMenu()
{
	ibClientMenu file;
	file.Append(ibDocCommand::Close);
	file.Append(ibDocCommand::Save);
	m_menu.AppendSubMenu(file, _("File"));

	ibClientMenu edit;
	edit.Append(ibDocCommand::Undo);
	edit.Append(ibDocCommand::Redo);
	m_menu.AppendSubMenu(edit, _("Edit"));

	// The runtime's menus a right closes are not there at all — the right is the schema's own.
	if (IsSchemaAllowed(ibClientSchemaKind::AllFunctions)) {
		ibClientMenu operations;
		operations.Append(ibClientSchemaKind::AllFunctions, _("All operations..."));
		m_menu.AppendSubMenu(operations, _("Operations"));
	}

	if (IsSchemaAllowed(ibClientSchemaKind::ActiveUser)) {
		ibClientMenu administration;
		administration.Append(ibClientSchemaKind::ActiveUser, _("Active users"));
		m_menu.AppendSubMenu(administration, _("Administration"));
	}
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
