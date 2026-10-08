#include "fileBase.h"

#include <memory>

#include <nlohmann/json.hpp>

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/appData.h"            // ibFileInstanceRequest, CreateAppDataEnv
#include "backend/appHost.h"            // ibApplicationInstanceScope
#include "backend/backend_exception.h"
#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the journal and the dumps of the process

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

ibFileBase* ibFileBaseOpen(const std::string& request, wxString& error)
{
	// THE PROCESS'S JOURNAL, AND ITS CRASH GUARD — the engine runs in this process now, and every application of the
	// tree installs both as its first act; a client that loads a file base is one too. Named by the process, as theirs.
	ibCrashGuard::Install(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetName());

	const nlohmann::json where = nlohmann::json::parse(request, nullptr, false);
	if (!where.is_object()) {
		error = wxT("the request to open a file base is not a JSON object");
		return nullptr;
	}
	const auto text = [&where](const char* name) {
		const auto found = where.find(name);
		return found != where.end() && found->is_string()
			? wxString::FromUTF8(found->get<std::string>()) : wxString();
	};
	ibFileInstanceRequest opening;
	opening.m_name      = text("Name");
	opening.m_locale    = text("Locale");
	opening.m_directory = text("Directory");
	opening.m_server    = text("Server");
	opening.m_port      = text("Port");
	opening.m_user      = text("User");
	opening.m_password  = text("Password");
	opening.m_database  = text("Database");

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
		wxString chain;
		for (const wxString& said : ibBackendException::DrainLastErrors())
			chain += (chain.IsEmpty() ? wxString() : wxString(wxT("\n"))) + said;
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

bool ibFileBaseCall(ibFileBase* base, const std::string& request, std::string& answer, wxString& error)
{
	if (base == nullptr || !base->host) {
		error = wxT("no base is open");
		return false;
	}
	// The port's door: the envelope read, the call made, the answer written — as a server's client is answered. The
	// base is the connection its clients log in through: what the base says, it says to them (ibFileBaseListen).
	answer = std::string(base->host->Call(wxString::FromUTF8(request.data(), request.size()), base).utf8_str());
	return true;
}

void ibFileBaseListen(ibFileBase* base, std::function<void(const std::string& text)> notified)
{
	if (base == nullptr || !base->host)
		return;
	if (!notified) {
		base->host->SetNotifier(base, nullptr);
		return;
	}
	base->host->SetNotifier(base, [notified = std::move(notified)](const wxString& text) {
		notified(std::string(text.utf8_str()));
	});
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
