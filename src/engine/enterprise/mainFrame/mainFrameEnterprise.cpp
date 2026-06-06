////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "docManager/docManager.h"

///////////////////////////////////////////////////////////////////

ibFrontendMainFrameEnterprise* ibFrontendMainFrameEnterprise::GetFrame() {
	ibFrontendMainFrame* instance = ibFrontendMainFrame::GetFrame();
	if (instance != nullptr) {
		ibFrontendMainFrameEnterprise* enterprise_instance =
			dynamic_cast<ibFrontendMainFrameEnterprise*>(instance);
		wxASSERT(enterprise_instance);
		return enterprise_instance;
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////

ibFrontendMainFrameEnterprise::ibFrontendMainFrameEnterprise(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) :
	ibFrontendMainFrame(title, pos, size),
	m_outputWindow(new ibOutputWindow(this, wxID_ANY))
{
	m_docManager = new ibDocManagerEnterprise;
}

ibFrontendMainFrameEnterprise::~ibFrontendMainFrameEnterprise()
{
	wxDELETE(m_docManager);
}

#include "backend/debugger/debugServer.h"
#include "frontend/win/dlgs/errorDialog.h"

#include "backend/appData.h"
#include "backend/session/sessionRegistry.h"

void ibFrontendMainFrameEnterprise::BackendError(const wxString& strFileName, const wxString& strDocPath, const long currLine, const wxString& strErrorMessage) const
{
	//open error dialog
	std::shared_ptr<ibDialogError> errDlg(new ibDialogError(mainFrame, wxID_ANY));
	
	//set message 
	errDlg->SetErrorMessage(strErrorMessage);

	//get error code
	const int retCode = errDlg->ShowModal();

	//send message to enterprise
	if (retCode == 1) {
		outputWindow->OutputError(strErrorMessage);
	}

	//send error to designer
	if (retCode == 2) {
		debugServer->SendErrorToClient(
			strFileName,
			strDocPath,
			currLine,
			strErrorMessage
		);
	}

	//close window — force-close every session the registry owns:
	// each session's m_forceExit flag interrupts any running script,
	// OnForceExit (overridden on ibGUISession) schedules wxTheApp::Exit
	// once, Remove submitted for each session row. GUI ends with the
	// app exiting through wx's normal teardown.
	if (retCode == 3) {
		if (auto* reg = ibApplicationData::GetSessionRegistry())
			reg->CloseAll(true);
	}
}

void ibFrontendMainFrameEnterprise::CreateGUI()
{
	CreateWideGui();
}

bool ibFrontendMainFrameEnterprise::Show(bool show)
{
	bool ret = ibFrontendMainFrame::Show(show);
	if (ret) {
		if (!outputWindow->IsEmpty()) {
			outputWindow->SetFocus();
		}
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/moduleManager/moduleManager.h"

bool ibFrontendMainFrameEnterprise::AllowRun() const
{
	// StartMainModule fires BeforeStart / OnStart on the session's
	// root. BeforeStart veto returns false → frame show blocked.
	if (ibSession* s = GetSession()) {
		if (auto* root = s->GetManagerModule())
			return root->StartMainModule();
	}
	return false;
}

bool ibFrontendMainFrameEnterprise::AllowClose() const
{
	// ExitMainModule fires BeforeExit / OnExit. BeforeExit veto blocks
	// close (user sees "cancelled by script" banner / modal).
	if (ibSession* s = GetSession()) {
		if (auto* root = s->GetManagerModule())
			return root->ExitMainModule();
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////