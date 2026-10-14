#include "debugging.h"

#include "backend/appData.h"
#include "backend/debugger/debugServer.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

namespace {

// THE DEBUG SERVER THE PERSON MAY SWITCH — the process's (one per process: `debugServer`), as the desktop's dialog
// switched it, while it allows that (a start waiting for the designer does not). Not on the application server: its
// one debug server serves every base and every session it holds, so no session's person switches it. None: neither.
ibDebuggerServer* SwitchableServer(const ibSession& session)
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();
	if (applicationInstance == nullptr || applicationInstance->ServiceMode())
		return nullptr;

	ibDebuggerServer* const server = debugServer;
	return server != nullptr && server->AllowDebugging() ? server : nullptr;
}

}

void ibSchemaDebugging::Build(const ibSession& session, const ibDataNode& WXUNUSED(args), ibDataNode& result) const
{
	const ibDebuggerServer* const server = debugServer;
	result.SetValue(wxT("Debugging"), server != nullptr && server->EnableDebugging());
	result.SetValue(wxT("Allowed"), SwitchableServer(session) != nullptr);
}

std::function<void()> ibSchemaDebugging::Command(const ibSession& session, s32 command,
	const ibDataNode& WXUNUSED(args), ibProtocolRefusal& refusal, wxString& error) const
{
	const ibProtocolSchemaCommand what = static_cast<ibProtocolSchemaCommand>(command);
	if (what != ibProtocolSchemaCommand::Start && what != ibProtocolSchemaCommand::Stop) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("debugging has no command %d"), command);
		return nullptr;
	}

	ibDebuggerServer* const server = SwitchableServer(session);
	if (server == nullptr) {
		refusal = ibProtocolRefusal::NotNow;
		error = wxT("debugging cannot be switched here now");
		return nullptr;
	}

	return [server, what]() {
		if (what == ibProtocolSchemaCommand::Start)
			server->CreateServer();
		else
			server->ShutdownServer();
	};
}
