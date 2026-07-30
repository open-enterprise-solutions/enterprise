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

ibFrontendMainFrameEnterprise::ibFrontendMainFrameEnterprise(ibSessionHolder&& holder,
	const wxString& title,
	const wxPoint& pos,
	const wxSize& size) :
	ibFrontendMainFrame(std::move(holder), title, pos, size),
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

	//close window — force-close every session the registry owns: the
	// force flag interrupts any running script and each session closes
	// its own window without asking. The app then ends through wx's
	// normal teardown, and each window's holder release removes its row.
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

// The enterprise window is the one with a full runtime behind it, so
// both of its boundaries fire script events on the session's root.

#include "frontend/docView/templates/docViewHomePage.h"

void ibFrontendMainFrameEnterprise::CreateStartupPage()
{
	// The start page — a composite tab of the forms the configuration attached to it. Opens
	// nothing when the configuration attached none. Created AFTER BeforeStart / OnStart; its
	// tab is locked, so it takes the head of the notebook regardless of what the script
	// opened before it.
	ibHomePageDocument::ShowHomePage();
}

bool ibFrontendMainFrameEnterprise::AllowRun()
{
	// StartMainModule fires BeforeStart / OnStart. A BeforeStart veto —
	// or no runtime at all — would leave a half-alive client, so refuse.
	ibSession* s = GetSession();
	auto* root = s != nullptr ? s->GetManagerModule() : nullptr;
	return root != nullptr && root->StartMainModule();
}

bool ibFrontendMainFrameEnterprise::AllowClose()
{
	// Documents first (base), script second: a document that refuses
	// stops us before BeforeExit runs — no point asking the script about
	// an exit that is not going to happen.
	if (!ibFrontendMainFrame::AllowClose())
		return false;

	// ExitMainModule fires BeforeExit / OnExit. It runs HERE — window,
	// runtime and session all alive — so the script can still save,
	// message and query, and BeforeExit can still cancel.
	//
	// No runtime ⇒ allow. Refusing on a missing root (startup failed
	// before compile) would trap the user in an unclosable window.
	ibSession* const s = GetSession();
	auto* root = s != nullptr ? s->GetManagerModule() : nullptr;
	return root == nullptr || root->ExitMainModule();
}

///////////////////////////////////////////////////////////////////////////