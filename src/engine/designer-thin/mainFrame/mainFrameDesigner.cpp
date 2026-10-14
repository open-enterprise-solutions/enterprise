////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

ibFrontendMainFrameDesigner::ibFrontendMainFrameDesigner(std::unique_ptr<ibCommunicator> communicator,
	const wxString& user, const wxString& password,
	const wxString& title, const wxPoint& pos, const wxSize& size)
	: ibFrontendMainFrame(std::move(communicator), ibProtocolMode::Designer, user, password, title, pos, size)
{
	// Its templates are the server's — the designer's doc manager's (DrawTemplates).
	m_docManager = new ibFrontendDocManager;
}

#include "frmclient/backend/appData.h"
#include "frmclient/backend/mcp/mcpServer.h"
#include "frmclient/mainFrame/output/outputWindow.h"

bool ibFrontendMainFrameDesigner::Show(bool show)
{
	bool ret = ibFrontendMainFrame::Show(show);
	if (ret && show) {
		// ⭐ THE ASSISTANT COMES UP WITH THE DESIGNER, when it is switched on.
		//
		// The setting already said so: `m_enabled` means "assistant access is on", and pressing
		// Start afterwards was asking the same question twice — a switch that has to be re-thrown
		// at every launch is a switch that does not mean what it says.
		//
		// HERE, and not in the constructor: Start needs a SESSION, and there is none until the base
		// Show has logged in. Show returning true is exactly the statement that it has.
		//
		// ⚠ A REFUSAL IS NOT FATAL AND IS NOT SILENT. The port may be taken by a designer already
		// open on another base, which is an ordinary thing to happen and no reason to hold up the
		// window — so it is said in the messages and the designer carries on. Nothing here starts
		// a server the settings did not ask for: Start refuses on its own when the access is
		// switched off, and that refusal is not worth showing at every launch.
		if (ibMcpServer* server = ibApplicationInstance::GetMcpServer()) {
			if (server->GetSettings().m_enabled && !server->IsRunning()) {

				wxString refusal;
				if (!server->Start(ibSession::Current(), refusal) && !refusal.IsEmpty() && m_outputWindow != nullptr)
					m_outputWindow->Output(refusal, ibProtocolMessageLevel::Warning);
			}
		}
	}

	return ret;
}
