#include "fileBase.h"

#include <memory>

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/appData.h"            // ibFileInstanceRequest, CreateAppDataEnv
#include "backend/appHost.h"            // ibApplicationInstanceScope
#include "backend/backend_exception.h"
#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the journal and the dumps of the process
#include "core/serialize/provider/jsonProvider.h"   // the request read, as the port's are

#include "protocol/protocol.h"          // ibProtocol::ibFileBase — the request's words

#include "frmserver/client/clientHost.h"

struct ibFileBase {
	ibApplicationInstance*        applicationInstance = nullptr;
	std::unique_ptr<ibClientHost> host;
};

namespace {

// THE PROCESS'S PART OF THE BACKEND, taken down with the last base — as the desktop's OnExit takes it down
// (appDataDestroy). This library is never unloaded, so what is left here would otherwise go at the process's end, after
// the statics it leans on (the codeRunner exit crash, 2026-10-07): a base that failed to open had made it all the same,
// its plugins loaded, and the client that went on its refusal fell at the exit (2026-10-08).
void ReleaseProcess()
{
	if (ibApplicationHost::IsEmpty())
		ibApplicationInstance::DestroyAppDataEnv();
}

} // namespace

ibFileBase* ibFileBaseOpen(const ibString& request, ibString& error)
{
	// THE PROCESS'S JOURNAL, AND ITS CRASH GUARD — the engine runs in this process now, and every application of the
	// tree installs both as its first act; a client that loads a file base is one too. Named by the process, as theirs.
	ibCrashGuard::Install(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetName());

	// Read as the port's requests are read — the text where it lies.
	ibDataNode where;
	bool read = false;
	try {
		read = ibJsonProvider().Read(request, where);
	}
	catch (const ibCoreException&) {
	}
	if (!read) {
		error = ibT("the request to open a file base is not a JSON object");
		return nullptr;
	}
	ibFileInstanceRequest opening;
	opening.m_name      = where.GetValue<ibString>(ibProtocol::ibFileBase::Name);
	opening.m_locale    = where.GetValue<ibString>(ibProtocol::ibFileBase::Locale);
	opening.m_directory = where.GetValue<ibString>(ibProtocol::ibFileBase::Directory);
	opening.m_server    = where.GetValue<ibString>(ibProtocol::ibFileBase::Server);
	opening.m_port      = where.GetValue<ibString>(ibProtocol::ibFileBase::Port);
	opening.m_user      = where.GetValue<ibString>(ibProtocol::ibFileBase::User);
	opening.m_password  = where.GetValue<ibString>(ibProtocol::ibFileBase::Password);
	opening.m_database  = where.GetValue<ibString>(ibProtocol::ibFileBase::Database);
	opening.m_create    = where.GetValue<bool>(ibProtocol::ibFileBase::Create);
	// THE DEBUG SERVER — as enterprise-thin.exe's `--debug`: it comes up with the base's configuration
	// (ibMetaDataConfiguration::OnInitialize) and listens from the start. Without it the client turns it on
	// later; a session stops for it either way (ibSession::Debug — made with the session).
	if (where.GetValue<bool>(ibProtocol::ibFileBase::Debug))
		opening.m_loadMetadataFlags = _app_start_create_debug_server_flag;

	// THE THREAD COMES BACK AS IT WAS. Opening leaves it working for the base — and the client's thread works for
	// none: its calls are served on the sessions' own workers (ibClientHost::Call), as the application server's are.
	const ibApplicationInstanceScope scope(nullptr);

	ibApplicationInstance* applicationInstance = nullptr;
	try {
		applicationInstance = ibApplicationInstance::CreateAppDataEnv(opening);
	}
	catch (const ibCoreException& err) {
		error = err.GetErrorDescription();
	}
	if (applicationInstance == nullptr) {
		// Why, as the backend said it — the chain the opening recorded, else what was thrown.
		ibString chain;
		for (const ibString& said : ibBackendException::DrainLastErrors())
			chain += (chain.IsEmpty() ? ibString() : ibString(ibT("\n"))) + said;
		if (!chain.IsEmpty())
			error = chain;
		if (error.IsEmpty())
			error = _("The infobase could not be opened, and the failure carried no description.");
		ReleaseProcess();
		return nullptr;
	}

	auto base = std::make_unique<ibFileBase>();
	base->applicationInstance = applicationInstance;
	base->host = std::make_unique<ibClientHost>(applicationInstance);
	return base.release();
}

bool ibFileBaseCall(ibFileBase* base, const ibMemoryBuffer& request, const void* connection,
	const ibString& address, ibMemoryBuffer& answer, ibString& error)
{
	if (base == nullptr || !base->host) {
		error = ibT("no base is open");
		return false;
	}
	// The port's door: the envelope read, the call made, the answer written — as a server's client is answered.
	answer = base->host->Call(request, connection, address);
	return true;
}

void ibFileBaseListen(ibFileBase* base, const void* connection, std::function<void(const ibMemoryBuffer& packet)> notified)
{
	if (base == nullptr || !base->host)
		return;
	base->host->SetNotifier(connection, std::move(notified));
}

void ibFileBaseDisconnect(ibFileBase* base, const void* connection)
{
	if (base == nullptr || !base->host)
		return;
	base->host->Disconnect(connection);
}

void ibFileBaseClose(ibFileBase* base)
{
	if (base == nullptr)
		return;
	base->host.reset();
	ibApplicationInstance::DestroyAppDataEnv(base->applicationInstance);
	delete base;

	ReleaseProcess();
}
