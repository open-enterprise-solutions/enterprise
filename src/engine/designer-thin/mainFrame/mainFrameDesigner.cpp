////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include "frmclient/artProvider/artProvider.h"   // the debugger's pictures, Update database's
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
	: ibFrontendMainFrame(std::move(communicator), ibProtocolMode::Designer, user, password, title, pos, size),
	m_stackWindow(new ibStackWindow(this, wxID_ANY)),
	m_watchWindow(new ibWatchWindow(this, wxID_ANY)),
	m_localWindow(new ibLocalWindow(this, wxID_ANY))
{
	// Its templates are the server's — the designer's doc manager's (DrawTemplates).
	m_docManager = new ibFrontendDocManager;
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

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "frmclient/win/dlgs/applyChange.h"

void ibFrontendMainFrameDesigner::ShowRequest(const ibProtocolNode& request, ibProtocolNode& response)
{
	// ⚠ FormType (the desktop's ibDialogSelectTypeForm) waits for the form editor — until it is, the base's default.
	switch (static_cast<ibProtocolRequestKind>(request.GetInt(ibProtocolName::Kind))) {
	case ibProtocolRequestKind::ApplyChange:
		// THE DESKTOP'S ibDialogApplyChange over the restructure's ledger — the base held on the server while it is open.
		// Cancelled — answered with no Done: everything rolled back.
		if (ibDialogApplyChange::ShowApplyChange(request, this))
			response.SetValue(ibProtocolName::Done, true);
		break;
	default:
		ibFrontendMainFrame::ShowRequest(request, response);
		break;
	}
}

#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"

void ibFrontendMainFrameDesigner::DrawFrame(const ibProtocolNode& frame, const ibProtocolNode& patch)
{
	if (m_metaWindow == nullptr || (patch.IsNode() && !patch.Has(ibProtocolName::MetaData)))
		return;

	const ibProtocolNode root = frame.FindChild(ibProtocolName::MetaData);
	m_metaWindow->Draw(root);

	// THE CAPTION — the desktop's Modify: "*" while somebody edited the configuration, "<!>" while the base does not hold
	// it; as the root row says.
	wxAuiPaneInfo& paneInfo = m_mgr.GetPane(wxAUI_PANE_METADATA);
	if (!paneInfo.IsOk())
		return;

	wxString caption = _("Configuration") + ' ';

	if (root.GetBool(ibProtocolName::Edited))
		caption += wxT('*');

	if (root.IsNode() && !root.GetBool(ibProtocolName::ConfigSave, true))
		caption += wxT("<!>");

	if (caption != paneInfo.caption) {
		paneInfo.Caption(caption);
		m_mgr.Refresh();
	}
}

#include "designer-thin/docManager/templates/docViewDataProcessorFile.h"
#include "designer-thin/docManager/templates/docViewDataReportFile.h"
#include "designer-thin/docManager/templates/docViewModuleEditor.h"

ibTabDocument* ibFrontendMainFrameDesigner::CreateDocument(ibProtocolViewKind kind, long long tabId)
{
	if (kind == ibProtocolViewKind::DataProcessor)
		return new ibDataProcessorFileDocument(tabId);
	if (kind == ibProtocolViewKind::Report)
		return new ibReportFileDocument(tabId);
	if (kind == ibProtocolViewKind::Module)
		return new ibModuleEditDocument(tabId);
	return ibFrontendMainFrame::CreateDocument(kind, tabId);
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
