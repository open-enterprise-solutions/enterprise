////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "docManager/docManager.h"

///////////////////////////////////////////////////////////////////

CDocEnterpriseMDIFrame* CDocEnterpriseMDIFrame::GetFrame() {
	CDocMDIFrame* instance = CDocMDIFrame::GetFrame();
	if (instance != nullptr) {
		CDocEnterpriseMDIFrame* enterprise_instance =
			dynamic_cast<CDocEnterpriseMDIFrame*>(instance);
		wxASSERT(enterprise_instance);
		return enterprise_instance;
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////

CDocEnterpriseMDIFrame::CDocEnterpriseMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) :
	CDocMDIFrame(title, pos, size)
{
	m_docManager = new CEnterpriseDocManager;
}

#include "backend/debugger/debugServer.h"
#include "frontend/win/dlgs/errorDialog.h"

#include "backend/appData.h"

void CDocEnterpriseMDIFrame::BackendError(const wxString& strFileName, const wxString& strDocPath, const long currLine, const wxString& strErrorMessage) const
{
	//open error dialog
	CDialogError *errDlg = new CDialogError(mainFrame, wxID_ANY);
	errDlg->SetErrorMessage(strErrorMessage);
	int retCode = errDlg->ShowModal();

	//send error to designer
	if (retCode > 1) {
		debugServer->SendErrorToClient(
			strFileName,
			strDocPath,
			currLine,
			strErrorMessage
		);
	}
	
	errDlg->Destroy();

	//close window
	if (retCode > 2) {
		appDataDestroy();
		std::exit(EXIT_SUCCESS);
	}

	outputWindow->OutputError(strErrorMessage);
}

void CDocEnterpriseMDIFrame::CreateGUI()
{
	CreateWideGui();
}

bool CDocEnterpriseMDIFrame::Show(bool show)
{
	bool ret = CDocMDIFrame::Show(show);
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

bool CDocEnterpriseMDIFrame::AllowRun() const
{
	return commonMetaData->StartMainModule();
}

bool CDocEnterpriseMDIFrame::AllowClose() const
{
	return commonMetaData->ExitMainModule();
}

///////////////////////////////////////////////////////////////////////////