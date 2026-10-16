#include "assistant.h"

#include "backend/appData.h"
#include "backend/mcp/mcpServer.h"
#include "backend/session/session.h"
#include "core/picturePredefined.h"   // the assistant's own icon, from the picture registry

#include "frmserver/docView/docManager.h"
#include "frmserver/docView/templates/designer/docViewAssistant.h"

namespace {

// The base's assistant access — its owner's, asked instead of "the current one".
ibMcpServer* ServerOf(const ibSession& session)
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();
	return applicationInstance != nullptr ? ibApplicationInstance::GetMcpServer(applicationInstance) : nullptr;
}

}

bool ibSchemaAssistant::AccessRight(const ibSession& session) const
{
	const ibMcpServer* const server = ServerOf(session);
	return server != nullptr && server->IsRunning();
}

std::function<void()> ibSchemaAssistant::Command(const ibSession& session, s32 command,
	const ibDataNode& WXUNUSED(args), ibProtocolRefusal& refusal, wxString& error) const
{
	if (static_cast<ibProtocolSchemaCommand>(command) != ibProtocolSchemaCommand::Open) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("the assistant window has no command %d"), command);
		return nullptr;
	}

	ibMcpServer* const server = ServerOf(session);
	if (server == nullptr || !server->IsRunning()) {
		refusal = ibProtocolRefusal::NotNow;
		error = wxT("the base's assistant access is not running");
		return nullptr;
	}

	return [server]() {
		ibDocManager* const documentManager = ibDocManager::GetDocumentManager();
		if (documentManager == nullptr)
			return;

		// ⭐ ONE ASSISTANT WINDOW — there is one server and one conversation, so a second tab would show the same
		// exchange in two places. The document brings ITSELF forward (the desktop's OnMcpAssistant).
		for (ibDocument* open : documentManager->GetDocumentsVector()) {
			if (ibAssistantTranscriptDocument* already = wxDynamicCast(open, ibAssistantTranscriptDocument)) {
				already->Activate();
				return;
			}
		}

		ibAssistantTranscriptDocument* doc = documentManager->CreateDocument<ibAssistantTranscriptDocument>(server);
		if (doc == nullptr)
			return;

		doc->SetTitle(_("Assistant"));
		doc->SetIcon(ibPictureDescription(g_picAssistantCLSID));
		documentManager->AddDocument(doc);

		if (!doc->OnCreate(wxEmptyString, 0))
			doc->DeleteAllViews();
	};
}
