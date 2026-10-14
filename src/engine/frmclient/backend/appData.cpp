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

	// THE BASE THIS ONE OPENED, as the engine's built it from the connection it held — the fields, not this process's
	// command line: a base the person picked (the folder dialog) is on none. Quoted, for the same reason the binary is.
	if (!m_strAddress.IsEmpty()) {
		executeCmd += wxString::Format(wxT(" --address=\"%s\""), m_strAddress);
	}
	else if (m_strFile.IsEmpty()) {
		if (!m_strServer.IsEmpty())
			executeCmd += wxString::Format(wxT(" --server=%s"), m_strServer);
		if (!m_strPort.IsEmpty())
			executeCmd += wxString::Format(wxT(" --dbport=%s"), m_strPort);
		if (!m_strDatabase.IsEmpty())
			executeCmd += wxString::Format(wxT(" --db=%s"), m_strDatabase);
		if (!m_strUser.IsEmpty())
			executeCmd += wxString::Format(wxT(" --user=%s"), m_strUser);
		if (!m_strPassword.IsEmpty())
			executeCmd += wxString::Format(wxT(" --password=%s"), m_strPassword);
	}
	else {
		executeCmd += wxString::Format(wxT(" --file=\"%s\""), m_strFile);
	}

	if (searchDebug)
		executeCmd += wxT(" --debug");

	if (!m_strIBUser.IsEmpty())
		executeCmd += wxString::Format(wxT(" --ibuser=%s"), m_strIBUser);
	if (!m_strIBPassword.IsEmpty())
		executeCmd += wxString::Format(wxT(" --ibpwd=%s"), m_strIBPassword);
	if (!m_strLocale.IsEmpty())
		executeCmd += wxString::Format(wxT(" --locale=%s"), m_strLocale);

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
