#include "appData.h"

#include <wx/filename.h>

#include "core/programFolder.h"   // the binary next to this one — RunApplication

#include "frmclient/backend/mcp/mcpServer.h"
#include "frmclient/mainFrame/mainFrame.h"

bool ibApplicationData::DesignerMode() const
{
	const ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr && frame->GetMode() == ibProtocolMode::Designer;
}

long ibApplicationData::RunApplication(const wxString& strAppName, bool searchDebug, bool useManifest) const
{
	if (useManifest)
		return 0;

	// Resolve the binary next to this one, as the engine's: a bare name is looked up on PATH and in the working
	// directory, and it is in neither. Quoted because a resolved path can contain a space and wxExecute splits on those.
	wxFileName binary = wxFileName::DirName(ibProgramFolder());
	binary.SetFullName(strAppName);

	wxString executeCmd = binary.FileExists()
		? (wxT('"') + binary.GetFullPath() + wxT('"'))
		: strAppName;

	// THIS PROCESS'S COMMAND LINE — the engine's built it from the connection it held; the options are the same set
	// (--file, --address, --server…, --ibuser, --ibpwd, --locale), so the child opens the base this one did. Each quoted,
	// for the same reason the binary is; a `--debug` of this one's is not passed on, the child's is asked for below.
	for (int idx = 1; idx < wxTheApp->argc; idx++) {
		const wxString arg = wxTheApp->argv[idx];
		if (arg == wxT("--debug"))
			continue;
		executeCmd += wxT(" \"") + arg + wxT('"');
	}

	if (searchDebug)
		executeCmd += wxT(" --debug");

	return wxExecute(executeCmd);
}

ibMcpServer* ibApplicationInstance::GetMcpServer()
{
	// The schema says whether the person may switch it here (Allowed): not on the application server, nor where the
	// mode has no such schema (the runtime) — refused, and said to nobody.
	static ibMcpServer server;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return nullptr;

	ibProtocolNode params, shown;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::McpServer));
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	return frame->Call(ibProtocolMethod::Schema, params, shown, refusal, error) && shown.GetBool(ibProtocolName::Allowed)
		? &server : nullptr;
}
