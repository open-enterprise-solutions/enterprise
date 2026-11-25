////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameStudio.h"

#include "deskgui/artProvider.h"   // the debugger's pictures, Update database's
#include "appstudio/backend/debugger/debugClient.h"
#include "appstudio/mainFrame/local/localWindow.h"
#include "appstudio/mainFrame/stack/stackWindow.h"
#include "appstudio/mainFrame/watch/watchWindow.h"
#include "protocol/protocolStudio.h"   // ibProtocol::ibRequest::ibApplyChange, ibProtocol::ibSchema::ibMetaData
#include "frmclient/backend/metaData.h"   // the application a metaobject opens in - its tab (OpenForm)
#include "deskgui/univ/messageDialog.h"

ibFrontendMainFrameStudio* ibFrontendMainFrameStudio::GetFrame() {
	ibFrontendMainFrame* instance = ibFrontendMainFrame::GetFrame();
	if (instance != nullptr) {
		ibFrontendMainFrameStudio* studio_instance =
			dynamic_cast<ibFrontendMainFrameStudio*>(instance);
		wxASSERT(studio_instance);
		return studio_instance;
	}
	return nullptr;
}

ibFrontendMainFrameStudio::ibFrontendMainFrameStudio(std::unique_ptr<ibCommunicator> communicator,
	const wxString& user, const wxString& password,
	const wxString& title, const wxPoint& pos, const wxSize& size)
	: ibFrontendMainFrame(std::move(communicator), ibProtocol::ibLogin::ibMode::Studio, user, password, title, pos, size),
	m_stackWindow(new ibStackWindow(this, wxID_ANY)),
	m_watchWindow(new ibWatchWindow(this, wxID_ANY)),
	m_localWindow(new ibLocalWindow(this, wxID_ANY))
{
}

ibFrontendMainFrameStudio::~ibFrontendMainFrameStudio()
{
	// The debugger first — its sockets let go and its bridges with it, while the panes they write into still stand.
	m_debugClient.reset();
}

void ibFrontendMainFrameStudio::CreateGUI()
{
	ibFrontendMainFrame::CreateGUI();

	// The desktop designer's last button on the main toolbar — Application's Update database, the same command.
	ibAuiToolBar* const mainFrameToolbar = GetMainFrameToolbar();
	mainFrameToolbar->AddSeparator();
	mainFrameToolbar->AddTool(wxID_STUDIO_APPLICATION_UPDATE_DATABASE, _("Update database"), wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)), _("Update database"), wxItemKind::wxITEM_NORMAL);
	mainFrameToolbar->Realize();
	m_mgr.GetPane(mainFrameToolbar).BestSize(mainFrameToolbar->GetSize());

	CreateDebugToolbar();

	CreateMetadataPane();
	CreateGitPane();

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "frmclient/win/dlgs/applyChange.h"
#include "frmclient/win/dlgs/kindSelector/kindSelector.h"
#include "frmclient/win/dlgs/selectPredefined.h"

void ibFrontendMainFrameStudio::ShowRequest(const ibProtocolNode& request, ibProtocolNode& response)
{
	switch (static_cast<ibProtocol::ibRequest::ibKind>(request.GetInt(ibProtocol::ibRequest::Kind))) {
	case ibProtocol::ibRequest::ibKind::ApplyChange:
		// THE DESKTOP'S ibDialogApplyChange over the restructure's ledger — the base held on the server while it is open.
		// Cancelled — answered with no Done: everything rolled back.
		if (ibDialogApplyChange::ShowApplyChange(request, this))
			response.SetValue(ibProtocol::ibRequest::ibApplyChange::Done, true);
		break;
	case ibProtocol::ibRequest::ibKind::ObjectKind: {
		// THE DESKTOP'S ibDialogSelectTypeForm over a form just made, and a template's the same way. Cancelled — answered
		// with no Id: the object taken away again.
		ibDialogSelectKind dialog(request);
		dialog.ShowModal(response);
		break;
	}
	case ibProtocol::ibRequest::ibKind::PredefinedSelector:
		// THE DESKTOP'S ibShowPredefinedSelector, its tree of declared values — the type asked before it. Closed —
		// answered with no Id.
		ibShowPredefinedSelector(this, request, response);
		break;
	default:
		ibFrontendMainFrame::ShowRequest(request, response);
		break;
	}
}

#include "appstudio/mainFrame/metaTree/treeApplication.h"

void ibFrontendMainFrameStudio::DrawFrame(const ibProtocolNode& frame, const ibProtocolNode& patch)
{
	if (m_metaWindow == nullptr)
		return;

	// THE LAST TAB CLOSED — the focus goes back to the application's tree, and the inspector shows its row: the owner of
	// the editor closed, as the desktop's went back to the tree the editor came from (ibMetaTreeBase::Activate). A frame
	// sent whole is the start, not a close.
	if (patch.Has(ibProtocol::ibFrame::ActiveTab) && frame.GetInt(ibProtocol::ibFrame::ActiveTab) == 0)
		m_metaWindow->SetFocus();

	if (patch.IsNode() && !patch.Has(ibProtocol::ibFrame::MetaData))
		return;

	const ibProtocolNode root = frame.FindChild(ibProtocol::ibFrame::MetaData);
	m_metaWindow->Draw(root);

	// THE CAPTION — the desktop's Modify: "*" while somebody edited the application, "<!>" while the base does not hold
	// it; as the root row says.
	ibAuiPaneInfo& paneInfo = m_mgr.GetPane(ibAUI_PANE_METADATA);
	if (!paneInfo.IsOk())
		return;

	wxString caption = _("Application") + ' ';

	if (root.GetBool(ibProtocol::ibSchema::ibMetaData::Edited))
		caption += wxT('*');

	if (root.IsNode() && !root.GetBool(ibProtocol::ibSchema::ibMetaData::ApplicationSave, true))
		caption += wxT("<!>");

	if (caption != paneInfo.caption) {
		paneInfo.Caption(caption);
		m_mgr.Refresh();
	}
}

#pragma region debugger
void ibFrontendMainFrameStudio::Debugger_OnStateChanged()
{
	// Asked again now, and not left to the next time the menu is opened: a shortcut goes through the
	// menu item (wxFrameBase::ProcessCommand refuses a disabled one), and on Windows the menu is only
	// brought up to date when it opens.
	if (m_menuDebug != nullptr)
		m_menuDebug->UpdateUI();
}

bool ibFrontendMainFrameStudio::ListenDebug(std::function<void(const ibProtocolNode& news)> told,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	return GetCommunicator().ListenDebug(std::move(told), refusal, error);
}

bool ibFrontendMainFrameStudio::Debug(const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	return GetCommunicator().Debug(params, result, refusal, error);
}
#pragma endregion

bool ibFrontendMainFrameStudio::OpenForm(ibMetaID id, const ibMetaData& metaData)
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, static_cast<long long>(id));
	if (metaData.GetTabId() != 0)
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Tab, metaData.GetTabId());

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::MetaData));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibMetaData::ibCommand::Open));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return Call(ibProtocol::ibMethod::Schema, params, answer);
}

void ibFrontendMainFrameStudio::EditModule(const wxString& fileName, const ibGuid& moduleName, int line)
{
	if (m_metaWindow != nullptr)
		m_metaWindow->EditModule(fileName, moduleName, line, false);
}

bool ibFrontendMainFrameStudio::AllowClose()
{
	// ⭐ THE APPLICATION IS ASKED ABOUT BEFORE THE TABS, where the desktop asked after them: the base's Exit is the
	// server's, and once it says Exit the window goes (CallSettled) — a Cancel said after it could not keep it. Not asked,
	// an edit nobody saved went with the window (2026-10-28). A session already gone has nothing to save.
	if (m_metaWindow != nullptr && GetCommunicator().IsLoggedIn() && m_metaWindow->IsEdited()) {
		const int answer = ibMessageBox(
			ibString::Format(_("Application '%s' has been changed. Save?"), m_metaWindow->GetApplicationName()),
			wxTheApp->GetAppDisplayName(), wxYES | wxNO | wxCANCEL | wxCENTRE | wxICON_QUESTION, this);
		if (answer == wxCANCEL)
			return false;
		if (answer == wxYES && !m_metaWindow->SaveDatabase())
			return false;
	}
	return ibFrontendMainFrame::AllowClose();
}

std::vector<ibProtocol::ibSchema::ibKind> ibFrontendMainFrameStudio::GetStartSchemas() const
{
	return { ibProtocol::ibSchema::ibKind::Debugger };
}

bool ibFrontendMainFrameStudio::Show(bool show)
{
	bool ret = ibFrontendMainFrame::Show(show);
	if (ret && show) {
		// The debugger, over the application now logged in to — the desktop's was made with its application
		// (ibMetaDataApplicationStorage::OnInitialize): made over its store as the login read it (ShowSchema), else
		// asked for it.
		if (m_debugClient == nullptr)
			m_debugClient.reset(new ibDebuggerClient);
	}

	return ret;
}
