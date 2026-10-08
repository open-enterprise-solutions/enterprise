#include "clientFrameDesigner.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"

#include "frmserver/docView/docManagerDesigner.h"

ibClientFrameDesigner::ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance)
{
	m_docManager = new ibDocManagerDesigner;
	InitializeDefaultMenu();
}

bool ibClientFrameDesigner::AllowRun()
{
	const ibMetaDataConfigurationBase* const metaData = GetSession()->GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibClientFrameDesigner::InitializeDefaultMenu()
{
	// File and Edit are the client's doc manager's. The designer's items stand whatever the rights; the schema's own
	// right switches its item off (as drawn).
	ibClientMenu administration;
	administration.Append(ibProtocolSchema::ActiveUser, _("Active users"));
	m_menu.AppendSubMenu(administration, _("Administration"));
}
