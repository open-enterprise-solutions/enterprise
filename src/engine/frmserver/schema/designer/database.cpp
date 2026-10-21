#include "database.h"

#include "backend/appData.h"
#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the person asked and told
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/temp/tempStorage.h"
#include "core/diagnostics/journal.h"    // a connection test is written down: the server connects where it is told
#include "core/exception.h"   // ibCoreException — a restore refused on the way
#include "core/fileKind.h"
#include "core/serialize/dataBuilder.h"

#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include <memory>   // the probe OWNS its driver, as the desktop's TestConnectionOnButtonClick
#endif

namespace {

// A FILE OF THE PERSON'S, asked as File → Open asks it (ibClientFrame::OpenChosenFile) — the desktop's file dialog over
// the data files: picked and uploaded (`file` empty), or a file of the session's taken down where they chose. False —
// cancelled.
bool RequestFile(ibBackendDocFrame* frame, const wxString& caption, wxString& file)
{
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<s32>(ibProtocol::ibRequest::ibKind::File));
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

void ibSchemaDatabase::Build(const ibSession& WXUNUSED(session), const ibDataNode& WXUNUSED(args),
	ibDataNode& WXUNUSED(result)) const
{
}

std::function<void()> ibSchemaDatabase::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const
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
	case ibProtocol::ibSchema::ibCommand::Test: {
		// THE DESKTOP'S PROBE, run where the base is: a driver opened with these settings, closed, and what happened
		// said — the driver's own message on a failure, read before Close clears it.
		const wxString server = args.GetValue<wxString>(ibProtocol::ibSchema::ibDatabase::Server);
		const wxString port = args.GetValue<wxString>(ibProtocol::ibSchema::ibDatabase::Port);
		const wxString user = args.GetValue<wxString>(ibProtocol::ibSchema::ibDatabase::User);
		const wxString password = args.GetValue<wxString>(ibProtocol::ibSchema::ibDatabase::Password);
		ibJournalInfo(wxT("designer"), wxT("[database] connection test to %s:%s as '%s' (session %s)"),
			server, port, user, session.Identity().m_guid.str());
		return [server, port, user, password]() {
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr)
				return;
#ifdef OES_USE_POSTGRESQL
			// The driver THROWS — when its client library cannot be loaded, and when it cannot connect
			// (ibDatabaseErrorReporter: what is said is the catch site's) — so it is caught here, where it is said, and
			// its message is the driver's.
			bool success = false;
			wxString failure;
			try {
				const std::unique_ptr<ibDatabaseLayerPostgres> postgresDatabaseLayer(new ibDatabaseLayerPostgres);
				try {
					success = postgresDatabaseLayer->Open(server, port, wxT(""), user, password);
				}
				catch (const ibCoreException& e) {
					failure = e.GetErrorDescription();
				}
				if (!success && failure.IsEmpty())
					failure = postgresDatabaseLayer->GetErrorMessage();
				postgresDatabaseLayer->Close();
			}
			catch (const ibCoreException& e) {
				success = false;
				failure = e.GetErrorDescription();
			}
			catch (const std::exception& e) {
				success = false;
				failure = wxString::FromUTF8(e.what());
			}
			ibJournalInfo(wxT("designer"), wxT("[database] connection test to %s:%s: %s"),
				server, port, success ? wxString(wxT("established")) : failure);
			if (success)
				frame->ShowModalMessage(_("Connection established"), _("Test connection"), wxOK | wxCENTRE | wxICON_INFORMATION);
			else
				frame->ShowModalMessage(failure.IsEmpty() ? _("Could not connect with these settings") : failure,
					_("Test connection"), wxOK | wxCENTRE | wxICON_ERROR);
#else
			frame->ShowModalMessage(_("PostgreSQL driver not available"), _("Test connection"), wxOK | wxCENTRE);
#endif
		};
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = wxString::Format(wxT("the base has no command %d"), command);
	return nullptr;
}
