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

void ibSchemaDebugging::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibDebuggerServer* const server = debugServer;
	result.SetValue(ibProtocol::ibSchema::ibDebugging::Debugging, server != nullptr && server->EnableDebugging());
	result.SetValue(ibProtocol::ibSchema::ibDebugging::Allowed, SwitchableServer(session) != nullptr);
}

std::function<void()> ibSchemaDebugging::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& /*args*/, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	const ibProtocol::ibSchema::ibCommand what = static_cast<ibProtocol::ibSchema::ibCommand>(command);
	if (what != ibProtocol::ibSchema::ibCommand::Start && what != ibProtocol::ibSchema::ibCommand::Stop) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxString::Format(wxT("debugging has no command %d"), command);
		return nullptr;
	}

	ibDebuggerServer* const server = SwitchableServer(session);
	if (server == nullptr) {
		refusal = ibProtocol::ibRefusal::NotNow;
		error = wxT("debugging cannot be switched here now");
		return nullptr;
	}

	return [server, what]() {
		if (what == ibProtocol::ibSchema::ibCommand::Start)
			server->CreateServer();
		else
			server->ShutdownServer();
	};
}
