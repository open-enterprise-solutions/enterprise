#include "database.h"

#include "backend/appData.h"
#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the person asked and told
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/temp/tempStorage.h"
#include "frmserver/client/clientFrame.h"   // ibClientFrame::CloseAll — the open tabs, closed before a clear
#include "core/exception.h"   // ibCoreException — a restore refused on the way
#include "core/fileKind.h"
#include "core/serialize/dataBuilder.h"

namespace {

// A FILE OF THE PERSON'S, asked as File → Open asks it (ibClientFrame::OpenChosenFile) — the desktop's file dialog over
// the data files: picked and uploaded (`file` empty), or a file of the session's taken down where they chose. False —
// cancelled.
bool RequestFile(ibBackendDocFrame* frame, const ibString& caption, ibString& file)
{
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::File));
	request.SetValue(ibProtocol::ibRequest::ibFile::Caption, caption);
	if (!file.IsEmpty())
		request.SetValue(ibProtocol::ibRequest::ibFile::File, file);
	ibDataNode& item = request.AddChild(0, 0);
	item.SetValue(ibProtocol::ibRequest::ibFile::Title, ibString(_("Data files")));
	item.SetValue(ibProtocol::ibRequest::ibFile::Mask, ibFileMask(ibFileKind::Save));

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;
	if (!file.IsEmpty())
		return response.GetValue<bool>(ibProtocol::ibRequest::ibFile::Done);
	file = response.GetValue<ibString>(ibProtocol::ibRequest::ibFile::File);
	return !file.IsEmpty();
}

}

bool ibSchemaDatabase::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_DataAdministration();
}

void ibSchemaDatabase::Build(const ibSession& /*session*/, const ibDataNode& /*args*/,
	ibDataNode& /*result*/) const
{
}

std::function<void()> ibSchemaDatabase::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& /*args*/, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();
	if (applicationInstance == nullptr) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = ibT("the session has no base");
		return nullptr;
	}

	// ⚠ A RESTORE DOES NOT CLOSE THE TABS FIRST, as the desktop's did: it closes every session after, this one among them.
	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Load:
		return [applicationInstance]() {
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			ibString id;
			if (frame == nullptr || !RequestFile(frame, _("Open database file"), id))
				return;     // the user changed idea...

			ibString failure;
			try {
				const ibTempFile file(id);
				ibTempFileInputStream stream(file);
				if (file.IsOpened() && applicationInstance->LoadDatabase(stream)) {
					frame->ShowModalMessage(_("Loading of tasks completed successful. Restart the program!"),
						ibString(), wxOK | wxCENTRE);
					if (ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance))
						registry->CloseAll(true);
					return;
				}
				failure = _("Error when trying to load database from a file!") + ibT("\n\n") + BackendErrorChain();
			}
			catch (const ibCoreException& e) {
				failure = _("Loading the database failed.") + ibT("\n\n") + BackendErrorChain(e.GetErrorDescription());
			}
			catch (const std::exception& e) {
				failure = _("Loading the database failed with an internal error.") + ibT("\n\n")
					+ BackendErrorChain(ibString::FromUTF8(e.what()));
			}
			frame->ShowModalMessage(failure, _("Error"), wxOK | wxCENTRE | wxICON_ERROR);
		};
	case ibProtocol::ibSchema::ibCommand::Save: {
		// THE DUMP IS WRITTEN FIRST, into a file of the session's, and then taken down: the server cannot write where
		// the person chose. The desktop asked first — a dump cancelled here has been made for nothing.
		ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(applicationInstance);
		const ibString id = storage != nullptr
			? storage->Create(session.Identity().m_guid, ibT("database.") + ibFileExtension(ibFileKind::Save)) : ibString();
		if (id.IsEmpty()) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = ibT("no file to dump the base into");
			return nullptr;
		}
		return [applicationInstance, id]() {
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr)
				return;
			ibTempFile file;
			if (file.Create(id, true)) {
				ibTempFileOutputStream stream(file);
				if (applicationInstance->SaveDatabase(stream) && stream.Close()) {
					ibString taken = id;
					if (RequestFile(frame, _("Save database file"), taken))
						frame->ShowModalMessage(_("Data upload completed successfully!"), ibString(), wxOK | wxCENTRE);
					return;
				}
			}
			frame->ShowModalMessage(_("Error when trying to upload data to a file!") + ibT("\n\n") + BackendErrorChain(),
				_("Error"), wxOK | wxCENTRE | wxICON_ERROR);
		};
	}
	case ibProtocol::ibSchema::ibCommand::Clear:
		return [applicationInstance]() {
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr || frame->ShowModalMessage(_("Are you sure you want to clear the database?"),
				ibString(), wxYES_NO | wxCENTRE | wxICON_QUESTION) != wxYES)
				return;
			// …the open tabs closed first, as the desktop's Clear closed its windows: the frame's Close All, each tab by the
			// road its cross takes (a modified one asks) — one kept, nothing cleared.
			ibClientFrame* const client = ibClientFrame::GetFrame();
			if (client != nullptr && !client->CloseAll())
				return;
			if (applicationInstance->ClearDatabase())
				frame->ShowModalMessage(_("Database cleared successfully!"), ibString(), wxOK | wxCENTRE);
			else
				frame->ShowModalMessage(_("Error when trying to clear database!") + ibT("\n\n") + BackendErrorChain(),
					_("Error"), wxOK | wxCENTRE | wxICON_ERROR);
		};
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("the base has no command %d"), command);
	return nullptr;
}
