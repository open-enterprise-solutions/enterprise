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
