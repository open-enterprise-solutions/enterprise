#include "auditLog.h"

#include "backend/backend_picture.h"
#include "backend/metadataConfiguration.h"
#include "backend/picturePredefined.h"
#include "backend/session/session.h"

#include "frmserver/docView/docManager.h"
#include "frmserver/docView/templates/docViewAuditLog.h"

bool ibSchemaAuditLog::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_ActiveUsers();
}

std::function<void()> ibSchemaAuditLog::Command(const ibSession& session, s32 command,
	const ibDataNode& WXUNUSED(args), ibProtocolRefusal& refusal, wxString& error) const
{
	if (static_cast<ibProtocolSchemaCommand>(command) != ibProtocolSchemaCommand::Open) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("the registration journal has no command %d"), command);
		return nullptr;
	}

	const ibMetaData* const metaData = session.GetMetaData();
	return [metaData]() {
		// Open the journal as a tab via the docview system — the desktop's OnAuditLog. The template is registered
		// in the ibDocManager ctor — invisible to File → New, reachable only through CreateDocument<T>() here.
		ibDocManager* const documentManager = ibDocManager::GetDocumentManager();
		if (documentManager != nullptr) {
			ibAuditLogDocument* doc = documentManager->CreateDocument<ibAuditLogDocument>(metaData);
			if (doc != nullptr) {
				doc->SetTitle(_("Registration journal"));
				doc->SetIcon(ibBackendPicture::GetServerPicture(g_picUserActiveCLSID));
				documentManager->AddDocument(doc);
				if (!doc->OnCreate(wxEmptyString, 0))
					doc->DeleteAllViews();
			}
		}
	};
}
