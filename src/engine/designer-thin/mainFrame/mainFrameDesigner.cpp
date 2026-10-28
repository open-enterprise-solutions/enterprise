////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include "frmclient/artProvider/artProvider.h"   // the debugger's pictures, Update database's
#include "designer-thin/backend/debugger/debugClient.h"
#include "designer-thin/mainFrame/local/localWindow.h"
#include "designer-thin/mainFrame/stack/stackWindow.h"
#include "designer-thin/mainFrame/watch/watchWindow.h"

ibFrontendMainFrameDesigner* ibFrontendMainFrameDesigner::GetFrame() {
	ibFrontendMainFrame* instance = ibFrontendMainFrame::GetFrame();
	if (instance != nullptr) {
		ibFrontendMainFrameDesigner* designer_instance =
			dynamic_cast<ibFrontendMainFrameDesigner*>(instance);
		wxASSERT(designer_instance);
		return designer_instance;
	}
	return nullptr;
}

ibFrontendMainFrameDesigner::ibFrontendMainFrameDesigner(std::unique_ptr<ibCommunicator> communicator,
	const wxString& user, const wxString& password,
	const wxString& title, const wxPoint& pos, const wxSize& size)
	: ibFrontendMainFrame(std::move(communicator), ibProtocol::ibLogin::ibMode::Designer, user, password, title, pos, size),
	m_stackWindow(new ibStackWindow(this, wxID_ANY)),
	m_watchWindow(new ibWatchWindow(this, wxID_ANY)),
	m_localWindow(new ibLocalWindow(this, wxID_ANY))
{
	// Its templates are the server's — the designer's doc manager's (DrawTemplates).
	m_docManager = new ibFrontendDocManager;
}

ibFrontendMainFrameDesigner::~ibFrontendMainFrameDesigner()
{
	// The debugger first — its sockets let go and its bridges with it, while the panes they write into still stand.
	m_debugClient.reset();
}

void ibFrontendMainFrameDesigner::CreateGUI()
{
	ibFrontendMainFrame::CreateGUI();

	// The desktop designer's last button on the main toolbar — Configuration's Update database, the same command.
	wxAuiToolBar* const mainFrameToolbar = GetMainFrameToolbar();
	mainFrameToolbar->AddSeparator();
	mainFrameToolbar->AddTool(wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE, _("Update database"), wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)), _("Update database"), wxItemKind::wxITEM_NORMAL);
	mainFrameToolbar->Realize();
	m_mgr.GetPane(mainFrameToolbar).BestSize(mainFrameToolbar->GetSize());

	CreateDebugToolbar();

	CreateMetadataPane();
	CreateGitPane();

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "frmclient/win/dlgs/applyChange.h"
#include "frmclient/win/dlgs/formSelector/formSelector.h"
#include "frmclient/win/dlgs/selectPredefined.h"

void ibFrontendMainFrameDesigner::ShowRequest(const ibProtocolNode& request, ibProtocolNode& response)
{
	switch (static_cast<ibProtocol::ibRequest::ibKind>(request.GetInt(ibProtocol::ibRequest::Kind))) {
	case ibProtocol::ibRequest::ibKind::ApplyChange:
		// THE DESKTOP'S ibDialogApplyChange over the restructure's ledger — the base held on the server while it is open.
		// Cancelled — answered with no Done: everything rolled back.
		if (ibDialogApplyChange::ShowApplyChange(request, this))
			response.SetValue(ibProtocol::ibRequest::ibApplyChange::Done, true);
		break;
	case ibProtocol::ibRequest::ibKind::FormType: {
		// THE DESKTOP'S ibDialogSelectTypeForm over a form just made. Cancelled — answered with no Id: the form taken
		// away again.
		ibDialogSelectTypeForm dialog(request);
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

#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"

void ibFrontendMainFrameDesigner::DrawFrame(const ibProtocolNode& frame, const ibProtocolNode& patch)
{
	if (m_metaWindow == nullptr)
		return;

	// THE LAST TAB CLOSED — the focus goes back to the configuration's tree, and the inspector shows its row: the owner of
	// the editor closed, as the desktop's went back to the tree the editor came from (ibMetaTreeBase::Activate). A frame
	// sent whole is the start, not a close.
	if (patch.Has(ibProtocol::ibFrame::ActiveTab) && frame.GetInt(ibProtocol::ibFrame::ActiveTab) == 0)
		m_metaWindow->SetFocus();

	if (patch.IsNode() && !patch.Has(ibProtocol::ibFrame::MetaData))
		return;

	const ibProtocolNode root = frame.FindChild(ibProtocol::ibFrame::MetaData);
	m_metaWindow->Draw(root);

	// THE CAPTION — the desktop's Modify: "*" while somebody edited the configuration, "<!>" while the base does not hold
	// it; as the root row says.
	wxAuiPaneInfo& paneInfo = m_mgr.GetPane(wxAUI_PANE_METADATA);
	if (!paneInfo.IsOk())
		return;

	wxString caption = _("Configuration") + ' ';

	if (root.GetBool(ibProtocol::ibSchema::ibMetaData::Edited))
		caption += wxT('*');

	if (root.IsNode() && !root.GetBool(ibProtocol::ibSchema::ibMetaData::ConfigSave, true))
		caption += wxT("<!>");

	if (caption != paneInfo.caption) {
		paneInfo.Caption(caption);
		m_mgr.Refresh();
	}
}

#pragma region debugger
void ibFrontendMainFrameDesigner::Debugger_OnStateChanged()
{
	// Asked again now, and not left to the next time the menu is opened: a shortcut goes through the
	// menu item (wxFrameBase::ProcessCommand refuses a disabled one), and on Windows the menu is only
	// brought up to date when it opens.
	if (m_menuDebug != nullptr)
		m_menuDebug->UpdateUI();
}

bool ibFrontendMainFrameDesigner::ListenDebug(std::function<void(const ibProtocolNode& news)> told,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	return GetCommunicator().ListenDebug(std::move(told), refusal, error);
}

bool ibFrontendMainFrameDesigner::Debug(const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	return GetCommunicator().Debug(params, result, refusal, error);
}
#pragma endregion

#include "designer-thin/docManager/templates/docViewMetaFile.h"
#include "designer-thin/docManager/templates/docViewDataProcessorFile.h"
#include "designer-thin/docManager/templates/docViewDataReportFile.h"
#include "designer-thin/docManager/templates/docViewModuleEditor.h"
#include "designer-thin/docManager/templates/docViewFormEditor.h"
#include "designer-thin/docManager/templates/docViewInterface.h"
#include "designer-thin/docManager/templates/docViewCommonAttribute.h"
#include "designer-thin/docManager/templates/docViewFunctionalOption.h"
#include "designer-thin/docManager/templates/docViewRole.h"
#include "designer-thin/docManager/templates/docViewComposer.h"
#include "designer-thin/docManager/templates/docViewAssistant.h"

ibTabDocument* ibFrontendMainFrameDesigner::CreateDocument(ibProtocol::ibView::ibKind kind, long long tabId)
{
	if (kind == ibProtocol::ibView::ibKind::Configuration)
		return new ibMetadataBrowserDocument(tabId);   // a file's or the database's copy — the server's to tell apart
	if (kind == ibProtocol::ibView::ibKind::DataProcessor)
		return new ibDataProcessorFileDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::Report)
		return new ibReportFileDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::Module)
		return new ibModuleEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::FormDesigner)
		return new ibFormEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::Interface)
		return new ibInterfaceEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::CommonAttribute)
		return new ibCommonAttributeEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::FunctionalOption)
		return new ibFunctionalOptionEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::Role)
		return new ibRoleEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::Composer)
		return new ibComposerEditDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::Assistant)
		return new ibAssistantDocument(tabId);
	return ibFrontendMainFrame::CreateDocument(kind, tabId);
}

std::vector<ibProtocol::ibSchema::ibKind> ibFrontendMainFrameDesigner::GetStartSchemas() const
{
	return { ibProtocol::ibSchema::ibKind::Debugger };
}

bool ibFrontendMainFrameDesigner::Show(bool show)
{
	bool ret = ibFrontendMainFrame::Show(show);
	if (ret && show) {
		// The debugger, over the configuration now logged in to — the desktop's was made with its configuration
		// (ibMetaDataConfigurationStorage::OnInitialize): made over its store as the login read it (ShowSchema), else
		// asked for it.
		if (m_debugClient == nullptr)
			m_debugClient.reset(new ibDebuggerClient);

		// The assistant comes up with the designer where the desktop's window started it — on the server, as the
		// designer's session starts (ibClientFrameDesigner::CreateStartupPage); its refusal comes in the messages.
	}

	return ret;
}
