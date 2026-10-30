#include "appData.h"

#include <wx/utils.h>   // wxLaunchDefaultBrowser, wxYieldIfNeeded

#include "core/diagnostics/journal.h"
#include "core/fileSystem.h"   // the binary next to this one — RunApplication
#include "core/localization/dictionary.h"   // ibDictionary::GetProcessLanguage — the base's language, RunApplication
#include "core/diagnostics/crashGuard.h"   // CurrentPidPortable — whom a debug run goes with
#include "core/startProcess.h"   // ibStartWebServer

#include "frmclient/backend/mcp/mcpServer.h"
#include "frmclient/mainFrame/mainFrame.h"

bool ibApplicationData::DesignerMode() const
{
	const ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr && frame->GetMode() == ibProtocol::ibLogin::ibMode::Designer;
}

long ibApplicationData::RunApplication(const ibString& strAppName, bool searchDebug, bool useManifest) const
{
	// Resolve the binary next to this one, as the engine's (ibExecutablePath): a bare name is looked up on PATH and in the
	// working directory, and it is in neither; on macOS it is inside its bundle. Quoted because a resolved path can
	// contain a space and wxExecute splits on those.
	ibString executeCmd = ibT('"') + ibExecutablePath(strAppName) + ibT('"');

	// THE LOCALE IT RUNS IN — the one asked for (--locale), else the base's: the desktop's passed its host's
	// (ibApplicationHost::GetLocaleName) whether or not it was asked for one; here the language the login answered
	// (ibLogin::Locale), the process's since. Neither — none passed, the program settles its own.
	const ibString locale = !m_strLocale.IsEmpty() ? m_strLocale : ibDictionary::GetProcessLanguage();

	// THE WEB SERVER publishes this base — the application server's, or the file base with its debugger — and the
	// browser opens its page once the server says where it is; a login is the browser's. A base on a DBMS server is no
	// publication of it (webserver/webServer.h).
	if (useManifest) {
		if (!m_strAddress.IsEmpty()) {
			executeCmd += ibString::Format(ibT(" --server=\"%s\""), m_strAddress);
			if (!m_strTrusted.IsEmpty())
				executeCmd += ibString::Format(ibT(" --trusted=\"%s\""), m_strTrusted);
		}
		else if (!m_strFile.IsEmpty()) {
			executeCmd += ibString::Format(ibT(" --file=\"%s\""), m_strFile);
			if (searchDebug)
				executeCmd += ibString::Format(ibT(" --debug --owner=%u"), ibCrashGuard::CurrentPidPortable());   // a debug run goes with whoever debugs it
		}
		else {
			ibJournalWarning(ibT("process"), ibT("%s publishes a file base or a base of an application server, not one on a DBMS server"),
				strAppName);
			return 0;
		}
		if (!locale.IsEmpty())
			executeCmd += ibString::Format(ibT(" --locale=%s"), locale);

		ibString url;
		const long pid = ibStartWebServer(executeCmd, url, []() { wxYieldIfNeeded(); });
		if (!url.IsEmpty())
			wxLaunchDefaultBrowser(url);
		return pid;
	}

	// THE BASE THIS ONE OPENED, as the engine's built it from the connection it held — the fields, not this process's
	// command line: a base the person picked (the folder dialog) is on none. Quoted, for the same reason the binary is.
	if (!m_strAddress.IsEmpty()) {
		executeCmd += ibString::Format(ibT(" --address=\"%s\""), m_strAddress);
		if (!m_strTrusted.IsEmpty())
			executeCmd += ibString::Format(ibT(" --trusted=\"%s\""), m_strTrusted);
	}
	else if (m_strFile.IsEmpty()) {
		if (!m_strServer.IsEmpty())
			executeCmd += ibString::Format(ibT(" --server=%s"), m_strServer);
		if (!m_strPort.IsEmpty())
			executeCmd += ibString::Format(ibT(" --dbport=%s"), m_strPort);
		if (!m_strDatabase.IsEmpty())
			executeCmd += ibString::Format(ibT(" --db=%s"), m_strDatabase);
		if (!m_strUser.IsEmpty())
			executeCmd += ibString::Format(ibT(" --user=%s"), m_strUser);
		if (!m_strPassword.IsEmpty())
			executeCmd += ibString::Format(ibT(" --password=%s"), m_strPassword);
	}
	else {
		executeCmd += ibString::Format(ibT(" --file=\"%s\""), m_strFile);
	}

	if (searchDebug)
		executeCmd += ibT(" --debug");

	if (!m_strIBUser.IsEmpty())
		executeCmd += ibString::Format(ibT(" --ibuser=%s"), m_strIBUser);
	if (!m_strIBPassword.IsEmpty())
		executeCmd += ibString::Format(ibT(" --ibpwd=%s"), m_strIBPassword);
	if (!locale.IsEmpty())
		executeCmd += ibString::Format(ibT(" --locale=%s"), locale);

	return wxExecute(executeCmd);
}

namespace {

// THE PERSON'S ASSISTANT ACCESS, as its schema showed it last — a page open over it reads that, not the server again.
ibMcpServer s_mcpServer;

}

ibMcpServer* ibApplicationInstance::GetMcpServer()
{
	// The schema says whether the person may switch it here (Allowed): not on the application server, nor where the
	// mode has no such schema (the runtime) — refused, and said to nobody. Allowed already — what was received stands.
	if (!s_mcpServer.IsAllowed())
		s_mcpServer.Receive();
	return s_mcpServer.IsAllowed() ? &s_mcpServer : nullptr;
}

ibMcpServer* ibApplicationInstance::GetMcpServer(const ibProtocolNode& shown)
{
	s_mcpServer.Receive(shown);
	return s_mcpServer.IsAllowed() ? &s_mcpServer : nullptr;
}
