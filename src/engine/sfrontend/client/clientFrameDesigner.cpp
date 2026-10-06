#include "clientFrameDesigner.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"

#include "sfrontend/docView/docManagerDesigner.h"

ibClientFrameDesigner::ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance)
{
	m_docManager = new ibDocManagerDesigner;
	InitializeDefaultMenu();
}

bool ibClientFrameDesigner::AllowRun()
{
	const ibMetaDataConfigurationBase* const metaData = ibApplicationInstance::GetActiveMetaData(GetSession()->GetApplicationInstance());
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibClientFrameDesigner::InitializeDefaultMenu()
{
	ibClientMenu file;
	file.Append(ibDocCommand::Close);
	file.Append(ibDocCommand::Save);
	m_menu.AppendSubMenu(file, _("File"));

	ibClientMenu edit;
	edit.Append(ibDocCommand::Undo);
	edit.Append(ibDocCommand::Redo);
	m_menu.AppendSubMenu(edit, _("Edit"));

	// The designer's items stand whatever the rights; the schema's own right switches its item off (as drawn).
	ibClientMenu administration;
	administration.Append(ibClientSchemaKind::ActiveUser, _("Active users"));
	m_menu.AppendSubMenu(administration, _("Administration"));
}
