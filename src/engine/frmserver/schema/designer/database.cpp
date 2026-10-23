#include "database.h"

#include "backend/appData.h"
#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the person asked and told
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/temp/tempStorage.h"
#include "core/exception.h"   // ibCoreException — a restore refused on the way
#include "core/fileKind.h"
#include "core/serialize/dataBuilder.h"

namespace {

// A FILE OF THE PERSON'S, asked as File → Open asks it (ibClientFrame::OpenChosenFile) — the desktop's file dialog over
// the data files: picked and uploaded (`file` empty), or a file of the session's taken down where they chose. False —
// cancelled.
bool RequestFile(ibBackendDocFrame* frame, const wxString& caption, wxString& file)
{
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::File));
	request.SetValue(ibProtocol::ibRequest::ibFile::Caption, caption);
	if (!file.IsEmpty())
		request.SetValue(ibProtocol::ibRequest::ibFile::File, file);
	ibDataNode& item = request.AddChild(0, 0);
	item.SetValue(ibProtocol::ibRequest::ibFile::Title, _("Data files"));
	item.SetValue(ibProtocol::ibRequest::ibFile::Mask, ibFileMask(ibFileKind::Save));

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;
	if (!file.IsEmpty())
		return response.GetValue<bool>(ibProtocol::ibRequest::ibFile::Done);
	file = response.GetValue<wxString>(ibProtocol::ibRequest::ibFile::File);
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
	const ibDataNode& /*args*/, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();
	if (applicationInstance == nullptr) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("the session has no base");
		return nullptr;
	}

	// ⚠ THE TABS ARE NOT CLOSED FIRST, as the desktop's were: a restore closes every session after, this one among them,
	// and a clear leaves the configuration the tabs edit as it was.
	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Load:
		return [applicationInstance]() {
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			wxString id;
			if (frame == nullptr || !RequestFile(frame, _("Open database file"), id))
				return;     // the user changed idea...

			wxString failure;
			try {
				const ibTempFile file(id);
				ibTempFileInputStream stream(file);
				if (file.IsOpened() && applicationInstance->LoadDatabase(stream)) {
					frame->ShowModalMessage(_("Loading of tasks completed successful. Restart the program!"),
						wxString(), wxOK | wxCENTRE);
					if (ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance))
						registry->CloseAll(true);
					return;
				}
				failure = _("Error when trying to load database from a file!") + wxT("\n\n") + BackendErrorChain();
			}
			catch (const ibCoreException& e) {
				failure = _("Loading the database failed.") + wxT("\n\n") + BackendErrorChain(e.GetErrorDescription());
			}
			catch (const std::exception& e) {
				failure = _("Loading the database failed with an internal error.") + wxT("\n\n")
					+ BackendErrorChain(wxString::FromUTF8(e.what()));
			}
			frame->ShowModalMessage(failure, wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR);
		};
	case ibProtocol::ibSchema::ibCommand::Save: {
		// THE DUMP IS WRITTEN FIRST, into a file of the session's, and then taken down: the server cannot write where
		// the person chose. The desktop asked first — a dump cancelled here has been made for nothing.
		ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(applicationInstance);
		const wxString id = storage != nullptr
			? storage->Create(session.Identity().m_guid, wxT("database.") + ibFileExtension(ibFileKind::Save)) : wxString();
		if (id.IsEmpty()) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = wxT("no file to dump the base into");
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
					wxString taken = id;
					if (RequestFile(frame, _("Save database file"), taken))
						frame->ShowModalMessage(_("Data upload completed successfully!"), wxString(), wxOK | wxCENTRE);
					return;
				}
			}
			frame->ShowModalMessage(_("Error when trying to upload data to a file!") + wxT("\n\n") + BackendErrorChain(),
				wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR);
		};
	}
	case ibProtocol::ibSchema::ibCommand::Clear:
		return [applicationInstance]() {
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr || frame->ShowModalMessage(_("Are you sure you want to clear the database?"),
				wxString(), wxYES_NO | wxCENTRE | wxICON_QUESTION) != wxYES)
				return;
			if (applicationInstance->ClearDatabase())
				frame->ShowModalMessage(_("Database cleared successfully!"), wxString(), wxOK | wxCENTRE);
			else
				frame->ShowModalMessage(_("Error when trying to clear database!") + wxT("\n\n") + BackendErrorChain(),
					wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR);
		};
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = wxString::Format(wxT("the base has no command %d"), command);
	return nullptr;
}
