#include "clientFrameDesigner.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"

#include "frmserver/docView/docManagerDesigner.h"

ibClientFrameDesigner::ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance), m_metaTree(this)
{
	m_docManager = new ibDocManagerDesigner;
	InitializeDefaultMenu();
}

bool ibClientFrameDesigner::AllowRun()
{
	ibMetaDataConfigurationBase* const metaData = GetSession()->GetMetaData();
	if (metaData == nullptr || !metaData->AccessRight_Administration())
		return false;
	return m_metaTree.Load(std::static_pointer_cast<ibMetaDataConfigurationBase>(metaData->shared_from_this()));
}

void ibClientFrameDesigner::OnDraw(ibDataNode& state)
{
	m_metaTree.OnDraw(state.Child(wxT("MetaData")));
	m_propertyInspector.OnDraw(state.Child(wxT("Properties")));
}

void ibClientFrameDesigner::BackendError(const wxString& WXUNUSED(strFileName),
	const wxString& WXUNUSED(strDocPath), const long WXUNUSED(line),
	const wxString& strErrorMessage)
{
	// ⚠ Without its place yet: the desktop's line opened the module at the error (ibOutputWindow), a message here is its
	// text alone.
	Message(strErrorMessage, ibStatusMessage::ibStatusMessage_Error);
}

void ibClientFrameDesigner::InitializeDefaultMenu()
{
	// File and Edit are the client's doc manager's. The designer's items stand whatever the rights; the schema's own
	// right switches its item off (as drawn).
	ibClientMenu administration;
	administration.Append(ibProtocolSchema::UserList, _("Users"));
	administration.Append(ibProtocolSchema::ActiveUser, _("Active users"));
	administration.AppendSeparator();
	administration.Append(ibProtocolSchema::AuditLog, _("Registration journal"));
	m_menu.AppendSubMenu(administration, _("Administration"));

	ibClientMenu tools;
	tools.Append(ibProtocolSchema::McpServer, _("Options..."));
	m_menu.AppendSubMenu(tools, _("Tools"));
}
